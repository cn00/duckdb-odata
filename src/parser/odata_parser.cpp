#include "parser/odata_parser.hpp"

#include "common/string_util.hpp"
#include "parser/lexer.hpp"

namespace duckdb_odata {

namespace {

// The OData "common expression" grammar is a small precedence grammar:
//
//   orExpr  := andExpr ( 'or' andExpr )*
//   andExpr := notExpr ( 'and' notExpr )*
//   notExpr := 'not' notExpr | comparison
//   comparison := addExpr ( (eq|ne|gt|ge|lt|le) addExpr )?
//   addExpr := primary ( function? )  // only comparison-level needed for v0.1
//   primary := literal | property | functionCall | '(' expr ')'
//
// For v0.1 the design doc restricts $filter to comparison + boolean
// operators and parentheses; function calls (contains, startswith, ...) are
// staged for v0.2 but the parser already understands generic function calls
// so the AST stays stable.

class FilterParser {
public:
	explicit FilterParser(const std::vector<Token> &tokens_p) : tokens(tokens_p), pos(0) {
	}

	std::unique_ptr<Expr> Parse() {
		if (Peek().type == TokenType::END) {
			throw ODataParseException("empty $filter expression");
		}
		auto expr = ParseOr();
		if (Peek().type != TokenType::END) {
			throw ODataParseException("unexpected token '" + Peek().text + "' in $filter");
		}
		return expr;
	}

private:
	const std::vector<Token> &tokens;
	size_t pos;

	const Token &Peek() const {
		return tokens[pos];
	}
	const Token &Next() {
		return tokens[pos++];
	}
	bool Match(TokenType type) {
		if (Peek().type == type) {
			pos++;
			return true;
		}
		return false;
	}
	void Expect(TokenType type, const char *what) {
		if (Peek().type != type) {
			throw ODataParseException(std::string("expected ") + what + " but found '" + Peek().text + "'");
		}
		pos++;
	}

	std::unique_ptr<Expr> ParseOr() {
		auto left = ParseAnd();
		while (Match(TokenType::OR)) {
			auto right = ParseAnd();
			auto bin = std::unique_ptr<BinaryExpr>(new BinaryExpr());
			bin->op = BinaryOp::OR;
			bin->left = std::move(left);
			bin->right = std::move(right);
			left = std::move(bin);
		}
		return left;
	}

	std::unique_ptr<Expr> ParseAnd() {
		auto left = ParseNot();
		while (Match(TokenType::AND)) {
			auto right = ParseNot();
			auto bin = std::unique_ptr<BinaryExpr>(new BinaryExpr());
			bin->op = BinaryOp::AND;
			bin->left = std::move(left);
			bin->right = std::move(right);
			left = std::move(bin);
		}
		return left;
	}

	std::unique_ptr<Expr> ParseNot() {
		if (Match(TokenType::NOT)) {
			auto unary = std::unique_ptr<UnaryExpr>(new UnaryExpr());
			unary->op = UnaryOp::NOT;
			unary->child = ParseNot();
			return unary;
		}
		return ParseComparison();
	}

	std::unique_ptr<Expr> ParseComparison() {
		auto left = ParsePrimary();
		TokenType t = Peek().type;
		BinaryOp op;
		switch (t) {
		case TokenType::EQ:
			op = BinaryOp::EQ;
			break;
		case TokenType::NE:
			op = BinaryOp::NE;
			break;
		case TokenType::GT:
			op = BinaryOp::GT;
			break;
		case TokenType::GE:
			op = BinaryOp::GE;
			break;
		case TokenType::LT:
			op = BinaryOp::LT;
			break;
		case TokenType::LE:
			op = BinaryOp::LE;
			break;
		default:
			return left; // no comparison: bare boolean-ish expression
		}
		pos++; // consume operator
		auto right = ParsePrimary();
		auto bin = std::unique_ptr<BinaryExpr>(new BinaryExpr());
		bin->op = op;
		bin->left = std::move(left);
		bin->right = std::move(right);
		return bin;
	}

