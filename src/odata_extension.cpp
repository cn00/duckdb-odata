#include "odata_extension.hpp"

#include "duckdb/common/string_util.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "common/string_util.hpp"
#include "auth/auth.hpp"
#include "metadata/metadata_generator.hpp"
#include "parser/odata_parser.hpp"
#include "server/odata_server.hpp"

#include <mutex>
#include <utility>

namespace duckdb {

using duckdb_odata::EntityBinding;
using duckdb_odata::ODataServerRegistry;
using duckdb_odata::ODataServerState;

namespace {

enum class ODataActionKind { STOP, ENTITY };

struct ODataActionData : public TableFunctionData {
	ODataActionKind kind;
	std::string arg;         // table / schema
	std::string key;         // odata_entity key column
	shared_ptr<DatabaseInstance> db; // strong ref keeps instance alive
	int64_t max_top = 10000;
};

struct ODataGlobalState : public GlobalTableFunctionState {
	ODataGlobalState() : done(false) {
	}
	bool done;
};

// ---- odata_serve(): quack-style one-row result ---------------------------

struct ODataServeData : public TableFunctionData {
	// call inputs (all optional, defaults mirror quack_serve)
	std::string address;   // empty => localhost on a free port
	std::string token;     // honored only when token_provided
	bool token_provided = false;
	std::string base_path; // empty => "/odata"
	shared_ptr<DatabaseInstance> db; // keeps the instance alive while serving
};

unique_ptr<FunctionData> ServeBind(ClientContext &context, TableFunctionBindInput &input,
                                   vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<ODataServeData>();
	result->db = context.db;
	if (!input.inputs.empty()) {
		result->address = input.inputs[0].ToString();
	}
	auto it = input.named_parameters.find("token");
	if (it != input.named_parameters.end()) {
		result->token = it->second.ToString();
		result->token_provided = true;
	}
	it = input.named_parameters.find("base_path");
	if (it != input.named_parameters.end()) {
		result->base_path = it->second.ToString();
	}
	// quack_serve-style output: one row reporting the effective endpoint
	return_types.push_back(LogicalType::VARCHAR);
	names.emplace_back("listen_uri");
	return_types.push_back(LogicalType::VARCHAR);
	names.emplace_back("listen_url");
	return_types.push_back(LogicalType::VARCHAR);
	names.emplace_back("auth_token");
	return std::move(result);
}

void ServeExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &global = data_p.global_state->Cast<ODataGlobalState>();
	if (global.done) {
		return;
	}
	global.done = true;

