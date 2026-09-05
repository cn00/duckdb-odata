//===----------------------------------------------------------------------===//
// odata / parser / errors
//
// Exception used across the parser layer (lexer + parser + semantic checks).
// It is thrown for any OData syntax / semantic error that should surface as
// a 400 Bad Request with a readable message. Kept in its own header so the
// lexer can throw it without a dependency cycle.
//===----------------------------------------------------------------------===//
#pragma once

#include <stdexcept>
#include <string>

namespace duckdb_odata {

class ODataParseException : public std::runtime_error {
public:
	explicit ODataParseException(const std::string &msg) : std::runtime_error(msg) {
	}
};

} // namespace duckdb_odata
