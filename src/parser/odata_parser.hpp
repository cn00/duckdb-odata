//===----------------------------------------------------------------------===//
// odata / parser
//
// Recursive-descent parser turning a $filter string into an AST
// (design doc sections 8-10, 17) and the full URL query string into an
// ODataQuery. The parser is pure C++ (no DuckDB dependency) so it can be
// unit tested standalone.
//===----------------------------------------------------------------------===//
#pragma once

#include "parser/ast.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace duckdb_odata {

// Thrown for any OData syntax / semantic error that should surface as a
// 400 Bad Request with a readable message.
class ODataParseException : public std::runtime_error {
public:
	explicit ODataParseException(const std::string &msg) : std::runtime_error(msg) {
	}
};

// Parse a $filter expression into an AST. Throws ODataParseException.
std::unique_ptr<Expr> ParseFilter(const std::string &filter);

// Parse the comma-separated $select list.
std::vector<std::string> ParseSelect(const std::string &select);

// Parse the comma-separated $orderby list ("prop [asc|desc], ...").
std::vector<OrderByTerm> ParseOrderBy(const std::string &orderby);

// Parse a non-negative integer option such as $top / $skip.
int64_t ParseNonNegativeInt(const std::string &name, const std::string &value);

// Parse a boolean option such as $count.
bool ParseBooleanOption(const std::string &name, const std::string &value);

// Parse an OData single-key literal. Returns the raw literal text and its
// kind (used for key lookups like /odata/customers(1)).
struct KeyLiteral {
	LiteralKind kind = LiteralKind::STRING;
	std::string str;
	int64_t integer = 0;
	double real = 0;
};
KeyLiteral ParseKeyLiteral(const std::string &text);

} // namespace duckdb_odata
