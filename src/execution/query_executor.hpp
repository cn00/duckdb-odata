//===----------------------------------------------------------------------===//
// odata / execution / query executor
//
// Execution backend abstraction (design doc section 31). v0.1 ships the local
// DuckDB executor; a QuackExecutor slots in behind the same interface in a
// later version.
//===----------------------------------------------------------------------===//
#pragma once

#include "compiler/sql_compiler.hpp"

#include <memory>

namespace duckdb {

class Connection;
class QueryResult;
class DatabaseInstance;

} // namespace duckdb

namespace duckdb_odata {

class QueryExecutor {
public:
	virtual ~QueryExecutor() = default;

	// Execute a compiled query against the given connection. The returned
	// QueryResult owns its data independent of further calls.
	virtual std::unique_ptr<duckdb::QueryResult> Execute(duckdb::Connection &con,
	                                                     const CompiledQuery &query) const = 0;
};

// Local execution: runs the SQL directly on the DuckDB connection.
class LocalDuckDBExecutor : public QueryExecutor {
public:
	std::unique_ptr<duckdb::QueryResult> Execute(duckdb::Connection &con,
	                                             const CompiledQuery &query) const override;
};

} // namespace duckdb_odata