	auto &serve = data_p.bind_data->Cast<ODataServeData>();
	// odata_serve() with no explicit token starts an authenticated server and
	// reports the auto-generated token back to the caller.
	std::string token = serve.token;
	if (!serve.token_provided) {
		token = duckdb_odata::GenerateAuthToken();
	}
	auto state = duckdb_odata::ODataServerRegistry::Get().GetOrCreate(*serve.db);
	std::string error;
	if (!duckdb_odata::StartODataServer(*state, serve.address, token, serve.base_path, error)) {
		throw InvalidInputException("odata_serve failed: %s", error);
	}
	// bind data is const during execute; report the effective endpoint
	// straight from the registry state (StartODataServer filled it in).
	output.SetCardinality(1);
	output.SetValue(0, 0, Value(state->listen_uri));
	output.SetValue(1, 0, Value(state->listen_url));
	output.SetValue(2, 0, Value(state->token));
}

unique_ptr<FunctionData> CommonBind(ClientContext &context, ODataActionKind kind, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<ODataActionData>();
	result->kind = kind;
	result->db = context.db;
	// read v0.1 server-side limit snapshot (SET odata_max_top = ...; CALL ...)
	Value max_top_val;
	if (context.TryGetCurrentSetting("odata_max_top", max_top_val) && !max_top_val.IsNull()) {
		result->max_top = max_top_val.GetValue<int64_t>();
	}
	// "Success" output column keeps CALL valid even though no rows are returned
	return_types.push_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");
	return std::move(result);
}

unique_ptr<FunctionData> StopBind(ClientContext &context, TableFunctionBindInput &input,
                                  vector<LogicalType> &return_types, vector<string> &names) {
	return CommonBind(context, ODataActionKind::STOP, input, return_types, names);
}

// odata_expose / odata_expose_schema: whitelist one table (or every table of
// one schema) and report back one row per exposed entity with the public
// entity-set name and the DuckDB table reference.
struct ODataExposeData : public TableFunctionData {
	std::string arg; // table ("t" or "s.t") or schema name
	shared_ptr<DatabaseInstance> db;
};

// Single-scalar SELECT helper (current_schema()/current_catalog()); used only
// to decide how to render a table reference.
static std::string ScalarQueryResult(duckdb::Connection &con, const std::string &sql) {
	auto res = con.Query(sql);
	if (!res || res->HasError() || res->RowCount() == 0) {
		return "";
	}
	return res->GetValue(0, 0).ToString();
}

// Render the DuckDB table a binding points at: the bare table name when it
// lives in the current schema, otherwise "schema.table". (v0.1 never exposes
// across catalogs, so the catalog is not part of the reference.)
static std::string BindingTableRef(const EntityBinding &binding, const std::string &current_schema) {
	if (!binding.schema.empty() && binding.schema != current_schema) {
		return binding.schema + "." + binding.table;
	}
	return binding.table;
}

unique_ptr<FunctionData> ExposeBind(ClientContext &context, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<ODataExposeData>();
	result->db = context.db;
	if (!input.inputs.empty()) {
		result->arg = input.inputs[0].ToString();
	}
	// shared shape for odata_expose and odata_expose_schema
	return_types.push_back(LogicalType::VARCHAR);
	names.emplace_back("entity");
	return_types.push_back(LogicalType::VARCHAR);
	names.emplace_back("table");
	return std::move(result);
}

void ExposeExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &global = data_p.global_state->Cast<ODataGlobalState>();
	if (global.done) {
		return;
	}
	global.done = true;

	auto &d = data_p.bind_data->Cast<ODataExposeData>();
	if (d.arg.empty()) {
		throw InvalidInputException("odata_expose requires a table name");
	}
	duckdb::Connection con(*d.db);
	EntityBinding binding;
	try {
		binding = duckdb_odata::ParseQualifiedBinding(d.arg);
		duckdb_odata::ResolveBindingTable(con, binding);
	} catch (const duckdb_odata::ODataParseException &e) {
		throw InvalidInputException("odata_expose failed: %s", e.what());
	}
	// replace an existing binding of the same entity name, else append
	{
		auto state = duckdb_odata::ODataServerRegistry::Get().GetOrCreate(*d.db);
		std::lock_guard<std::mutex> lock(state->mu);
		bool replaced = false;
		for (auto &b : state->bindings) {
			if (StringUtil::Lower(b.name) == StringUtil::Lower(binding.name)) {
				b = binding;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			state->bindings.push_back(binding);
		}
	}
	std::string current_schema = ScalarQueryResult(con, "SELECT current_schema()");
	output.SetCardinality(1);
	output.SetValue(0, 0, Value(binding.name));
	output.SetValue(1, 0, Value(BindingTableRef(binding, current_schema)));
}

void ExposeSchemaExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &global = data_p.global_state->Cast<ODataGlobalState>();
	if (global.done) {
		return;
	}
	global.done = true;

