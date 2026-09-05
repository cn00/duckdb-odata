#include "server/http_server.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace duckdb_odata {

namespace {

// Read until CRLFCRLF or EOF; returns false on error/timeout.
bool ReadRequestHead(int fd, std::string &head) {
	std::string buffer;
	char chunk[4096];
	while (true) {
		ssize_t n = read(fd, chunk, sizeof(chunk));
		if (n <= 0) {
			return false;
		}
		buffer.append(chunk, static_cast<size_t>(n));
		auto pos = buffer.find("\r\n\r\n");
		if (pos != std::string::npos) {
			head = buffer.substr(0, pos + 4);
			return true;
		}
		if (buffer.size() > 1 << 16) {
			return false; // header too large
		}
	}
}

} // namespace

SocketHttpServer::~SocketHttpServer() {
	Stop();
}

bool SocketHttpServer::Start(const std::string &host_p, int port_p, HttpHandler handler_p) {
	if (running.load()) {
		return false;
	}
	host = host_p;
	port = port_p;
	handler = std::move(handler_p);

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		return false;
	}
	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<uint16_t>(port));
	if (host.empty() || host == "0.0.0.0") {
		addr.sin_addr.s_addr = INADDR_ANY;
	} else {
		if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
			close(listen_fd);
			listen_fd = -1;
			return false;
		}
	}
	if (bind(listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
		close(listen_fd);
		listen_fd = -1;
		return false;
	}
	if (listen(listen_fd, 32) != 0) {
		close(listen_fd);
		listen_fd = -1;
		return false;
	}
	stop_requested = false;
	running = true;
	accept_thread = std::thread([this] { AcceptLoop(); });
	return true;
}

void SocketHttpServer::AcceptLoop() {
	while (!stop_requested.load()) {
		sockaddr_in client_addr;
		socklen_t addr_len = sizeof(client_addr);
		int client_fd = accept(listen_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &addr_len);
		if (client_fd < 0) {
			if (stop_requested.load()) {
				break;
			}
			continue;
		}
		// short-lived per-connection thread; detached to keep v0.1 simple
		std::thread t([this, client_fd] { HandleConnection(client_fd); });
		t.detach();
	}
	running = false;
}

void SocketHttpServer::HandleConnection(int client_fd) {
	HttpResponse response;
	try {
		std::string head;
		bool ok = ReadRequestHead(client_fd, head);
		if (!ok) {
			response.status = 400;
			response.body = "400 Bad Request";
		} else {
			HttpRequest request;
			if (!ParseHttpRequest(head, request)) {
				response.status = 400;
				response.body = "400 Bad Request";
			} else {
				response = handler(request);
			}
		}
	} catch (const std::exception &ex) {
		// never let a request-handler exception escape a connection thread:
		// an uncaught exception there would std::terminate the whole process
		response.status = 500;
		response.headers["Content-Type"] = "application/json";
		response.body = "{\"error\":{\"code\":\"500\",\"message\":\"" + std::string(ex.what()) + "\"}}";
	} catch (...) {
		response.status = 500;
		response.headers["Content-Type"] = "application/json";
		response.body = "{\"error\":{\"code\":\"500\",\"message\":\"internal error\"}}";
	}
	if (!response.headers.count("Content-Type")) {
		response.headers["Content-Type"] = "text/plain";
	}
	std::string wire = response.ToWire();
	// best-effort write (ignore partial-write edge cases for v0.1)
	const char *data = wire.data();
	size_t remaining = wire.size();
	while (remaining > 0) {
		ssize_t n = write(client_fd, data, remaining);
		if (n <= 0) {
			break;
		}
		data += n;
		remaining -= static_cast<size_t>(n);
	}
	close(client_fd);
}

void SocketHttpServer::Stop() {
	if (!running.load() && !stop_requested.load()) {
		return;
	}
	stop_requested = true;
	// closing the listen socket unblocks accept()
	if (listen_fd >= 0) {
		shutdown(listen_fd, SHUT_RDWR);
		close(listen_fd);
		listen_fd = -1;
	}
	if (accept_thread.joinable()) {
		accept_thread.join();
	}
	running = false;
}

bool SocketHttpServer::IsRunning() const {
	return running.load() || (listen_fd >= 0 && !stop_requested.load());
}

} // namespace duckdb_odata
