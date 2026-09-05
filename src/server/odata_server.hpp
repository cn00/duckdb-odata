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
	bool running = false;
	std::string started_at;
	std::string started_address;
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
bool StartODataServer(ODataServerState &state, const std::string &address, const std::string &token,
                      const std::string &base_path, std::string &error);
void StopODataServer(ODataServerState &state);

} // namespace duckdb_odata
