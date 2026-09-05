//===----------------------------------------------------------------------===//
// odata / execution / result serializer
//
// Serializes a DuckDB QueryResult as an OData JSON payload (design doc
// sections 32-33). v0.1 buffers the encoded document in memory before sending
// (bounded by odata_max_response_bytes); the encoder itself is row-incremental
// so a streaming transport can be dropped in later without rework.
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <string>

namespace duckdb {

class QueryResult;
class Value;

} // namespace duckdb

namespace duckdb_odata {

// Serialize a DuckDB value to a JSON fragment (e.g. "123", "\"abc\"").
std::string ValueToJson(const duckdb::Value &value);

// Serialize every row of the result as JSON objects separated by commas
// (no leading/trailing brackets). `rows_out` receives the number of rows.
std::string SerializeRows(duckdb::QueryResult &result, uint64_t &rows_out);

// Serialize only the first row as `"col":value, ...` (no braces). Returns
// false when the result contains no rows. Used for single-entity responses.
bool SerializeFirstRowMembers(duckdb::QueryResult &result, std::string &out);

} // namespace duckdb_odata
