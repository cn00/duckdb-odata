//===----------------------------------------------------------------------===//
// odata / metadata / edm model
//
// EDM (Entity Data Model) description of an exposed entity set: the columns
// of a DuckDB table mapped onto OData types (design doc sections 13-14).
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <vector>

namespace duckdb_odata {

// OData primitive types used by v0.1 (design doc section 14 mapping table).
enum class EdmType {
	BOOLEAN,    // Edm.Boolean
	SBYTE,      // Edm.SByte
	INT16,      // Edm.Int16
	INT32,      // Edm.Int32
	INT64,      // Edm.Int64
	SINGLE,     // Edm.Single
	DOUBLE,     // Edm.Double
	DECIMAL,    // Edm.Decimal
	STRING,     // Edm.String
	DATE,       // Edm.Date
	TIME_OF_DAY, // Edm.TimeOfDay
	DATETIME_OFFSET, // Edm.DateTimeOffset
	GUID,       // Edm.Guid
	BINARY,     // Edm.Binary
	UNSUPPORTED
};

struct EdmProperty {
	std::string name;
	EdmType edm_type = EdmType::STRING;
	bool nullable = true;
	bool is_key = false;
	// DuckDB-side type name (as reported by duckdb_columns()), e.g. "BIGINT",
	// used to emit typed SQL casts and JSON serialization hints.
	std::string duckdb_type;
	// Whether duckdb_type is a scalar type we can safely project/serialize.
	bool serializable = true;
};

// An exposed entity set. `name` is the public OData name, `catalog/schema/table`
// identify the DuckDB object (empty catalog => current).
struct EdmEntity {
	std::string name;
	std::string catalog;
	std::string schema;
	std::string table;
	std::vector<EdmProperty> properties;
	bool has_key = false;
};

// Whole EDM: list of exposed entities, in $metadata order.
struct EdmModel {
	std::vector<EdmEntity> entities;
};

// Map a DuckDB type name (string, as reported by the DuckDB catalog) to an
// EDM type. Complex / not-yet-supported types map to UNSUPPORTED.
EdmType DuckDBTypeToEdm(const std::string &duckdb_type, bool &serializable);

// Pretty EDM type name used in EDMX documents (e.g. "Edm.Int64").
const char *EdmTypeName(EdmType type);

// Is the given type a "supported scalar" for v0.1 projection/serialization?
bool IsSupportedEdmType(EdmType type);

// Render an EDMX (CSDL XML) document for the model (design doc section 13).
std::string GenerateEdmx(const EdmModel &model);

} // namespace duckdb_odata
