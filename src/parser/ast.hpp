//===----------------------------------------------------------------------===//
// odata AST
//
// OData query expression tree produced by the OData filter parser. This file
// intentionally has no DuckDB dependency so it can be unit-tested standalone.
//===----------------------------------------------------------------------===//
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace duckdb_odata {

enum class BinaryOp { EQ, NE, GT, GE, LT, LE, AND, OR };

enum class UnaryOp { NOT };

enum class LiteralKind { INTEGER, FLOAT, STRING, BOOLEAN, NULL_VALUE };

enum class ExprKind { PROPERTY, LITERAL, BINARY, UNARY, FUNCTION_CALL };

struct Expr {
	ExprKind kind;
	explicit Expr(ExprKind kind_p) : kind(kind_p) {
	}
	virtual ~Expr() = default;
};

struct PropertyExpr : Expr {
	PropertyExpr() : Expr(ExprKind::PROPERTY) {
	}
	std::string name;
};

struct LiteralExpr : Expr {
	LiteralExpr() : Expr(ExprKind::LITERAL) {
	}
	LiteralKind literal_kind = LiteralKind::STRING;
	// value payload: only the relevant field is populated
	std::string str;
	int64_t integer = 0;
	double real = 0;
	bool boolean = false;
};

struct BinaryExpr : Expr {
	BinaryExpr() : Expr(ExprKind::BINARY) {
	}
	BinaryOp op;
	std::unique_ptr<Expr> left;
	std::unique_ptr<Expr> right;
};

struct UnaryExpr : Expr {
	UnaryExpr() : Expr(ExprKind::UNARY) {
	}
	UnaryOp op;
	std::unique_ptr<Expr> child;
};

// A function call, e.g. contains(name,'Alice'). The function name is a
// whitelisted identifier (validated semantically before SQL generation).
struct FunctionCallExpr : Expr {
	FunctionCallExpr() : Expr(ExprKind::FUNCTION_CALL) {
	}
	std::string name;
	std::vector<std::unique_ptr<Expr>> args;
};

enum class SortOrder { ASC, DESC };

struct OrderByTerm {
	std::string property;
	SortOrder order = SortOrder::ASC;
};

// The normalized OData query (design doc section 10).
struct ODataQuery {
	std::string entity_set;
	std::vector<std::string> select; // empty => all properties
	std::unique_ptr<Expr> filter;
	std::vector<OrderByTerm> order_by;
	// -1 == not specified
	int64_t top = -1;
	int64_t skip = -1;
	bool count = false;
};

} // namespace duckdb_odata