	auto &d = data_p.bind_data->Cast<ODataExposeData>();
	if (d.arg.empty()) {
		throw InvalidInputException("odata_expose_schema requires a schema name");
	}
	auto state = duckdb_odata::ODataServerRegistry::Get().GetOrCreate(*d.db);
	duckdb::Connection con(*d.db);
	auto res = con.Query("SELECT table_name, database_name FROM duckdb_tables() WHERE schema_name = " +
	                     duckdb_odata::QuoteStringLiteral(d.arg) +
	                     " AND NOT internal AND NOT temporary AND database_name = current_catalog()");
	if (res->HasError()) {
		throw InvalidInputException("odata_expose_schema failed: %s", res->GetError());
	}
	std::string current_schema = ScalarQueryResult(con, "SELECT current_schema()");
	std::vector<std::pair<std::string, std::string>> exposed; // (entity, table)
	for (idx_t r = 0; r < res->RowCount(); r++) {
		std::string tname = res->GetValue(0, r).ToString();
		if (tname.empty()) {
			continue;
		}
		EntityBinding binding;
		binding.table = tname;
		binding.schema = d.arg;
		binding.catalog = res->GetValue(1, r).ToString();
		// Public entity name: keep the bare table name (schema is only used
		// for qualified resolution; uniqueness is the caller's concern for
		// per-schema exposure).
		binding.name = tname;
		{
			std::lock_guard<std::mutex> lock(state->mu);
			bool found = false;
			for (auto &b : state->bindings) {
				if (StringUtil::Lower(b.name) == StringUtil::Lower(binding.name) &&
				    StringUtil::Lower(b.schema) == StringUtil::Lower(binding.schema) &&
				    StringUtil::Lower(b.catalog) == StringUtil::Lower(binding.catalog)) {
					b = binding;
					found = true;
					break;
				}
			}
			if (!found) {
				state->bindings.push_back(binding);
			}
		}
		exposed.emplace_back(binding.name, BindingTableRef(binding, current_schema));
	}
	output.SetCardinality(exposed.size());
	for (idx_t i = 0; i < exposed.size(); i++) {
		output.SetValue(0, i, Value(exposed[i].first));
		output.SetValue(1, i, Value(exposed[i].second));
	}
}

unique_ptr<FunctionData> EntityBind(ClientContext &context, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, vector<string> &names) {
	auto data = CommonBind(context, ODataActionKind::ENTITY, input, return_types, names);
	auto &d = data->Cast<ODataActionData>();
	if (!input.inputs.empty()) {
		d.arg = input.inputs[0].ToString();
	}
	auto it = input.named_parameters.find("key");
	if (it != input.named_parameters.end()) {
		d.key = it->second.ToString();
	}
	return data;
}

unique_ptr<GlobalTableFunctionState> CommonInit(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<ODataGlobalState>();
}

// Register / replace an exposed entity on the state of `db`.
std::shared_ptr<ODataServerState> GetStateFor(ClientContext &context) {
	return ODataServerRegistry::Get().GetOrCreate(*context.db);
}

void ActionExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &global = data_p.global_state->Cast<ODataGlobalState>();
	if (global.done) {
		return; // no output rows after the first call
	}
	global.done = true;

	auto &action = data_p.bind_data->Cast<ODataActionData>();
	switch (action.kind) {
	case ODataActionKind::STOP: {
		auto state = ODataServerRegistry::Get().Find(*context.db);
		if (state) {
			StopODataServer(*state);
		}
		break;
	}
	case ODataActionKind::ENTITY: {
		if (action.arg.empty() || action.key.empty()) {
			throw InvalidInputException("odata_entity requires a table name and a key column");
		}
		EntityBinding binding;
		try {
			binding = duckdb_odata::ParseQualifiedBinding(action.arg);
			duckdb::Connection con(*action.db);
			duckdb_odata::ResolveBindingTable(con, binding);
		} catch (const duckdb_odata::ODataParseException &e) {
			throw InvalidInputException("odata_entity failed: %s", e.what());
		}
		binding.configured_keys.push_back(action.key);
		auto state = GetStateFor(context);
		std::lock_guard<std::mutex> lock(state->mu);
		bool replaced = false;
		for (auto &b : state->bindings) {
			if (StringUtil::Lower(b.name) == StringUtil::Lower(binding.name)) {
				b.configured_keys = binding.configured_keys;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			state->bindings.push_back(binding);
		}
		break;
	}
	}
	output.SetCardinality(0);
}

// ---- odata_status() : real table function returning a single row ----------

unique_ptr<FunctionData> StatusBind(ClientContext &context, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, vector<string> &names) {
	return_types.push_back(LogicalType::BOOLEAN);
	names.emplace_back("running");
	return_types.push_back(LogicalType::VARCHAR);
	names.emplace_back("address");
	return_types.push_back(LogicalType::VARCHAR);
	names.emplace_back("base_path");
	return_types.push_back(LogicalType::BOOLEAN);
	names.emplace_back("token_required");
	return_types.push_back(LogicalType::BIGINT);
	names.emplace_back("exposed_entities");
	return nullptr;
}

