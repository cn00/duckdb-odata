#include "compiler/expression_compiler.hpp"

#include "common/string_util.hpp"
#include "parser/odata_parser.hpp"

namespace duckdb_odata {

namespace {

struct FunctionMapEntry {
	const char *odata_name;
	const char *duckdb_name;
	int min_args;
	int max_args;
};

// OData string / temporal functions and their DuckDB counterparts.
// (v0.2 scope per design doc section 18, but supported cheaply; unknown
// function names are rejected so no user text reaches SQL.)
const std::vector<FunctionMapEntry> &GetFunctionMap() {
	static const std::vector<FunctionMapEntry> map = {
	    {"contains", "contains", 2, 2},   {"startswith", "starts_with", 2, 2},
	    {"endswith", "ends_with", 2, 2},  {"tolower", "lower", 1, 1},
	    {"toupper", "upper", 1, 1},       {"length", "length", 1, 1},
	    {"substring", "substring", 2, 3}, {"concat", "concat", 2, 2},
	    {"indexof", "strpos", 2, 2},      {"trim", "trim", 1, 1},
	    {"year", "year", 1, 1},           {"month", "month", 1, 1},
	    {"day", "day", 1, 1},             {"hour", "hour", 1, 1},
	    {"minute", "minute", 1, 1},       {"second", "second", 1, 1},
	    {"now", "now", 0, 0},             {"round", "round", 2, 2},
	    {"floor", "floor", 1, 1},         {"ceiling", "ceil", 1, 1},
	    {"abs", "abs", 1, 1},
	};
	return map;
}

bool IsStringBase(const std::string &lower) {
	return lower == "varchar" || lower == "text" || lower == "string" || lower == "char" ||
	       lower == "bpchar" || lower == "uuid" || lower == "blob" || lower == "json" ||
	       lower == "bit" || lower == "bitstring";
}

std::string BaseTypeOf(const std::string &duckdb_type) {
	auto t = ToLower(duckdb_type);
	auto paren = t.find('(');
	if (paren != std::string::npos) {
		t = t.substr(0, paren);
	}
	return Trim(t);
}

} // namespace

ExpressionCompiler::ExpressionCompiler(const EdmEntity &entity_p) : entity(entity_p) {
	for (auto &prop : entity.properties) {
		property_index[ToLower(prop.name)] = &prop;
	}
}

std::string ExpressionCompiler::Compile(const Expr &expr) {
	switch (expr.kind) {
	case ExprKind::PROPERTY: {
		auto &prop = static_cast<const PropertyExpr &>(expr);
		return ResolveProperty(prop.name);
	}
	case ExprKind::LITERAL: {
		auto &lit = static_cast<const LiteralExpr &>(expr);
		return CompileLiteral(lit, "");
	}
	case ExprKind::UNARY: {
		auto &un = static_cast<const UnaryExpr &>(expr);
		return CompileUnary(un);
	}
	case ExprKind::BINARY: {
		auto &bin = static_cast<const BinaryExpr &>(expr);
		return CompileBinary(bin);
	}
	case ExprKind::FUNCTION_CALL: {
		auto &call = static_cast<const FunctionCallExpr &>(expr);
		return CompileFunction(call);
	}
	}
	throw ODataParseException("unknown expression node");
}

std::string ExpressionCompiler::ResolveProperty(const std::string &name) {
	auto it = property_index.find(ToLower(name));
	if (it == property_index.end()) {
		throw ODataParseException("property '" + name + "' does not exist on entity '" + entity.name + "'");
	}
	return QuoteIdentifier(it->second->name);
}

std::string ExpressionCompiler::CompileBinary(const BinaryExpr &expr) {
	// Null literal special-casing (OData semantics: eq null / ne null)
	auto IsNullLit = [](const Expr &e) {
		return e.kind == ExprKind::LITERAL &&
		       static_cast<const LiteralExpr &>(e).literal_kind == LiteralKind::NULL_VALUE;
	};
	auto ResolveLiteralType = [&](const Expr &other) -> std::string {
		// Determine the duckdb type of the property on the other side, if any
		if (other.kind == ExprKind::PROPERTY) {
			auto &prop = static_cast<const PropertyExpr &>(other);
			auto it = property_index.find(ToLower(prop.name));
			if (it != property_index.end() && it->second->edm_type != EdmType::UNSUPPORTED) {
				return it->second->duckdb_type;
			}
		}
		if (other.kind == ExprKind::FUNCTION_CALL) {
			auto &call = static_cast<const FunctionCallExpr &>(other);
			// Functions like year/month/length return INT; string funcs return bool
			auto lower = ToLower(call.name);
			if (lower == "year" || lower == "month" || lower == "day" || lower == "hour" ||
			    lower == "minute" || lower == "second" || lower == "length" || lower == "indexof") {
				return "BIGINT";
			}
			if (lower == "tolower" || lower == "toupper" || lower == "trim" || lower == "substring" ||
			    lower == "concat") {
				return "VARCHAR";
			}
			return "BOOLEAN";
		}
		return "";
	};

	const auto &op = expr.op;

	// eq null / ne null
	if (IsNullLit(*expr.left) && expr.right->kind != ExprKind::LITERAL) {
		std::string rhs = Compile(*expr.right);
		if (op == BinaryOp::EQ) {
			return rhs + " IS NULL";
		}
		if (op == BinaryOp::NE) {
			return rhs + " IS NOT NULL";
		}
	}
	if (IsNullLit(*expr.right) && expr.left->kind != ExprKind::LITERAL) {
		std::string lhs = Compile(*expr.left);
		if (op == BinaryOp::EQ) {
			return lhs + " IS NULL";
		}
		if (op == BinaryOp::NE) {
			return lhs + " IS NOT NULL";
		}
	}

	const char *sql_op = nullptr;
	switch (op) {
	case BinaryOp::EQ:
		sql_op = "=";
		break;
	case BinaryOp::NE:
		sql_op = "<>";
		break;
	case BinaryOp::GT:
		sql_op = ">";
		break;
	case BinaryOp::GE:
		sql_op = ">=";
		break;
	case BinaryOp::LT:
		sql_op = "<";
		break;
	case BinaryOp::LE:
		sql_op = "<=";
		break;
	case BinaryOp::AND:
		sql_op = "AND";
		break;
	case BinaryOp::OR:
		sql_op = "OR";
		break;
	}

	std::string lhs, rhs;
	if (expr.left->kind == ExprKind::LITERAL && expr.right->kind != ExprKind::LITERAL) {
		// literal on the left: infer type from the right-hand property
		auto lit = static_cast<const LiteralExpr &>(*expr.left);
		auto type = ResolveLiteralType(*expr.right);
		lhs = CompileLiteral(lit, type);
		rhs = Compile(*expr.right);
	} else if (expr.right->kind == ExprKind::LITERAL && expr.left->kind != ExprKind::LITERAL) {
		auto lit = static_cast<const LiteralExpr &>(*expr.right);
		auto type = ResolveLiteralType(*expr.left);
		lhs = Compile(*expr.left);
		rhs = CompileLiteral(lit, type);
	} else {
		lhs = Compile(*expr.left);
		rhs = Compile(*expr.right);
	}

	if (op == BinaryOp::AND || op == BinaryOp::OR) {
		return "(" + lhs + " " + sql_op + " " + rhs + ")";
	}
	return lhs + " " + sql_op + " " + rhs;
}

std::string ExpressionCompiler::CompileUnary(const UnaryExpr &expr) {
	if (expr.op != UnaryOp::NOT) {
		throw ODataParseException("unsupported unary operator");
	}
	return "(NOT " + Compile(*expr.child) + ")";
}

std::string ExpressionCompiler::CompileLiteral(const LiteralExpr &expr, const std::string &target_duckdb_type) {
	switch (expr.literal_kind) {
	case LiteralKind::NULL_VALUE:
		return "NULL";
	case LiteralKind::BOOLEAN:
		return expr.boolean ? "TRUE" : "FALSE";
	case LiteralKind::INTEGER:
		return std::to_string(expr.integer);
	case LiteralKind::FLOAT: {
		std::string s = std::to_string(expr.real);
		if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
		    s.find('E') == std::string::npos) {
			s += ".0";
		}
		return s;
	}
	case LiteralKind::STRING: {
		auto base = BaseTypeOf(target_duckdb_type);
		if (base.empty() || IsStringBase(base)) {
			return QuoteStringLiteral(expr.str);
		}
		// numeric / temporal column: cast the string literal explicitly so the
		// comparison is type-correct (e.g. date columns vs '2026-01-01')
		return "CAST(" + QuoteStringLiteral(expr.str) + " AS " + target_duckdb_type + ")";
	}
	}
	throw ODataParseException("unsupported literal kind");
}

