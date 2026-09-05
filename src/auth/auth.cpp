#include "auth/auth.hpp"

#include "common/string_util.hpp"

namespace duckdb_odata {

bool CheckBearerToken(const std::string &authorization_header, const std::string &expected_token) {
	if (expected_token.empty()) {
		return true;
	}
	// Parse "Bearer <token>" (case-insensitive scheme per RFC 6750)
	auto scheme_end = authorization_header.find(' ');
	if (scheme_end == std::string::npos) {
		return false;
	}
	std::string scheme = authorization_header.substr(0, scheme_end);
	if (ToLower(scheme) != "bearer") {
		return false;
	}
	std::string token = Trim(authorization_header.substr(scheme_end + 1));
	// constant-time-ish comparison
	if (token.size() != expected_token.size()) {
		return false;
	}
	unsigned char diff = 0;
	for (size_t i = 0; i < token.size(); i++) {
		diff |= static_cast<unsigned char>(token[i] ^ expected_token[i]);
	}
	return diff == 0;
}

} // namespace duckdb_odata
