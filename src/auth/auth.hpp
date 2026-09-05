//===----------------------------------------------------------------------===//
// odata / auth
//
// v0.1 authentication: optional Bearer token (design doc section 35).
//===----------------------------------------------------------------------===//
#pragma once

#include <string>

namespace duckdb_odata {

// Returns true when the Authorization header value grants access given the
// configured token. When token is empty, every request is allowed.
bool CheckBearerToken(const std::string &authorization_header, const std::string &expected_token);

} // namespace duckdb_odata
