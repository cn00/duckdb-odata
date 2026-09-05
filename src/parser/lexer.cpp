#include "parser/lexer.hpp"

#include <cctype>
#include <stdexcept>

namespace duckdb_odata {

std::string Token::ToString() const {
	return text;
}

std::string EscapeODataString(const std::string &input) {
	return input;
}

static TokenType KeywordType(const std::string &lower) {
	if (lower == "eq") {
		return TokenType::EQ;
	}
	if (lower == "ne") {
		return TokenType::NE;
	}
	if (lower == "gt") {
		return TokenType::GT;
	}
	if (lower == "ge") {
		return TokenType::GE;
	}
	if (lower == "lt") {
		return TokenType::LT;
	}
	if (lower == "le") {
		return TokenType::LE;
	}
	if (lower == "and") {
		return TokenType::AND;
	}
	if (lower == "or") {
		return TokenType::OR;
	}
	if (lower == "not") {
		return TokenType::NOT;
	}
	if (lower == "true" || lower == "false") {
		return TokenType::BOOLEAN;
	}
	if (lower == "null") {
		return TokenType::NULL_VAL;
	}
	return TokenType::IDENTIFIER;
}

static bool IsIdentStart(char c) {
	return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool IsIdentChar(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '/';
}

std::vector<Token> Tokenize(const std::string &input) {
	std::vector<Token> tokens;
	size_t i = 0;
	size_t line = 1;
	while (i < input.size()) {
		char c = input[i];
		if (c == ' ' || c == '\t' || c == '\r') {
			i++;
			continue;
		}
		if (c == '\n') {
			line++;
			i++;
			continue;
		}
		Token token;
		token.line = static_cast<int>(line);

		// string literal: '...'  ('' escapes a quote)
		if (c == '\'') {
			size_t start = i;
			i++;
			std::string value;
			bool closed = false;
			while (i < input.size()) {
				if (input[i] == '\'') {
					if (i + 1 < input.size() && input[i + 1] == '\'') {
						value += '\'';
						i += 2;
						continue;
					}
					i++;
					closed = true;
					break;
				}
				value += input[i];
				i++;
			}
			if (!closed) {
				throw std::runtime_error("Unterminated string literal in $filter");
			}
			token.type = TokenType::STRING;
			token.text = value;
			tokens.push_back(token);
			(void)start;
			continue;
		}

		// number literal
		if (std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && i + 1 < input.size() &&
		                                                   std::isdigit(static_cast<unsigned char>(input[i + 1])))) {
			size_t start = i;
			if (c == '-') {
				i++;
			}
			bool is_float = false;
			while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
				i++;
			}
			if (i < input.size() && input[i] == '.') {
				is_float = true;
				i++;
				while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
					i++;
				}
			}
			if (i < input.size() && (input[i] == 'e' || input[i] == 'E')) {
				is_float = true;
				i++;
				if (i < input.size() && (input[i] == '+' || input[i] == '-')) {
					i++;
				}
				while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
					i++;
				}
			}
			std::string text = input.substr(start, i - start);
			token.text = text;
			if (is_float) {
				token.type = TokenType::FLOAT;
				token.float_value = std::stod(text);
			} else {
				token.type = TokenType::INTEGER;
				token.int_value = std::stoll(text);
			}
			tokens.push_back(token);
			continue;
		}

		// identifier / keyword
		if (IsIdentStart(c)) {
			size_t start = i;
			while (i < input.size() && IsIdentChar(input[i])) {
				i++;
			}
			std::string text = input.substr(start, i - start);
			std::string lower;
			for (auto ch : text) {
				lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			TokenType kt = KeywordType(lower);
			if (kt == TokenType::BOOLEAN) {
				token.type = kt;
				token.text = text;
			} else if (kt == TokenType::NULL_VAL) {
				token.type = kt;
				token.text = "null";
			} else if (kt != TokenType::IDENTIFIER) {
				token.type = kt;
				token.text = lower;
			} else {
				token.type = TokenType::IDENTIFIER;
				token.text = text;
			}
			tokens.push_back(token);
			continue;
		}

		switch (c) {
		case '(':
			token.type = TokenType::LPAREN;
			token.text = "(";
			break;
		case ')':
			token.type = TokenType::RPAREN;
			token.text = ")";
			break;
		case ',':
			token.type = TokenType::COMMA;
			token.text = ",";
			break;
		case ':':
			token.type = TokenType::COLON;
			token.text = ":";
			break;
		default:
			throw std::runtime_error(std::string("Unexpected character '") + c + "' in $filter");
		}
		tokens.push_back(token);
		i++;
	}
	Token end;
	end.type = TokenType::END;
	tokens.push_back(end);
	return tokens;
}

} // namespace duckdb_odata
