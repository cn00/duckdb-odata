#include "execution/query_executor.hpp"

#include "duckdb.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/query_result.hpp"

namespace duckdb_odata {

std::unique_ptr<duckdb::QueryResult> LocalDuckDBExecutor::Execute(duckdb::Connection &con,
                                                                  const CompiledQuery &query) const {
	// v0.1 materializes results (streaming output staged for v0.2)
	return con.Query(query.sql);
}

} // namespace duckdb_odata
