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
// DuckDB table (optionally schema qualified) + optional configured key columns.
struct EntityBinding {
	std::string name;    // public entity set name (also the default table name)
	std::string table;   // DuckDB table name
	std::string schema;  // optional: DuckDB schema (empty = resolve at runtime)
	std::vector<std::string> configured_keys; // from odata_entity
};

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
