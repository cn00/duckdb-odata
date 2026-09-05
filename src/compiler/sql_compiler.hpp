//===----------------------------------------------------------------------===//
// odata / compiler / sql compiler
//
// Compiles an ODataQuery (+ entity model) into a full DuckDB SQL string.
// Nothing from the URL ever reaches SQL unescaped: identifiers are validated
// against the entity model and emitted via QuoteIdentifier, literals via the
// expression compiler. (design doc sections 11, 12, 19, 20, 21)
//===----------------------------------------------------------------------===//
#pragma once

#include "metadata/edm_model.hpp"
#include "parser/ast.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_odata {

struct CompiledQuery {
	std::string sql;
	bool is_count = false;
};

class SqlCompiler {
public:
	// Compile a collection query (SELECT with optional WHERE/ORDER BY/LIMIT).
	// max_top: server-side cap for $top; -1 disables the cap.
	CompiledQuery CompileCollection(const ODataQuery &query, const EdmEntity &entity, int64_t max_top) const;
	// Compile a COUNT(*) query honoring only the filter.
	CompiledQuery CompileCount(const ODataQuery &query, const EdmEntity &entity) const;
	// Compile a single-entity lookup by key value(s).
	CompiledQuery CompileByKey(const ODataQuery &query, const EdmEntity &entity,
	                           const std::vector<std::string> &key_values) const;

private:
	std::string CompileSelectList(const ODataQuery &query, const EdmEntity &entity) const;
	std::string CompileFilterText(const ODataQuery &query, const EdmEntity &entity) const;
};

} // namespace duckdb_odata
