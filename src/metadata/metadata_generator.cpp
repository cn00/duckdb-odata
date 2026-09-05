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

} // namespace

EntityBinding ParseQualifiedBinding(const std::string &qualified_name) {
	EntityBinding binding;
	std::string input = Trim(qualified_name);
	if (input.empty()) {
		throw ODataParseException("empty entity identifier");
	}
	// split on '.': table | schema.table
	std::vector<std::string> parts;
	size_t start = 0;
	while (true) {
		auto dot = input.find('.', start);
		parts.push_back(input.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
		if (dot == std::string::npos) {
			break;
		}
		start = dot + 1;
	}
	for (auto &p : parts) {
		if (p.empty()) {
			throw ODataParseException("invalid qualified identifier '" + qualified_name + "'");
		}
	}
	if (parts.size() > 2) {
		throw ODataParseException("catalog-qualified identifiers are not supported yet: '" + qualified_name + "'");
	}
	if (parts.size() == 2) {
		binding.schema = parts[0];
		binding.table = parts[1];
		binding.name = binding.schema + "_" + binding.table;
	} else {
		binding.table = parts[0];
		binding.name = binding.table;
	}
	return binding;
}

void ResolveBindingTable(duckdb::Connection &con, EntityBinding &binding) {
	std::string sql = "SELECT database_name, schema_name FROM duckdb_columns() WHERE table_name = " +
	                  SqlLiteral(binding.table) + " AND NOT internal";
	if (!binding.schema.empty()) {
		sql += " AND schema_name = " + SqlLiteral(binding.schema);
	}
	if (!binding.catalog.empty()) {
		sql += " AND database_name = " + SqlLiteral(binding.catalog);
	}
	sql += " GROUP BY database_name, schema_name";
	auto result = RunSql(con, sql);

	if (binding.schema.empty() && binding.catalog.empty()) {
		// unqualified: adopt when unambiguous, otherwise require qualification
		if (result->RowCount() == 0) {
			throw ODataParseException("table '" + binding.table + "' does not exist");
		}
		if (result->RowCount() > 1) {
			throw ODataParseException("table '" + binding.table + "' exists in multiple schemas; qualify it, e.g. " +
			                          "odata_expose('schema.table')");
		}
		binding.catalog = GetString(result, 0, 0);
		binding.schema = GetString(result, 0, 1);
		return;
	}
	// qualified: the table must exist exactly where requested
	if (result->RowCount() == 0) {
		throw ODataParseException("table '" + binding.table + "' does not exist in schema '" +
		                          (binding.schema.empty() ? "<current>" : binding.schema) + "'");
	}
	binding.catalog = GetString(result, 0, 0);
	binding.schema = GetString(result, 0, 1);
}

EdmEntity MetadataGenerator::BuildEntity(const EntityBinding &binding) const {
	EdmEntity entity;
	entity.name = binding.name;
	entity.table = binding.table;
	entity.schema = binding.schema;
	entity.catalog = binding.catalog;

	// Scope to a single catalog: duckdb_columns() spans every attached
	// database, so unqualified schema/table filters would match any catalog.
	std::string catalog_scope = binding.catalog.empty() ? "current_catalog()" : SqlLiteral(binding.catalog);
	std::string schema_scope = binding.schema.empty() ? "current_schema()" : SqlLiteral(binding.schema);

	std::string sql = "SELECT column_name, data_type, is_nullable, database_name, schema_name "
	                  "FROM duckdb_columns() WHERE ";
	sql += "database_name = " + catalog_scope;
	sql += " AND schema_name = " + schema_scope;
	sql += " AND table_name = " + SqlLiteral(binding.table);
	sql += " AND NOT internal ORDER BY column_index";
	auto result = RunSql(con, sql);
	if (result->RowCount() == 0) {
		std::string hint;
		if (binding.schema.empty()) {
			hint = "; table may live in another schema - qualify it, e.g. odata_expose('schema.table')";
		}
		throw ODataParseException("table '" + binding.table + "' does not exist or is not readable in schema '" +
		                          (binding.schema.empty() ? "<current>" : binding.schema) + "'" + hint);
	}

	// Record the concrete catalog/schema of the first row
	entity.catalog = GetString(result, 0, 3);
	entity.schema = GetString(result, 0, 4);
	entity.table = binding.table;

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
		// try catalog single-column primary key (scoped to this table's
		// catalog/schema: duckdb_constraints() spans all attached databases)
		std::string cons_sql = "SELECT constraint_column_names FROM duckdb_constraints() WHERE constraint_type = "
		                       "'PRIMARY KEY' AND table_name = " +
		                       SqlLiteral(binding.table);
		cons_sql += " AND database_name = " + (binding.catalog.empty() ? "current_catalog()" : SqlLiteral(binding.catalog));
		cons_sql += " AND schema_name = " + (binding.schema.empty() ? "current_schema()" : SqlLiteral(binding.schema));
		auto cons = RunSql(con, cons_sql);
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
