//===----------------------------------------------------------------------===//
// odata / server / http server
//
// Minimal HTTP/1.1 server on top of POSIX sockets. Design doc section 27
// chooses "own HTTP layer, with the HTTP abstraction separately wrapped" so a
// different backend can be swapped in later.
//===----------------------------------------------------------------------===//
#pragma once

#include "server/http_request.hpp"
#include "server/http_response.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace duckdb_odata {

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
	void HandleConnection(int client_fd);

	std::atomic<bool> running {false};
	std::atomic<bool> stop_requested {false};
	int listen_fd = -1;
	std::string host;
	int port = 0;
	HttpHandler handler;
	std::thread accept_thread;
};

} // namespace duckdb_odata
