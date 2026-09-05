//===----------------------------------------------------------------------===//
// odata / metadata / metadata generator
//
// Builds an EdmModel from the live DuckDB catalog for the currently exposed
// entities (design doc section 13: DuckDB Catalog -> EDM model). Introspection
// goes through duckdb_* catalog table functions so it tracks schema changes at
// request time; nothing here ever interpolates user strings into SQL.
//===----------------------------------------------------------------------===//
#pragma once

#include "metadata/edm_model.hpp"

#include <string>
#include <vector>

namespace duckdb {

class Connection;

} // namespace duckdb

namespace duckdb_odata {

// A requested (possibly configured) entity binding: public OData name + the
// DuckDB table (optionally catalog/schema qualified) + optional configured
// key columns.
struct EntityBinding {
	std::string name;    // public entity set name (defaults to the table name)
	std::string table;   // DuckDB table name
	std::string schema;  // optional: DuckDB schema (empty = current schema)
	std::string catalog; // optional: DuckDB catalog (empty = current catalog)
	std::vector<std::string> configured_keys; // from odata_entity
};

// Parse a DuckDB-qualified identifier into binding fields.
//
// Accepted forms:
//   customers           -> table only (resolved in the current schema)
//   s1.customers        -> schema + table
//
// Public-name rule (deterministic, no connection needed): a schema-qualified
// input gets the OData entity-set name "<schema>_<table>" (a legal EDM simple
// identifier, avoids clashes across schemas); an unqualified input keeps the
// bare table name (resolved in the default schema), preserving single-schema
// usage. Catalog qualification is not supported yet (v0.1).
//
// Throws ODataParseException for empty inputs or >2 components.
EntityBinding ParseQualifiedBinding(const std::string &qualified_name);

// Resolve and pin the concrete catalog/schema of `binding.table` against the
// live catalog (fills binding.catalog/schema).
//
//   - when binding.schema is set, the table must exist in exactly that schema;
//   - otherwise, if the table exists in exactly one schema it is adopted;
//   - if it exists in several schemas an ambiguity error is raised asking for
//     a qualified name (schema.table).
// Throws ODataParseException when the table cannot be uniquely resolved.
void ResolveBindingTable(duckdb::Connection &con, EntityBinding &binding);

class MetadataGenerator {
public:
	explicit MetadataGenerator(duckdb::Connection &connection) : con(connection) {
	}

	// Build the full model for the given bindings (order preserved).
	EdmModel BuildModel(const std::vector<EntityBinding> &bindings) const;

	// Build a model for a single binding (used by collection handlers).
	EdmEntity BuildEntity(const EntityBinding &binding) const;

private:
	duckdb::Connection &con;
};

} // namespace duckdb_odata