	std::unique_ptr<Expr> ParsePrimary() {
		const Token &tok = Peek();
		switch (tok.type) {
		case TokenType::LPAREN: {
			pos++;
			auto expr = ParseOr();
			Expect(TokenType::RPAREN, "')'");
			return expr;
		}
		case TokenType::INTEGER: {
			pos++;
			auto lit = std::unique_ptr<LiteralExpr>(new LiteralExpr());
			lit->literal_kind = LiteralKind::INTEGER;
			lit->integer = tok.int_value;
			return lit;
		}
		case TokenType::FLOAT: {
			pos++;
			auto lit = std::unique_ptr<LiteralExpr>(new LiteralExpr());
			lit->literal_kind = LiteralKind::FLOAT;
			lit->real = tok.float_value;
			return lit;
		}
		case TokenType::STRING: {
			pos++;
			auto lit = std::unique_ptr<LiteralExpr>(new LiteralExpr());
			lit->literal_kind = LiteralKind::STRING;
			lit->str = tok.text;
			return lit;
		}
		case TokenType::BOOLEAN: {
			pos++;
			auto lit = std::unique_ptr<LiteralExpr>(new LiteralExpr());
			lit->literal_kind = LiteralKind::BOOLEAN;
			lit->boolean = ToLower(tok.text) == "true";
			return lit;
		}
		case TokenType::NULL_VAL: {
			pos++;
			auto lit = std::unique_ptr<LiteralExpr>(new LiteralExpr());
			lit->literal_kind = LiteralKind::NULL_VALUE;
			return lit;
		}
		case TokenType::IDENTIFIER: {
			pos++;
			// function call?
			if (Match(TokenType::LPAREN)) {
				auto call = std::unique_ptr<FunctionCallExpr>(new FunctionCallExpr());
				call->name = tok.text;
				if (!Match(TokenType::RPAREN)) {
					while (true) {
						call->args.push_back(ParseOr());
						if (Match(TokenType::RPAREN)) {
							break;
						}
						Expect(TokenType::COMMA, "','");
					}
				}
				return call;
			}
			auto prop = std::unique_ptr<PropertyExpr>(new PropertyExpr());
			prop->name = tok.text;
			return prop;
		}
		default:
			throw ODataParseException("unexpected token '" + tok.text + "' in $filter");
		}
	}
};

} // namespace

std::unique_ptr<Expr> ParseFilter(const std::string &filter) {
	auto tokens = Tokenize(filter);
	FilterParser parser(tokens);
	return parser.Parse();
}

std::vector<std::string> ParseSelect(const std::string &select) {
	std::vector<std::string> result;
	auto parts = select.empty() ? std::vector<std::string>() : std::vector<std::string>{""};
	if (!select.empty()) {
		parts.clear();
		size_t start = 0;
		while (true) {
			auto comma = select.find(',', start);
			parts.push_back(select.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
			if (comma == std::string::npos) {
				break;
			}
			start = comma + 1;
		}
	}
	for (auto &part : parts) {
		auto trimmed = Trim(part);
		if (!trimmed.empty()) {
			result.push_back(trimmed);
		}
	}
	return result;
}

std::vector<OrderByTerm> ParseOrderBy(const std::string &orderby) {
	std::vector<OrderByTerm> result;
	if (orderby.empty()) {
		return result;
	}
	size_t start = 0;
	while (true) {
		auto comma = orderby.find(',', start);
		std::string term = orderby.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
		term = Trim(term);
		if (term.empty()) {
			throw ODataParseException("empty term in $orderby");
		}
		OrderByTerm ob;
		auto space = term.find(' ');
		if (space == std::string::npos) {
			ob.property = term;
		} else {
			ob.property = term.substr(0, space);
			std::string dir = ToLower(Trim(term.substr(space + 1)));
			if (dir == "asc") {
				ob.order = SortOrder::ASC;
			} else if (dir == "desc") {
				ob.order = SortOrder::DESC;
			} else {
				throw ODataParseException("invalid sort direction '" + dir + "' in $orderby");
			}
		}
		result.push_back(std::move(ob));
		if (comma == std::string::npos) {
			break;
		}
		start = comma + 1;
	}
	return result;
}

int64_t ParseNonNegativeInt(const std::string &name, const std::string &value) {
	if (value.empty()) {
		throw ODataParseException("empty value for $" + name);
	}
	int64_t parsed = 0;
	try {
		size_t idx = 0;
		parsed = std::stoll(value, &idx, 10);
		if (idx != value.size()) {
			throw ODataParseException("invalid value for $" + name + ": '" + value + "'");
		}
	} catch (const std::invalid_argument &) {
		throw ODataParseException("invalid value for $" + name + ": '" + value + "'");
	} catch (const std::out_of_range &) {
		throw ODataParseException("value out of range for $" + name + ": '" + value + "'");
	}
	if (parsed < 0) {
		throw ODataParseException("$" + name + " must not be negative");
	}
	return parsed;
}

bool ParseBooleanOption(const std::string &name, const std::string &value) {
	auto lower = ToLower(value);
	if (lower == "true" || lower == "1") {
		return true;
	}
	if (lower == "false" || lower == "0") {
		return false;
	}
	throw ODataParseException("invalid boolean value for $" + name + ": '" + value + "'");
}

KeyLiteral ParseKeyLiteral(const std::string &text) {
	KeyLiteral result;
	auto trimmed = Trim(text);
	if (trimmed.size() >= 2 && trimmed.front() == '\'' && trimmed.back() == '\'') {
		result.kind = LiteralKind::STRING;
		result.str = trimmed.substr(1, trimmed.size() - 2);
		// undo '' escaping
		std::string unescaped;
		for (size_t i = 0; i < result.str.size(); i++) {
			if (result.str[i] == '\'' && i + 1 < result.str.size() && result.str[i + 1] == '\'') {
				unescaped += '\'';
				i++;
			} else {
				unescaped += result.str[i];
			}
		}
		result.str = unescaped;
		return result;
	}
	auto lower = ToLower(trimmed);
	if (lower == "true" || lower == "false") {
		result.kind = LiteralKind::BOOLEAN;
		result.str = lower == "true" ? "true" : "false";
		return result;
	}
	// numeric?
	try {
		size_t idx = 0;
		int64_t v = std::stoll(trimmed, &idx, 10);
		if (idx == trimmed.size()) {
			result.kind = LiteralKind::INTEGER;
			result.integer = v;
			result.str = trimmed;
			return result;
		}
	} catch (...) {
	}
	// default: treat as string literal (unquoted)
	result.kind = LiteralKind::STRING;
	result.str = trimmed;
	return result;
}

} // namespace duckdb_odata
