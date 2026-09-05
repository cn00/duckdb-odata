#include "execution/result_serializer.hpp"

#include "common/string_util.hpp"

#include "duckdb.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/query_result.hpp"

#include <cmath>

namespace duckdb_odata {

using duckdb::Value;

namespace {

std::string FormatDouble(double d) {
	if (std::isnan(d) || std::isinf(d)) {
		return "null";
	}
	std::string s = std::to_string(d);
	// trim trailing zeros, keep at least one fraction digit
	auto dot = s.find('.');
	if (dot != std::string::npos) {
		auto end = s.size();
		while (end > dot + 1 && s[end - 1] == '0') {
			end--;
		}
		if (end == dot + 1) {
			end++; // keep ".0"
		}
		s = s.substr(0, end);
	}
	return s;
}

std::string Base64Encode(const char *data, size_t len) {
	static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve(((len + 2) / 3) * 4);
	size_t i = 0;
	while (i + 3 <= len) {
		unsigned n = (static_cast<unsigned char>(data[i]) << 16) |
		             (static_cast<unsigned char>(data[i + 1]) << 8) | static_cast<unsigned char>(data[i + 2]);
		out += tbl[(n >> 18) & 63];
		out += tbl[(n >> 12) & 63];
		out += tbl[(n >> 6) & 63];
		out += tbl[n & 63];
		i += 3;
	}
	if (i + 1 == len) {
		unsigned n = static_cast<unsigned char>(data[i]) << 16;
		out += tbl[(n >> 18) & 63];
		out += tbl[(n >> 12) & 63];
		out += "==";
	} else if (i + 2 == len) {
		unsigned n = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8);
		out += tbl[(n >> 18) & 63];
		out += tbl[(n >> 12) & 63];
		out += tbl[(n >> 6) & 63];
		out += "=";
	}
	return out;
}

} // namespace

std::string ValueToJson(const Value &value) {
	if (value.IsNull()) {
		return "null";
	}
	switch (value.type().id()) {
	case duckdb::LogicalTypeId::BOOLEAN:
		return value.GetValue<bool>() ? "true" : "false";
	case duckdb::LogicalTypeId::TINYINT:
	case duckdb::LogicalTypeId::SMALLINT:
	case duckdb::LogicalTypeId::INTEGER:
	case duckdb::LogicalTypeId::BIGINT:
	case duckdb::LogicalTypeId::UTINYINT:
	case duckdb::LogicalTypeId::USMALLINT:
	case duckdb::LogicalTypeId::UINTEGER:
	case duckdb::LogicalTypeId::UBIGINT:
		return value.ToString();
	case duckdb::LogicalTypeId::FLOAT:
	case duckdb::LogicalTypeId::DOUBLE:
		return FormatDouble(value.GetValue<double>());
	case duckdb::LogicalTypeId::HUGEINT:
	case duckdb::LogicalTypeId::UHUGEINT:
	case duckdb::LogicalTypeId::DECIMAL:
		return value.ToString();
	case duckdb::LogicalTypeId::VARCHAR:
	case duckdb::LogicalTypeId::UUID:
	case duckdb::LogicalTypeId::DATE:
	case duckdb::LogicalTypeId::TIME:
	case duckdb::LogicalTypeId::TIMESTAMP:
	case duckdb::LogicalTypeId::TIMESTAMP_SEC:
	case duckdb::LogicalTypeId::TIMESTAMP_MS:
	case duckdb::LogicalTypeId::TIMESTAMP_NS:
	case duckdb::LogicalTypeId::TIMESTAMP_TZ:
		return "\"" + JsonEscape(value.ToString()) + "\"";
	case duckdb::LogicalTypeId::BLOB: {
		auto blob = value.GetValue<std::string>();
		return "\"" + Base64Encode(blob.data(), blob.size()) + "\"";
	}
	default:
		// fallback: string-ify so the payload stays valid JSON
		return "\"" + JsonEscape(value.ToString()) + "\"";
	}
}

std::string SerializeRows(duckdb::QueryResult &result, uint64_t &rows_out) {
	std::string body;
	rows_out = 0;
	while (true) {
		auto chunk = result.Fetch();
		if (!chunk) {
			break;
		}
		idx_t n = chunk->size();
		for (idx_t r = 0; r < n; r++) {
			if (rows_out > 0) {
				body += ",";
			}
			body += "{";
			for (idx_t c = 0; c < chunk->ColumnCount(); c++) {
				if (c > 0) {
					body += ",";
				}
				body += "\"" + JsonEscape(result.names[c]) + "\":";
				Value v = chunk->GetValue(c, r);
				body += ValueToJson(v);
			}
			body += "}";
			rows_out++;
		}
	}
	return body;
}

bool SerializeFirstRowMembers(duckdb::QueryResult &result, std::string &out) {
	auto chunk = result.Fetch();
	if (!chunk || chunk->size() == 0) {
		return false;
	}
	idx_t r = 0;
	for (idx_t c = 0; c < chunk->ColumnCount(); c++) {
		if (c > 0) {
			out += ",";
		}
		out += "\"" + JsonEscape(result.names[c]) + "\":";
		Value v = chunk->GetValue(c, r);
		out += ValueToJson(v);
	}
	return true;
}

} // namespace duckdb_odata
