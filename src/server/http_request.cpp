#include "server/http_request.hpp"

#include <cstring>
#include <sstream>

namespace duckdb_odata {

// Parse an HTTP/1.x GET request head. Returns false if the head is malformed.
// `head` must contain everything up to and including the blank line.
bool ParseHttpRequest(const std::string &head, HttpRequest &request) {
	auto line_end = head.find("\r\n");
	if (line_end == std::string::npos) {
		return false;
	}
	std::string request_line = head.substr(0, line_end);
	std::istringstream line_ss(request_line);
	std::string target;
	if (!(line_ss >> request.method >> target)) {
		return false;
	}
	// parse target: /path?query
	auto qmark = target.find('?');
	if (qmark == std::string::npos) {
		request.path = UrlDecode(target);
		request.raw_query = "";
	} else {
		request.path = UrlDecode(target.substr(0, qmark));
		request.raw_query = target.substr(qmark + 1);
	}
	request.query = ParseQueryString(request.raw_query);

	// headers
	size_t pos = line_end + 2;
	while (pos < head.size()) {
		auto hdr_end = head.find("\r\n", pos);
		if (hdr_end == std::string::npos) {
			break;
		}
		std::string header_line = head.substr(pos, hdr_end - pos);
		pos = hdr_end + 2;
		if (header_line.empty()) {
			break; // end of headers
		}
		auto colon = header_line.find(':');
		if (colon == std::string::npos) {
			return false;
		}
		std::string name = header_line.substr(0, colon);
		std::string value = header_line.substr(colon + 1);
		// trim value
		size_t start = 0;
		while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
			start++;
		}
		size_t end = value.size();
		while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) {
			end--;
		}
		request.headers[ToLower(name)] = value.substr(start, end - start);
	}
	return true;
}

} // namespace duckdb_odata
