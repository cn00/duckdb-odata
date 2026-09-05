#include "odata_extension.hpp"

#include "duckdb/common/string_util.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "common/string_util.hpp"
#include "metadata/metadata_generator.hpp"
#include "server/odata_server.hpp"

namespace duckdb {

using duckdb_odata::EntityBinding;
using duckdb_odata::ODataServerRegistry;
using duckdb_odata::ODataServerState;

namespace {

enum class ODataActionKind { SERVE, STOP, EXPOSE, EXPOSE_SCHEMA, ENTITY };

struct ODataActionData : public TableFunctionData {
	ODataActionKind kind;
	std::string arg;         // address / table / schema
	std::string token;       // odata_serve token
	std::string base_path;   // odata_serve base_path
	std::string key;         // odata_entity key column
	shared_ptr<DatabaseInstance> db; // strong ref keeps instance alive
	int64_t max_top = 10000;
};

struct ODataGlobalState : public GlobalTableFunctionState {
	ODataGlobalState() : done(false) {
	}
	bool done;
};

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

unique_ptr<FunctionData> ServeBind(ClientContext &context, TableFunctionBindInput &input,
                                   vector<LogicalType> &return_types, vector<string> &names) {
	auto data = CommonBind(context, ODataActionKind::SERVE, input, return_types, names);
	auto &d = data->Cast<ODataActionData>();
	if (!input.inputs.empty()) {
		d.arg = input.inputs[0].ToString();
	}
	auto it = input.named_parameters.find("token");
	if (it != input.named_parameters.end()) {
		d.token = it->second.ToString();
	}
	it = input.named_parameters.find("base_path");
	if (it != input.named_parameters.end()) {
		d.base_path = it->second.ToString();
	}
	return data;
}

unique_ptr<FunctionData> StopBind(ClientContext &context, TableFunctionBindInput &input,
                                  vector<LogicalType> &return_types, vector<string> &names) {
	return CommonBind(context, ODataActionKind::STOP, input, return_types, names);
}

unique_ptr<FunctionData> ExposeBind(ClientContext &context, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, vector<string> &names) {
	auto data = CommonBind(context, ODataActionKind::EXPOSE, input, return_types, names);
	if (!input.inputs.empty()) {
		data->Cast<ODataActionData>().arg = input.inputs[0].ToString();
	}
	return data;
}

unique_ptr<FunctionData> ExposeSchemaBind(ClientContext &context, TableFunctionBindInput &input,
                                          vector<LogicalType> &return_types, vector<string> &names) {
	auto data = CommonBind(context, ODataActionKind::EXPOSE_SCHEMA, input, return_types, names);
	if (!input.inputs.empty()) {
		data->Cast<ODataActionData>().arg = input.inputs[0].ToString();
	}
	return data;
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
	case ODataActionKind::SERVE: {
		auto state = GetStateFor(context);
		std::string error;
		if (!StartODataServer(*state, action.arg, action.token, action.base_path, error)) {
			throw InvalidInputException("odata_serve failed: %s", error);
		}
		break;
	}
	case ODataActionKind::STOP: {
		auto state = ODataServerRegistry::Get().Find(*context.db);
		if (state) {
			StopODataServer(*state);
		}
		break;
	}
	case ODataActionKind::EXPOSE: {
		if (action.arg.empty()) {
			throw InvalidInputException("odata_expose requires a table name");
		}
		auto state = GetStateFor(context);
		EntityBinding binding;
		binding.name = action.arg;
		binding.table = action.arg;
		binding.schema.clear();
		// replace / append
		bool replaced = false;
		{
			std::lock_guard<std::mutex> lock(state->mu);
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
		break;
	}
	case ODataActionKind::EXPOSE_SCHEMA: {
		if (action.arg.empty()) {
			throw InvalidInputException("odata_expose_schema requires a schema name");
		}
		auto state = GetStateFor(context);
		duckdb::Connection con(*action.db);
		auto res = con.Query("SELECT table_name, schema_name FROM duckdb_tables() WHERE schema_name = " +
		                     duckdb_odata::QuoteStringLiteral(action.arg) + " AND NOT internal");
		if (res->HasError()) {
			throw InvalidInputException("odata_expose_schema failed: %s", res->GetError());
		}
		for (idx_t r = 0; r < res->RowCount(); r++) {
			std::string tname = res->GetValue(0, r).ToString();
			if (tname.empty()) {
				continue;
			}
			EntityBinding binding;
			binding.name = tname;
			binding.table = tname;
			binding.schema = action.arg;
			{
				std::lock_guard<std::mutex> lock(state->mu);
				bool found = false;
				for (auto &b : state->bindings) {
					if (StringUtil::Lower(b.name) == StringUtil::Lower(binding.name)) {
						b = binding;
						found = true;
						break;
					}
				}
				if (!found) {
					state->bindings.push_back(binding);
				}
			}
		}
		break;
	}
	case ODataActionKind::ENTITY: {
		if (action.arg.empty() || action.key.empty()) {
			throw InvalidInputException("odata_entity requires a table name and a key column");
		}
		auto state = GetStateFor(context);
		EntityBinding binding;
		binding.name = action.arg;
		binding.table = action.arg;
		binding.configured_keys.push_back(action.key);
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
	std::string address = state ? state->started_address : "";
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

	TableFunction serve("odata_serve", {LogicalType::VARCHAR}, ActionExecute, ServeBind, CommonInit);
	serve.named_parameters["token"] = LogicalType::VARCHAR;
	serve.named_parameters["base_path"] = LogicalType::VARCHAR;
	loader.RegisterFunction(serve);

	TableFunction stop("odata_stop", {}, ActionExecute, StopBind, CommonInit);
	loader.RegisterFunction(stop);

	TableFunction expose("odata_expose", {LogicalType::VARCHAR}, ActionExecute, ExposeBind, CommonInit);
	loader.RegisterFunction(expose);

	TableFunction expose_schema("odata_expose_schema", {LogicalType::VARCHAR}, ActionExecute, ExposeSchemaBind,
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
