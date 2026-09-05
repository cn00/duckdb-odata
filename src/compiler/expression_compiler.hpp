//===----------------------------------------------------------------------===//
// odata / compiler / expression compiler
//
// Compiles an OData expression AST into a DuckDB SQL expression *fragment*.
// Only validated identifiers ever reach SQL text (design doc sections 11, 19,
// 38): property names must exist in the entity model and are emitted through
// QuoteIdentifier; literals are type-checked against the property type when a
// side of a comparison is a known column.
//===----------------------------------------------------------------------===//
#pragma once

#include "metadata/edm_model.hpp"
#include "parser/ast.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace duckdb_odata {

class ExpressionCompiler {
public:
	// type_of(property_name_lower) -> duckdb type name; used for literal casts.
	explicit ExpressionCompiler(const EdmEntity &entity);

	// Throws ODataParseException for semantically invalid expressions.
	std::string Compile(const Expr &expr);

private:
	std::string CompileBinary(const BinaryExpr &expr);
	std::string CompileUnary(const UnaryExpr &expr);
	std::string CompileLiteral(const LiteralExpr &expr, const std::string &target_duckdb_type);
	std::string CompileFunction(const FunctionCallExpr &expr);
	std::string ResolveProperty(const std::string &name);

	const EdmEntity &entity;
	std::unordered_map<std::string, const EdmProperty *> property_index;
};

} // namespace duckdb_odata
