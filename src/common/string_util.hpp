//===----------------------------------------------------------------------===//
// Small string / URL / escaping utilities shared across the odata extension.
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace duckdb_odata {

// Quote a DuckDB identifier with double quotes, doubling embedded quotes.
// This is the ONLY way identifiers from user input reach SQL text.
std::string QuoteIdentifier(const std::string &name);

// Quote a SQL string literal with single quotes, doubling embedded quotes.
std::string QuoteStringLiteral(const std::string &value);

// JSON-escape a string (for use inside a double-quoted JSON string).
std::string JsonEscape(const std::string &value);

// Percent-decode a URL-encoded string (used for query strings and path segs).
std::string UrlDecode(const std::string &input);

// Parse a URL query string into key/value pairs ($foo=bar&baz=qux). Values
// are percent-decoded. Repeated keys are kept in order.
std::vector<std::pair<std::string, std::string>> ParseQueryString(const std::string &query);

// Lowercase a string (ASCII only, sufficient for option/property names).
std::string ToLower(const std::string &input);

// Trim ASCII whitespace from both ends.
std::string Trim(const std::string &input);

// True when `name` starts with `prefix` (case-sensitive).
bool StartsWith(const std::string &name, const std::string &prefix);

} // namespace duckdb_odata
