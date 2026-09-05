//===----------------------------------------------------------------------===//
// odata / server / http request
//
// Minimal HTTP/1.1 request model (GET-only for v0.1).
//===----------------------------------------------------------------------===//
#pragma once

#include "common/string_util.hpp"

#include <map>
#include <string>
#include <vector>

namespace duckdb_odata {

struct HttpRequest {
	std::string method; // GET (others rejected before parsing)
	std::string path;   // URL-decoded path, no query string
	std::string raw_query;
	// decoded query pairs
	std::vector<std::pair<std::string, std::string>> query;
	std::map<std::string, std::string> headers; // lower-cased keys

	bool HasHeader(const std::string &name) const {
		return headers.count(ToLower(name)) > 0;
	}
	std::string GetHeader(const std::string &name) const {
		auto it = headers.find(ToLower(name));
		return it == headers.end() ? std::string() : it->second;
	}
};

// Parse an HTTP request head ("METHOD /path?query HTTP/1.x\r\n<headers>\r\n").
bool ParseHttpRequest(const std::string &head, HttpRequest &request);

} // namespace duckdb_odata
