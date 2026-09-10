#include "server/http_server.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include <cstring>
#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <vector>

namespace duckdb_odata {

namespace {

// Read until CRLFCRLF or EOF; returns false on error/timeout.
bool ReadRequestHead(int fd, std::string &head, std::string &body) {
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
			if (pos > (1 << 16)) return false;
			head = buffer.substr(0, pos + 4);
			body = buffer.substr(pos + 4);
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
	if (host.empty() || host == "0.0.0.0" || host == "::") {
		addr.sin_addr.s_addr = INADDR_ANY;
	} else {
		// resolve a hostname (e.g. "localhost") or IPv4 literal
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		struct addrinfo *result = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
			close(listen_fd);
			listen_fd = -1;
			return false;
		}
		auto *sin = reinterpret_cast<struct sockaddr_in *>(result->ai_addr);
		addr.sin_addr = sin->sin_addr;
		freeaddrinfo(result);
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
	// port 0 => kernel picked a free port; read it back so callers can
	// report the real listen_url (like quack_serve does).
	if (port == 0) {
		sockaddr_in bound;
		socklen_t bound_len = sizeof(bound);
		if (getsockname(listen_fd, reinterpret_cast<struct sockaddr *>(&bound), &bound_len) == 0) {
			port = ntohs(bound.sin_port);
		}
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
	timeval timeout {10, 0};
	setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
	int no_sigpipe = 1;
	setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
	try {
		std::string head;
		std::string body;
		bool ok = ReadRequestHead(client_fd, head, body);
		if (!ok) {
			response.status = 400;
			response.body = "{\"error\":{\"code\":\"InvalidRequest\",\"message\":\"invalid request header\"}}";
		} else {
			HttpRequest request;
			if (!ParseHttpRequest(head, request)) {
				response.status = 400;
				response.body = "{\"error\":{\"code\":\"InvalidRequest\",\"message\":\"invalid request header\"}}";
			} else {
				// Bounded Content-Length framing. Chunked request bodies are not supported.
				size_t length = 0;
				bool valid = !request.HasHeader("Transfer-Encoding");
				auto raw_length = request.GetHeader("Content-Length");
				if (request.HasHeader("Content-Length") && raw_length.empty()) valid = false;
				for (char c : raw_length) {
					if (c < '0' || c > '9' || length > 1048576) { valid = false; break; }
					length = length * 10 + (c - '0');
				}
				if (length > 1048576) {
					response.status = 413;
					response.body = "{\"error\":{\"code\":\"PayloadTooLarge\",\"message\":\"request body exceeds 1 MiB\"}}";
				} else {
					while (valid && body.size() < length) {
						char buffer[4096];
						auto n = read(client_fd, buffer, std::min(sizeof(buffer), length - body.size()));
						if (n <= 0) { valid = false; break; }
						body.append(buffer, static_cast<size_t>(n));
					}
					if (!valid) {
						response.status = 400;
						response.body = "{\"error\":{\"code\":\"InvalidRequest\",\"message\":\"invalid or unsupported body framing\"}}";
					} else {
						request.body = body.substr(0, length);
						response = handler(request);
					}
				}
			}
		}
	} catch (const std::exception &ex) {
		// never let a request-handler exception escape a connection thread:
		// an uncaught exception there would std::terminate the whole process
		response.status = 500;
		response.headers["Content-Type"] = "application/json";
		response.body = "{\"error\":{\"code\":\"500\",\"message\":\"" + JsonEscape(ex.what()) + "\"}}";
	} catch (...) {
		response.status = 500;
		response.headers["Content-Type"] = "application/json";
		response.body = "{\"error\":{\"code\":\"500\",\"message\":\"internal error\"}}";
	}
	if (!response.headers.count("Content-Type")) {
		response.headers["Content-Type"] = response.status >= 400 ? "application/json" : "text/plain";
	}
	std::string wire = response.ToWire();
	// best-effort write (ignore partial-write edge cases for v0.1)
	const char *data = wire.data();
	size_t remaining = wire.size();
	while (remaining > 0) {
#ifdef MSG_NOSIGNAL
		ssize_t n = send(client_fd, data, remaining, MSG_NOSIGNAL);
#else
		ssize_t n = send(client_fd, data, remaining, 0);
#endif
		if (n <= 0) {
			break;
		}
		data += n;
		remaining -= static_cast<size_t>(n);
	}
	if (response.status == 413) {
		// Deliver the rejection before closing a socket with unread request data.
		// Bounded draining avoids a TCP reset masking the 413 for uploading clients.
		shutdown(client_fd, SHUT_WR);
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		size_t drained = 0;
		while (drained < 8 * 1048576 && std::chrono::steady_clock::now() < deadline) {
			pollfd pending {client_fd, POLLIN, 0};
			if (poll(&pending, 1, 100) <= 0) break;
			char discard[8192];
			auto n = recv(client_fd, discard, sizeof(discard), MSG_DONTWAIT);
			if (n <= 0) break;
			drained += static_cast<size_t>(n);
		}
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
