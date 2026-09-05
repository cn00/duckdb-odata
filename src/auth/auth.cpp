#include "auth/auth.hpp"

#include "common/string_util.hpp"

#include <random>

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

std::string GenerateAuthToken() {
	static const char *hex = "0123456789ABCDEF";
	std::random_device rd;
	std::uniform_int_distribution<int> byte(0, 255);
	std::string token;
	token.reserve(32);
	for (int i = 0; i < 16; i++) {
		int b = byte(rd);
		token += hex[(b >> 4) & 0xF];
		token += hex[b & 0xF];
	}
	return token;
}

} // namespace duckdb_odata
