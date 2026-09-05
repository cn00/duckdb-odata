//===----------------------------------------------------------------------===//
// odata / lexer
//
// Tokenizer for OData $filter expressions. Design doc section 8/9.
//===----------------------------------------------------------------------===//
#pragma once

#include "parser/errors.hpp"

#include <string>
#include <vector>

namespace duckdb_odata {

enum class TokenType {
	// literal / identifier
	IDENTIFIER, // bare name like age, status
	STRING,     // 'active' (quotes stripped)
	INTEGER,    // 18
	FLOAT,      // 1.5 / 2e3
	DATE,       // 2026-01-01 (bare or in quotes)
	TIMESTAMP,  // 2026-01-01T10:20:30 (bare or quoted)
	GUID,       // guid'...' / '0000...'
	BOOLEAN,    // true / false
	NULL_VAL,   // null

	// operators
	EQ, // eq
	NE, // ne
	GT, // gt
	GE, // ge
	LT, // lt
	LE, // le
	AND,
	OR,
	NOT,

	// punctuation
	LPAREN,
	RPAREN,
	COMMA,
	COLON,

	END
};

struct Token {
	TokenType type;
	std::string text;   // raw lexeme (decoded)
	int64_t int_value = 0;  // for INTEGER
	double float_value = 0; // for FLOAT
	int line = 1;

	std::string ToString() const;
};

// Tokenize an OData filter expression. Throws std::runtime_error on
// lexical errors.
std::vector<Token> Tokenize(const std::string &input);

// Escape table for the common OData string functions that map 1:1 onto
// DuckDB scalar functions (used by the compiler, not the lexer).
std::string EscapeODataString(const std::string &input);

} // namespace duckdb_odata