std::string ExpressionCompiler::CompileFunction(const FunctionCallExpr &expr) {
	std::string lower_name = ToLower(expr.name);
	const FunctionMapEntry *entry = nullptr;
	for (auto &candidate : GetFunctionMap()) {
		if (lower_name == candidate.odata_name) {
			entry = &candidate;
			break;
		}
	}
	if (!entry) {
		throw ODataParseException("function '" + expr.name + "' is not supported in $filter");
	}
	if (expr.args.size() < static_cast<size_t>(entry->min_args) ||
	    expr.args.size() > static_cast<size_t>(entry->max_args)) {
		throw ODataParseException("function '" + expr.name + "' expects " + std::to_string(entry->min_args) + ".." +
		                          std::to_string(entry->max_args) + " arguments");
	}
	std::string result;
	if (lower_name == "now") {
		result = "now()";
	} else {
		result = std::string(entry->duckdb_name) + "(";
		bool first = true;
		for (auto &arg : expr.args) {
			if (!first) {
				result += ", ";
			}
			first = false;
			// function args typed as the function input expects; compile literal
			// without a cast unless it is a property (property literals are
			// compared later)
			if (arg->kind == ExprKind::LITERAL) {
				auto &lit = static_cast<const LiteralExpr &>(*arg);
				result += CompileLiteral(lit, "");
			} else {
				result += Compile(*arg);
			}
		}
		result += ")";
	}
	return result;
}

} // namespace duckdb_odata
