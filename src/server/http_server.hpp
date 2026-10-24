//===----------------------------------------------------------------------===//
// odata / server / http server
//
// Minimal HTTP/1.1 server on top of native sockets. Design doc section 27
// chooses "own HTTP layer, with the HTTP abstraction separately wrapped" so a
// different backend can be swapped in later.
//===----------------------------------------------------------------------===//
#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "server/http_request.hpp"
#include "server/http_response.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace duckdb_odata {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif

// Request handler receives a parsed request and returns a response.
typedef std::function<HttpResponse(const HttpRequest &)> HttpHandler;

class HttpServer {
public:
	virtual ~HttpServer() = default;

	// Bind and start accepting. Returns false if bind failed (e.g. port busy).
	virtual bool Start(const std::string &host, int port, HttpHandler handler) = 0;
	virtual void Stop() = 0;
	virtual bool IsRunning() const = 0;
};

// Simple blocking, thread-per-connection server.
class SocketHttpServer : public HttpServer {
public:
	SocketHttpServer() {
	}
	~SocketHttpServer() override;

	bool Start(const std::string &host, int port, HttpHandler handler) override;
	void Stop() override;
	bool IsRunning() const override;
	// Actual bound port (useful when the server was started with port 0,
	// i.e. "pick a free port").
	int GetBoundPort() const {
		return port;
	}

private:
	void AcceptLoop();
	void HandleConnection(SocketHandle client_fd);

	std::atomic<bool> running {false};
	std::atomic<bool> stop_requested {false};
	SocketHandle listen_fd = INVALID_SOCKET_HANDLE;
#ifdef _WIN32
	bool sockets_initialized = false;
#endif
	std::string host;
	int port = 0;
	HttpHandler handler;
	std::thread accept_thread;
};

} // namespace duckdb_odata
