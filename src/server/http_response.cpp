#include "server/http_response.hpp"

namespace duckdb_odata {

const char *HttpStatusPhrase(int status) {
	switch (status) {
	case 200:
		return "OK";
	case 201:
		return "Created";
	case 204:
		return "No Content";
	case 304:
		return "Not Modified";
	case 400:
		return "Bad Request";
	case 401:
		return "Unauthorized";
	case 403:
		return "Forbidden";
	case 404:
		return "Not Found";
	case 405:
		return "Method Not Allowed";
	case 406:
		return "Not Acceptable";
	case 409:
		return "Conflict";
	case 412:
		return "Precondition Failed";
	case 413:
		return "Payload Too Large";
	case 415:
		return "Unsupported Media Type";
	case 500:
		return "Internal Server Error";
	case 501:
		return "Not Implemented";
	default:
		return "Unknown";
	}
}

std::string HttpResponse::ToWire(const std::string &http_version) const {
	std::string wire = http_version + " " + std::to_string(status) + " " + HttpStatusPhrase(status) + "\r\n";
	for (auto &kv : headers) {
		wire += kv.first + ": " + kv.second + "\r\n";
	}
	if (headers.count("Content-Length") == 0 && headers.count("Transfer-Encoding") == 0) {
		wire += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	}
	wire += "Connection: close\r\n";
	wire += "\r\n";
	wire += body;
	return wire;
}

} // namespace duckdb_odata
