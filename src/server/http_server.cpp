#include "server/http_server.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <poll.h>
#endif

#include <climits>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <vector>

namespace duckdb_odata {

namespace {

#ifdef _WIN32
using SocketLength = int;
constexpr int SHUTDOWN_WRITE = SD_SEND;
constexpr int SHUTDOWN_BOTH = SD_BOTH;
#else
using SocketLength = socklen_t;
constexpr int SHUTDOWN_WRITE = SHUT_WR;
constexpr int SHUTDOWN_BOTH = SHUT_RDWR;
#endif

void CloseSocket(SocketHandle fd) {
#ifdef _WIN32
	closesocket(fd);
#else
	close(fd);
#endif
}

int Receive(SocketHandle fd, char *buffer, size_t length, int flags = 0) {
	return static_cast<int>(recv(fd, buffer, static_cast<int>(std::min(length, size_t(INT_MAX))), flags));
}

// Read until CRLFCRLF or EOF; returns false on error/timeout.
bool ReadRequestHead(SocketHandle fd, std::string &head, std::string &body) {
	std::string buffer;
	char chunk[4096];
	while (true) {
		auto n = Receive(fd, chunk, sizeof(chunk));
		if (n <= 0) {
			return false;
		}
		buffer.append(chunk, static_cast<size_t>(n));
		auto pos = buffer.find("\r\n\r\n");
		if (pos != std::string::npos) {
			if (pos > (1 << 16)) {
				return false;
			}
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
#ifdef _WIN32
	if (sockets_initialized) {
		WSACleanup();
	}
#endif
}

bool SocketHttpServer::Start(const std::string &host_p, int port_p, HttpHandler handler_p) {
	if (running.load()) {
		return false;
	}
#ifdef _WIN32
	if (!sockets_initialized) {
		WSADATA data;
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
			return false;
		}
		sockets_initialized = true;
	}
#endif
	host = host_p;
	port = port_p;
	handler = std::move(handler_p);

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == INVALID_SOCKET_HANDLE) {
		return false;
	}
	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt));

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
			CloseSocket(listen_fd);
			listen_fd = INVALID_SOCKET_HANDLE;
			return false;
		}
		auto *sin = reinterpret_cast<struct sockaddr_in *>(result->ai_addr);
		addr.sin_addr = sin->sin_addr;
		freeaddrinfo(result);
	}
	if (bind(listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
		CloseSocket(listen_fd);
		listen_fd = INVALID_SOCKET_HANDLE;
		return false;
	}
	if (listen(listen_fd, 32) != 0) {
		CloseSocket(listen_fd);
		listen_fd = INVALID_SOCKET_HANDLE;
		return false;
	}
	// port 0 => kernel picked a free port; read it back so callers can
	// report the real listen_url (like quack_serve does).
	if (port == 0) {
		sockaddr_in bound;
		SocketLength bound_len = sizeof(bound);
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
		SocketLength addr_len = sizeof(client_addr);
		auto client_fd = accept(listen_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &addr_len);
		if (client_fd == INVALID_SOCKET_HANDLE) {
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

void SocketHttpServer::HandleConnection(SocketHandle client_fd) {
	HttpResponse response;
#ifdef _WIN32
	DWORD timeout = 10000;
#else
	timeval timeout {10, 0};
#endif
	setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
	setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
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
				if (request.HasHeader("Content-Length") && raw_length.empty()) {
					valid = false;
				}
				for (char c : raw_length) {
					if (c < '0' || c > '9' || length > 1048576) {
						valid = false;
						break;
					}
					length = length * 10 + (c - '0');
				}
				if (length > 1048576) {
					response.status = 413;
					response.body =
					    "{\"error\":{\"code\":\"PayloadTooLarge\",\"message\":\"request body exceeds 1 MiB\"}}";
				} else {
					while (valid && body.size() < length) {
						char buffer[4096];
						auto n = Receive(client_fd, buffer, std::min(sizeof(buffer), length - body.size()));
						if (n <= 0) {
							valid = false;
							break;
						}
						body.append(buffer, static_cast<size_t>(n));
					}
					if (!valid) {
						response.status = 400;
						response.body = "{\"error\":{\"code\":\"InvalidRequest\",\"message\":\"invalid or unsupported "
						                "body framing\"}}";
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
		constexpr int flags = MSG_NOSIGNAL;
#else
		constexpr int flags = 0;
#endif
		auto n = send(client_fd, data, static_cast<int>(std::min(remaining, size_t(INT_MAX))), flags);
		if (n <= 0) {
			break;
		}
		data += n;
		remaining -= static_cast<size_t>(n);
	}
	if (response.status == 413) {
		// Deliver the rejection before closing a socket with unread request data.
		// Bounded draining avoids a TCP reset masking the 413 for uploading clients.
		shutdown(client_fd, SHUTDOWN_WRITE);
#ifdef _WIN32
		u_long nonblocking = 1;
		auto nonblocking_result = ioctlsocket(client_fd, FIONBIO, &nonblocking);
#endif
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		size_t drained = 0;
		while (drained < size_t(8) * 1048576 && std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
			if (nonblocking_result != 0) {
				break;
			}
			WSAPOLLFD pending {client_fd, POLLRDNORM, 0};
			if (WSAPoll(&pending, 1, 100) <= 0) {
				break;
			}
#else
			pollfd pending {client_fd, POLLIN, 0};
			if (poll(&pending, 1, 100) <= 0) {
				break;
			}
#endif
			char discard[8192];
#ifdef _WIN32
			auto n = Receive(client_fd, discard, sizeof(discard));
#else
			auto n = Receive(client_fd, discard, sizeof(discard), MSG_DONTWAIT);
#endif
			if (n <= 0) {
				break;
			}
			drained += static_cast<size_t>(n);
		}
	}
	CloseSocket(client_fd);
}

void SocketHttpServer::Stop() {
	if (!running.load() && !stop_requested.load()) {
		return;
	}
	stop_requested = true;
	// closing the listen socket unblocks accept()
	if (listen_fd != INVALID_SOCKET_HANDLE) {
		shutdown(listen_fd, SHUTDOWN_BOTH);
		CloseSocket(listen_fd);
		listen_fd = INVALID_SOCKET_HANDLE;
	}
	if (accept_thread.joinable()) {
		accept_thread.join();
	}
	running = false;
}

bool SocketHttpServer::IsRunning() const {
	return running.load() || (listen_fd != INVALID_SOCKET_HANDLE && !stop_requested.load());
}

} // namespace duckdb_odata