unique_ptr<GlobalTableFunctionState> StatusInit(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<ODataGlobalState>();
}

void StatusExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &global = data_p.global_state->Cast<ODataGlobalState>();
	if (global.done) {
		return;
	}
	global.done = true;

	auto state = ODataServerRegistry::Get().Find(*context.db);
	bool running = state ? state->running : false;
	std::string address = state && !state->listen_url.empty() ? state->listen_url : (state ? state->started_address : "");
	std::string base_path = state ? state->base_path : "/odata";
	bool token_required = state ? !state->token.empty() : false;
	idx_t entity_count = state ? state->bindings.size() : 0;

	output.SetCardinality(1);
	output.SetValue(0, 0, Value::BOOLEAN(running));
	output.SetValue(1, 0, Value(address));
	output.SetValue(2, 0, Value(base_path));
	output.SetValue(3, 0, Value::BOOLEAN(token_required));
	output.SetValue(4, 0, Value::BIGINT((int64_t)entity_count));
}

void RegisterOptions(DatabaseInstance &db) {
	auto &config = DBConfig::GetConfig(db);
	config.AddExtensionOption("odata_max_top", "Maximum value allowed for $top", LogicalType::BIGINT,
	                          Value::BIGINT(10000));
	config.AddExtensionOption("odata_max_filter_depth", "Maximum nesting depth of $filter", LogicalType::BIGINT,
	                          Value::BIGINT(64));
	config.AddExtensionOption("odata_max_response_bytes", "Maximum response body size in bytes", LogicalType::BIGINT,
	                          Value::BIGINT(104857600));
	config.AddExtensionOption("odata_query_timeout_ms", "Query timeout in ms (0 = disabled)", LogicalType::BIGINT,
	                          Value::BIGINT(0));
}

void LoadInternal(ExtensionLoader &loader) {
	RegisterOptions(loader.GetDatabaseInstance());

	// odata_serve(): no-arg (defaults: localhost on a free port, auto token)
	// and single-address overloads; both accept token/base_path named params.
	TableFunction serve0("odata_serve", {}, ServeExecute, ServeBind, CommonInit);
	serve0.named_parameters["token"] = LogicalType::VARCHAR;
	serve0.named_parameters["base_path"] = LogicalType::VARCHAR;
	TableFunction serve1("odata_serve", {LogicalType::VARCHAR}, ServeExecute, ServeBind, CommonInit);
	serve1.named_parameters["token"] = LogicalType::VARCHAR;
	serve1.named_parameters["base_path"] = LogicalType::VARCHAR;
	TableFunctionSet serve_set("odata_serve");
	serve_set.AddFunction(serve0);
	serve_set.AddFunction(serve1);
	loader.RegisterFunction(serve_set);

	TableFunction stop("odata_stop", {}, ActionExecute, StopBind, CommonInit);
	loader.RegisterFunction(stop);

	TableFunction expose("odata_expose", {LogicalType::VARCHAR}, ExposeExecute, ExposeBind, CommonInit);
	loader.RegisterFunction(expose);

	TableFunction expose_schema("odata_expose_schema", {LogicalType::VARCHAR}, ExposeSchemaExecute, ExposeBind,
	                            CommonInit);
	loader.RegisterFunction(expose_schema);

	TableFunction entity("odata_entity", {LogicalType::VARCHAR}, ActionExecute, EntityBind, CommonInit);
	entity.named_parameters["key"] = LogicalType::VARCHAR;
	loader.RegisterFunction(entity);

	TableFunction status("odata_status", {}, StatusExecute, StatusBind, StatusInit);
	loader.RegisterFunction(status);
}

} // namespace

void ODataExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string ODataExtension::Name() {
	return "odata";
}

std::string ODataExtension::Version() const {
	return "0.1.0";
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void odata_duckdb_cpp_init(duckdb::ExtensionLoader &loader) {
	duckdb::LoadInternal(loader);
}
}
