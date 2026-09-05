#include "metadata/metadata_generator.hpp"

#include "common/string_util.hpp"
#include "parser/odata_parser.hpp"

#include "duckdb.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/materialized_query_result.hpp"

namespace duckdb_odata {

using duckdb::Connection;
using duckdb::MaterializedQueryResult;
using duckdb::Value;

namespace {

std::string SqlLiteral(const std::string &value) {
	std::string s = "'";
	for (char c : value) {
		if (c == '\'') {
			s += "''";
		} else {
			s += c;
		}
	}
	s += "'";
	return s;
}

duckdb::unique_ptr<MaterializedQueryResult> RunSql(Connection &con, const std::string &sql) {
	auto result = con.Query(sql);
	if (result->HasError()) {
		throw ODataParseException("catalog introspection failed: " + result->GetError());
	}
	return result;
}

std::string GetString(duckdb::unique_ptr<MaterializedQueryResult> &result, idx_t row, idx_t col) {
	auto value = result->GetValue(col, row);
	if (value.IsNull()) {
		return "";
	}
	return value.ToString();
}

std::vector<std::string> TableNamesOf(const EdmModel &model) {
	std::vector<std::string> names;
	names.reserve(model.entities.size());
	for (auto &e : model.entities) {
		names.push_back(e.table);
	}
	return names;
}

} // namespace

EdmEntity MetadataGenerator::BuildEntity(const EntityBinding &binding) const {
	EdmEntity entity;
	entity.name = binding.name;
	entity.table = binding.table;

	// Resolve schema + columns from the live catalog of the connection.
	// duckdb_columns() lists every table in every attached catalog; scope to
	// the default catalog for v0.1.
	std::string sql = "SELECT column_name, data_type, is_nullable, schema_name FROM duckdb_columns() WHERE ";
	if (!binding.schema.empty()) {
		sql += "schema_name = " + SqlLiteral(binding.schema);
	} else {
		// current schema (usually 'main')
		sql += "schema_name = current_schema()";
	}
	sql += " AND table_name = " + SqlLiteral(binding.table);
	sql += " AND NOT internal ORDER BY column_index";
	auto result = RunSql(con, sql);
	if (result->RowCount() == 0) {
		throw ODataParseException("table '" + binding.table + "' does not exist or is not readable");
	}

	// pick schema from the row unless the caller provided one
	entity.schema = binding.schema.empty() ? GetString(result, 0, 3) : binding.schema;

	std::vector<EdmProperty> props;
	for (idx_t r = 0; r < result->RowCount(); r++) {
		EdmProperty prop;
		prop.name = GetString(result, r, 0);
		prop.duckdb_type = GetString(result, r, 1);
		bool serializable = true;
		prop.edm_type = DuckDBTypeToEdm(prop.duckdb_type, serializable);
		prop.serializable = serializable;
		auto null_val = result->GetValue(2, r);
		prop.nullable = null_val.IsNull() || null_val.GetValue<bool>();
		props.push_back(std::move(prop));
	}
	if (props.empty()) {
		throw ODataParseException("table '" + binding.table + "' has no columns");
	}

	// Key resolution: configured key -> catalog single-column PK -> first col.
	std::vector<std::string> keys;
	if (!binding.configured_keys.empty()) {
		for (auto &k : binding.configured_keys) {
			bool found = false;
			for (auto &p : props) {
				if (ToLower(p.name) == ToLower(k)) {
					keys.push_back(p.name);
					found = true;
					break;
				}
			}
			if (!found) {
				throw ODataParseException("key column '" + k + "' does not exist on table '" + binding.table + "'");
			}
		}
	} else {
		// try catalog single-column primary key
		auto cons = RunSql(con,
		                   "SELECT constraint_column_names FROM duckdb_constraints() WHERE constraint_type = "
		                   "'PRIMARY KEY' AND table_name = " +
		                       SqlLiteral(binding.table));
		for (idx_t r = 0; r < cons->RowCount(); r++) {
			auto list = duckdb::ListValue::GetChildren(cons->GetValue(0, r));
			if (list.size() == 1) {
				std::string key_name = list[0].ToString();
				bool found = false;
				for (auto &p : props) {
					if (ToLower(p.name) == ToLower(key_name)) {
						keys.push_back(p.name);
						found = true;
						break;
					}
				}
				if (found) {
					break;
				}
			}
		}
		if (keys.empty()) {
			keys.push_back(props[0].name); // documented MVP fallback
		}
	}
	for (auto &p : props) {
		for (auto &k : keys) {
			if (ToLower(p.name) == ToLower(k)) {
				p.is_key = true;
			}
		}
		entity.properties.push_back(std::move(p));
	}
	entity.has_key = !keys.empty();
	return entity;
}

EdmModel MetadataGenerator::BuildModel(const std::vector<EntityBinding> &bindings) const {
	EdmModel model;
	for (auto &b : bindings) {
		model.entities.push_back(BuildEntity(b));
	}
	return model;
}

} // namespace duckdb_odata
