#include "server/odata_server.hpp"

#include "auth/auth.hpp"
#include "common/string_util.hpp"
#include "compiler/sql_compiler.hpp"
#include "execution/query_executor.hpp"
#include "execution/result_serializer.hpp"
#include "metadata/metadata_generator.hpp"
#include "parser/odata_parser.hpp"
#include "server/http_server.hpp"

#include "duckdb.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"

#include <cctype>
#include <chrono>
#include <ctime>
#include <sstream>

namespace duckdb_odata {

using duckdb::Connection;
using duckdb::DatabaseInstance;

ODataServerRegistry &ODataServerRegistry::Get() {
	static ODataServerRegistry registry;
	return registry;
}

std::shared_ptr<ODataServerState> ODataServerRegistry::GetOrCreate(duckdb::DatabaseInstance &db) {
	std::lock_guard<std::mutex> lock(mu);
	auto it = states.find(&db);
	if (it != states.end()) {
		return it->second;
	}
	auto state = std::make_shared<ODataServerState>(db);
	states[&db] = state;
	return state;
}

std::shared_ptr<ODataServerState> ODataServerRegistry::Find(duckdb::DatabaseInstance &db) {
	std::lock_guard<std::mutex> lock(mu);
	auto it = states.find(&db);
	if (it != states.end()) {
		return it->second;
	}
	return nullptr;
}

namespace {

HttpResponse MakeError(int status, const std::string &code, const std::string &message) {
	HttpResponse resp;
	resp.status = status;
	resp.headers["Content-Type"] = "application/json";
	resp.body = "{\"error\":{\"code\":\"" + code + "\",\"message\":\"" + JsonEscape(message) + "\"}}";
	return resp;
}

HttpResponse MakeJson(int status, const std::string &body) {
	HttpResponse resp;
	resp.status = status;
	resp.headers["Content-Type"] = "application/json;odata.metadata=minimal;charset=utf-8";
	resp.headers["OData-Version"] = "4.0";
	resp.body = body;
	return resp;
}

std::string EntityContextUri(const ODataServerState &state, const std::string &entity_name, bool single_entity) {
	std::string ctx = state.base_path + "/$metadata#" + entity_name;
	if (single_entity) {
		ctx += "/$entity";
	}
	return ctx;
}

// Parse an absolute address "http://host:port[/path]"
bool ParseAddress(const std::string &address, std::string &host, int &port, std::string &path) {
	host = "0.0.0.0";
	port = 8080;
	path = "/odata";
	std::string a = address;
	// strip scheme
	auto scheme = a.find("://");
	if (scheme != std::string::npos) {
		a = a.substr(scheme + 3);
	}
	// host[:port][/path]
	auto slash = a.find('/');
	std::string hostport = slash == std::string::npos ? a : a.substr(0, slash);
	std::string rest = slash == std::string::npos ? "" : a.substr(slash + 1);
	auto colon = hostport.rfind(':');
	if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
		std::string port_str = hostport.substr(colon + 1);
		try {
			port = std::stoi(port_str);
		} catch (...) {
			return false;
		}
		hostport = hostport.substr(0, colon);
	}
	if (!hostport.empty()) {
		host = hostport;
	}
	if (!rest.empty()) {
		path = "/" + rest;
	}
	return true;
}

// %-decode and split "/odata/customers(1)" style paths
struct Route {
	bool service_doc = false;
	bool metadata = false;
	bool health = false;
	std::string entity;
	std::vector<std::string> keys; // populated for entity(key)
	std::string rest;              // remaining path (future: navigation)
};

Route ParseRoute(const ODataServerState &state, const std::string &full_path) {
	Route route;
	std::string path = full_path;
	// strip base path prefix (case-sensitive but compare insensitive)
	if (path == "/health") {
		route.health = true;
		return route;
	}
	if (path == "/") {
		route.service_doc = true;
		return route;
	}
	auto base = state.base_path;
	if (base.empty()) {
		base = "/";
	}
	if (base != "/") {
		if (ToLower(path).find(ToLower(base)) != 0) {
			return route; // not under base path
		}
		path = path.substr(base.size());
		if (path.empty() || path[0] != '/') {
			if (!path.empty()) {
				return route;
			}
			path = "/";
		}
	}
	if (path == "/") {
		route.service_doc = true;
		return route;
	}
	if (path == "/$metadata") {
		route.metadata = true;
		return route;
	}
	if (path[0] != '/') {
		return route;
	}
	std::string remaining = path.substr(1);
	// split off entity and any parenthesized key
	size_t i = 0;
	while (i < remaining.size() && remaining[i] != '(' && remaining[i] != '/') {
		i++;
	}
	route.entity = remaining.substr(0, i);
	if (i >= remaining.size()) {
		return route;
	}
	if (remaining[i] == '(') {
		// find matching close paren
		size_t close = remaining.find(')', i);
		if (close == std::string::npos) {
			return route;
		}
		std::string key_part = remaining.substr(i + 1, close - i - 1);
		if (!key_part.empty()) {
			route.keys.push_back(UrlDecode(Trim(key_part)));
		}
		route.rest = remaining.substr(close + 1);
		return route;
	}
	// '/key' style: treat as key too
	route.rest = remaining.substr(i);
	if (!route.rest.empty()) {
		route.rest = route.rest.substr(1); // strip '/'
		if (!route.rest.empty()) {
			route.keys.push_back(UrlDecode(route.rest));
		}
	}
	return route;
}

// The set of recognized $ system query options
bool IsSystemOption(const std::string &name) {
	return name == "$select" || name == "$filter" || name == "$orderby" || name == "$top" || name == "$skip" ||
	       name == "$count" || name == "$format" || name == "$expand" || name == "$apply" || name == "$search" ||
	       name == "$skiptoken";
}

} // namespace

