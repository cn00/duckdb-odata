//===----------------------------------------------------------------------===//
// odata / server / odata server
//
// Owns process-global (per-DatabaseInstance) state and routes HTTP requests:
//
//   GET /health
//   GET {base_path}            -> service document
//   GET {base_path}/$metadata  -> EDMX XML
//   GET {base_path}/{entity}          -> entity set query ($select/$filter/...)
//   GET {base_path}/{entity}({key})   -> single entity by key
//
// Security/limits are enforced here (design doc sections 34-40).
//===----------------------------------------------------------------------===//
#pragma once

#include "metadata/metadata_generator.hpp"
#include "server/http_request.hpp"
#include "server/http_response.hpp"
#include "server/http_server.hpp"

#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

namespace duckdb {
class DatabaseInstance;
}

namespace duckdb_odata {

// Server + whitelist state for one DatabaseInstance. v0.1 assumes the DuckDB
// process (and therefore the instance) outlives the server thread.
class ODataServerState {
public:
	explicit ODataServerState(duckdb::DatabaseInstance &db_p) : db(db_p) {
	}

	std::mutex mu;
	duckdb::DatabaseInstance &db;
	std::vector<EntityBinding> bindings; // exposed entities (whitelist)
	// serve config
	std::string host = "0.0.0.0";
	int port = 0;
	std::string base_path = "/odata";
	std::string token; // empty => no auth
	int64_t max_top = 10000;
	int64_t max_filter_depth = 64;
	int64_t max_response_bytes = 104857600;
	int64_t query_timeout_ms = 0;
	int64_t page_size = 0; // 0 disables server-driven paging
	int64_t max_concurrent_queries = 0; // 0 disables the limit
	std::atomic<int64_t> active_queries {0};
	bool running = false;
	std::string started_at;
	std::string started_address;
	// Effective endpoint once bound (address may be empty/"localhost:0").
	std::string listen_uri;  // e.g. "odata:localhost" (quack-style)
	std::string listen_url;  // e.g. "http://localhost:34567"
	std::shared_ptr<HttpServer> server;
};

// Global registry of server states keyed by DatabaseInstance*.
class ODataServerRegistry {
public:
	static ODataServerRegistry &Get();

	// state is created on first use for an instance and never removed while the
	// process lives (matching the v0.1 embedded/CLI deployment).
	std::shared_ptr<ODataServerState> GetOrCreate(duckdb::DatabaseInstance &db);
	std::shared_ptr<ODataServerState> Find(duckdb::DatabaseInstance &db);

private:
	std::mutex mu;
	std::unordered_map<duckdb::DatabaseInstance *, std::shared_ptr<ODataServerState>> states;
};

// Handle one HTTP request using the state (registry + db). Called from the
// per-connection server thread.
HttpResponse HandleODataRequest(ODataServerState &state, const HttpRequest &request);

// start/stop helpers used by the SQL-level procedures
// Empty address means "localhost on a free port". On success the state's
// host/port/listen_uri/listen_url reflect the real bound endpoint.
bool StartODataServer(ODataServerState &state, const std::string &address, const std::string &token,
                      const std::string &base_path, int64_t max_top, int64_t max_filter_depth,
                      int64_t max_response_bytes, int64_t query_timeout_ms, int64_t page_size,
                      int64_t max_concurrent_queries, std::string &error);
void StopODataServer(ODataServerState &state);

} // namespace duckdb_odata
