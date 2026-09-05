#include "compiler/sql_compiler.hpp"

#include "common/string_util.hpp"
#include "compiler/expression_compiler.hpp"
#include "parser/odata_parser.hpp"

namespace duckdb_odata {

namespace {

std::string QuotedTable(const EdmEntity &entity) {
	// Prefer fully qualified name if we recorded schema; otherwise rely on
	// DuckDB search path (MVP).
	if (!entity.schema.empty()) {
		return QuoteIdentifier(entity.schema) + "." + QuoteIdentifier(entity.table);
	}
	return QuoteIdentifier(entity.table);
}

bool IsStringDuckType(const std::string &type) {
	auto base = ToLower(type);
	auto paren = base.find('(');
	if (paren != std::string::npos) {
		base = base.substr(0, paren);
	}
	base = Trim(base);
	return base == "varchar" || base == "text" || base == "string" || base == "char" || base == "bpchar" ||
	       base == "uuid" || base == "blob";
}

// Render a key literal against the DuckDB type of the key column.
std::string KeyLiteralSql(const EdmProperty &prop, const std::string &raw_value) {
	// strip surrounding quotes if the caller passed a quoted representation
	std::string value = raw_value;
	if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
		value = value.substr(1, value.size() - 2);
	}
	if (prop.edm_type == EdmType::STRING || prop.edm_type == EdmType::GUID || prop.edm_type == EdmType::BINARY) {
		return QuoteStringLiteral(value);
	}
	// numeric/temporal key: cast string form
	return "CAST(" + QuoteStringLiteral(value) + " AS " + prop.duckdb_type + ")";
}

} // namespace

std::string SqlCompiler::CompileSelectList(const ODataQuery &query, const EdmEntity &entity) const {
	// property lower -> canonical EdmProperty
	std::vector<const EdmProperty *> selected;
	if (query.select.empty()) {
		for (auto &prop : entity.properties) {
			selected.push_back(&prop);
		}
	} else {
		for (auto &name : query.select) {
			bool found = false;
			for (auto &prop : entity.properties) {
				if (ToLower(prop.name) == ToLower(name)) {
					selected.push_back(&prop);
					found = true;
					break;
				}
			}
			if (!found) {
				throw ODataParseException("$select property '" + name + "' does not exist on entity '" +
				                          entity.name + "'");
			}
		}
	}
	std::string sql;
	for (size_t i = 0; i < selected.size(); i++) {
		if (i > 0) {
			sql += ", ";
		}
		sql += QuoteIdentifier(selected[i]->name);
	}
	if (selected.empty()) {
		sql = "*";
	}
	return sql;
}

std::string SqlCompiler::CompileFilterText(const ODataQuery &query, const EdmEntity &entity) const {
	if (!query.filter) {
		return "";
	}
	ExpressionCompiler compiler(entity);
	return compiler.Compile(*query.filter);
}

CompiledQuery SqlCompiler::CompileCollection(const ODataQuery &query, const EdmEntity &entity, int64_t max_top) const {
	std::string select_list = CompileSelectList(query, entity);

	std::string sql = "SELECT " + select_list + " FROM " + QuotedTable(entity);
	std::string where = CompileFilterText(query, entity);
	if (!where.empty()) {
		sql += " WHERE " + where;
	}

	// $orderby
	if (!query.order_by.empty()) {
		std::string order;
		for (auto &term : query.order_by) {
			bool found = false;
			for (auto &prop : entity.properties) {
				if (ToLower(prop.name) == ToLower(term.property)) {
					found = true;
					break;
				}
			}
			if (!found) {
				throw ODataParseException("$orderby property '" + term.property + "' does not exist on entity '" +
				                          entity.name + "'");
			}
			if (!order.empty()) {
				order += ", ";
			}
			order += QuoteIdentifier(term.property);
			order += term.order == SortOrder::DESC ? " DESC" : " ASC";
		}
		sql += " ORDER BY " + order;
	}

	// $skip / $top (cap $top with server side limit)
	int64_t top = -1;
	if (query.top >= 0) {
		top = query.top;
		if (max_top >= 0 && top > max_top) {
			top = max_top;
		}
	}
	if (top >= 0) {
		sql += " LIMIT " + std::to_string(top);
	}
	if (query.skip >= 0) {
		sql += " OFFSET " + std::to_string(query.skip);
	}

	CompiledQuery result;
	result.sql = sql;
	return result;
}

CompiledQuery SqlCompiler::CompileCount(const ODataQuery &query, const EdmEntity &entity) const {
	std::string sql = "SELECT COUNT(*) AS __odata_count FROM " + QuotedTable(entity);
	std::string where = CompileFilterText(query, entity);
	if (!where.empty()) {
		sql += " WHERE " + where;
	}
	CompiledQuery result;
	result.sql = sql;
	result.is_count = true;
	return result;
}

CompiledQuery SqlCompiler::CompileByKey(const ODataQuery &query, const EdmEntity &entity,
                                        const std::vector<std::string> &key_values) const {
	std::vector<const EdmProperty *> keys;
	for (auto &prop : entity.properties) {
		if (prop.is_key) {
			keys.push_back(&prop);
		}
	}
	if (keys.empty()) {
		throw ODataParseException("entity '" + entity.name + "' has no key configured");
	}
	if (key_values.size() != keys.size()) {
		throw ODataParseException("key lookup expects " + std::to_string(keys.size()) + " key value(s), got " +
		                          std::to_string(key_values.size()));
	}
	std::string select_list = CompileSelectList(query, entity);
	std::string sql = "SELECT " + select_list + " FROM " + QuotedTable(entity) + " WHERE ";
	std::string where;
	for (size_t i = 0; i < keys.size(); i++) {
		if (i > 0) {
			where += " AND ";
		}
		where += QuoteIdentifier(keys[i]->name) + " = " + KeyLiteralSql(*keys[i], key_values[i]);
	}
	sql += where;
	// OData: key lookup returns at most one entity
	sql += " LIMIT 1";
	CompiledQuery result;
	result.sql = sql;
	return result;
}

} // namespace duckdb_odata