// start/stop implementations (socket backend)
bool StartODataServer(ODataServerState &state, const std::string &address, const std::string &token,
                      const std::string &base_path, std::string &error) {
	std::lock_guard<std::mutex> lock(state.mu);
	if (state.running) {
		error = "server is already running";
		return false;
	}
	std::string host;
	int port = 0;
	std::string path;
	if (!ParseAddress(address, host, port, path)) {
		error = "invalid server address: '" + address + "'";
		return false;
	}
	auto server = std::make_shared<SocketHttpServer>();
	// The registry keeps `state` alive for the process lifetime (v0.1).
	if (!server->Start(host, port, [&state](const HttpRequest &request) { return HandleODataRequest(state, request); })) {
		error = "could not bind to " + host + ":" + std::to_string(port);
		return false;
	}
	state.host = host;
	state.port = port;
	state.base_path = base_path.empty() ? path : base_path;
	state.token = token;
	state.server = server;
	state.running = true;
	state.started_at = "now";
	state.started_address = address;
	return true;
}
void StopODataServer(ODataServerState &state) {
	std::lock_guard<std::mutex> lock(state.mu);
	if (state.server) {
		state.server->Stop();
		state.server.reset();
	}
	state.running = false;
}

HttpResponse HandleODataRequest(ODataServerState &state, const HttpRequest &request) {
	// method restriction: read-only GET in v0.1
	if (request.method != "GET") {
		return MakeError(405, "MethodNotAllowed", "only GET is supported");
	}
	// auth
	{
		std::lock_guard<std::mutex> lock(state.mu);
		if (!state.token.empty() && !CheckBearerToken(request.GetHeader("Authorization"), state.token)) {
			return MakeError(401, "Unauthorized", "missing or invalid bearer token");
		}
	}
	auto route = ParseRoute(state, request.path);
	if (route.health) {
		return MakeJson(200, "{\"status\":\"ok\"}");
	}

	// build binding list snapshot
	std::vector<EntityBinding> bindings;
	{
		std::lock_guard<std::mutex> lock(state.mu);
		bindings = state.bindings;
	}
	if (bindings.empty()) {
		return MakeError(404, "NotFound", "no entity sets are exposed (call odata_expose first)");
	}

	duckdb::Connection con(state.db);

	if (route.service_doc) {
		MetadataGenerator gen(con);
		std::string body = "{\"@odata.context\":\"" + state.base_path + "/$metadata\",\"value\":[";
		bool first = true;
		for (auto &e : bindings) {
			// only list entities whose tables resolve
			try {
				gen.BuildEntity(e);
			} catch (...) {
				continue;
			}
			if (!first) {
				body += ",";
			}
			first = false;
			body += "{\"name\":\"" + JsonEscape(e.name) + "\",\"kind\":\"EntitySet\",\"url\":\"" + JsonEscape(e.name) +
			        "\"}";
		}
		body += "]}";
		return MakeJson(200, body);
	}

	if (route.metadata) {
		MetadataGenerator gen(con);
		EdmModel model;
		for (auto &e : bindings) {
			try {
				model.entities.push_back(gen.BuildEntity(e));
			} catch (const ODataParseException &ex) {
				// a bound table that no longer resolves: skip it in metadata
				(void)ex;
			}
		}
		HttpResponse resp;
		resp.status = 200;
		resp.headers["Content-Type"] = "application/xml";
		resp.headers["OData-Version"] = "4.0";
		resp.body = GenerateEdmx(model);
		return resp;
	}

	if (route.entity.empty()) {
		return MakeError(404, "NotFound", "resource not found");
	}

	// find binding (case-insensitive)
	const EntityBinding *found = nullptr;
	for (auto &b : bindings) {
		if (ToLower(b.name) == ToLower(route.entity)) {
			found = &b;
			break;
		}
	}
	if (!found) {
		return MakeError(404, "NotFound", "entity set '" + route.entity + "' is not exposed");
	}

	// parse OData query options
	ODataQuery query;
	query.entity_set = found->name;
	for (auto &kv : request.query) {
		std::string name = kv.first;
		if (name.empty()) {
			continue;
		}
		if (name[0] != '$') {
			// non-system query options are ignored by v4 clients, but we only
			// tolerate the documented set to avoid silent misparsing
			continue;
		}
		if (!IsSystemOption(name)) {
			return MakeError(400, "InvalidQuery", "unsupported system query option '" + name + "'");
		}
	}
	try {
		for (auto &kv : request.query) {
			if (kv.first == "$select") {
				query.select = ParseSelect(kv.second);
			} else if (kv.first == "$filter") {
				query.filter = ParseFilter(kv.second);
			} else if (kv.first == "$orderby") {
				query.order_by = ParseOrderBy(kv.second);
			} else if (kv.first == "$top") {
				query.top = ParseNonNegativeInt("top", kv.second);
			} else if (kv.first == "$skip") {
				query.skip = ParseNonNegativeInt("skip", kv.second);
			} else if (kv.first == "$count") {
				query.count = ParseBooleanOption("count", kv.second);
			}
		}
	} catch (const ODataParseException &e) {
		return MakeError(400, "InvalidQuery", e.what());
	}

	MetadataGenerator gen(con);
	EdmEntity entity;
	try {
		entity = gen.BuildEntity(*found);
	} catch (const ODataParseException &e) {
		return MakeError(404, "NotFound", e.what());
	}

	// server-side top cap (design doc section 21)
	const int64_t odata_max_top = 10000;

	// key lookup
	if (!route.keys.empty()) {
		if (!entity.has_key) {
			return MakeError(400, "EntityHasNoKey", "entity set '" + entity.name + "' has no key configured");
		}
		if (route.keys.size() != 1) {
			return MakeError(400, "InvalidKey", "compound keys are not supported in v0.1");
		}
		CompiledQuery cq;
		try {
			SqlCompiler compiler;
			cq = compiler.CompileByKey(query, entity, route.keys);
		} catch (const ODataParseException &e) {
			return MakeError(400, "InvalidQuery", e.what());
		}
		LocalDuckDBExecutor executor;
		auto result = executor.Execute(con, cq);
		if (result->HasError()) {
			return MakeError(500, "InternalError", result->GetError());
		}
		std::string members;
		if (!SerializeFirstRowMembers(*result, members)) {
			return MakeError(404, "NotFound", "entity with key not found");
		}
		std::string body =
		    "{\"@odata.context\":\"" + EntityContextUri(state, entity.name, true) + "\"," + members + "}";
		return MakeJson(200, body);
	}

	// collection query
	CompiledQuery cq;
	CompiledQuery cq_count;
	try {
		SqlCompiler compiler;
		cq = compiler.CompileCollection(query, entity, odata_max_top);
		if (query.count) {
			cq_count = compiler.CompileCount(query, entity);
		}
	} catch (const ODataParseException &e) {
		return MakeError(400, "InvalidQuery", e.what());
	}

	LocalDuckDBExecutor executor;
	std::string body = "{\"@odata.context\":\"" + EntityContextUri(state, entity.name, false) + "\"";

	if (query.count) {
		// design doc section 22: v0.1 may run a second query for the count
		auto count_result = executor.Execute(con, cq_count);
		if (count_result->HasError()) {
			return MakeError(500, "InternalError", count_result->GetError());
		}
		auto count_chunk = count_result->Fetch();
		if (count_chunk && count_chunk->size() > 0) {
			auto v = count_chunk->GetValue(0, 0);
			body += ",\"@odata.count\":" + ValueToJson(v);
		} else {
			body += ",\"@odata.count\":0";
		}
	}

	auto result = executor.Execute(con, cq);
	if (result->HasError()) {
		return MakeError(500, "InternalError", result->GetError());
	}
	uint64_t rows = 0;
	std::string rows_json = SerializeRows(*result, rows);
	body += ",\"value\":[" + rows_json + "]}";
	return MakeJson(200, body);
}

} // namespace duckdb_odata
