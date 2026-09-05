//===----------------------------------------------------------------------===//
// odata / server / http response
//
// Minimal HTTP/1.1 response model. A response is fully buffered (the OData
// serializer writes into `body`); content-length is set when sending.
//===----------------------------------------------------------------------===//
#pragma once

#include <map>
#include <string>

namespace duckdb_odata {

struct HttpResponse {
	int status = 200;
	std::map<std::string, std::string> headers;
	std::string body;

	std::string ToWire(const std::string &http_version = "HTTP/1.1") const;
};

// Well-known status phrases
const char *HttpStatusPhrase(int status);

} // namespace duckdb_odata
