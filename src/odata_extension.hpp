//===----------------------------------------------------------------------===//
// odata extension entry point
//
// Registers the SQL-facing surface of the extension (design doc section 5):
//
//   CALL odata_serve();                                    -- defaults: localhost, free port, auto token
//   CALL odata_serve('http://0.0.0.0:8080');               -- 1-row result: listen_uri/listen_url/auth_token
//   CALL odata_stop();
//   SELECT * FROM odata_status();
//   CALL odata_expose('customers');
//   CALL odata_expose_schema('main');
//   CALL odata_entity('customers', key := 'id');
//
// and the resource-limit configuration options (design doc section 39).
//===----------------------------------------------------------------------===//
#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ODataExtension : public Extension {
public:
	void Load(ExtensionLoader &loader) override;
	std::string Name() override;
	std::string Version() const override;
};

} // namespace duckdb
