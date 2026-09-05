#include "common/string_util.hpp"

#include <cctype>

namespace duckdb_odata {

std::string QuoteIdentifier(const std::string &name) {
	std::string result = "\"";
	for (auto c : name) {
		if (c == '"') {
			result += "\"\"";
		} else {
			result += c;
		}
	}
	result += "\"";
	return result;
}

std::string QuoteStringLiteral(const std::string &value) {
	std::string result = "'";
	for (auto c : value) {
		if (c == '\'') {
			result += "''";
		} else {
			result += c;
		}
	}
	result += "'";
	return result;
}

std::string JsonEscape(const std::string &value) {
	std::string result;
	result.reserve(value.size() + 8);
	for (unsigned char c : value) {
		switch (c) {
		case '"':
			result += "\\\"";
			break;
		case '\\':
			result += "\\\\";
			break;
		case '\b':
			result += "\\b";
			break;
		case '\f':
			result += "\\f";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\t':
			result += "\\t";
			break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				result += buf;
			} else {
				result += static_cast<char>(c);
			}
		}
	}
	return result;
}

static int HexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

std::string UrlDecode(const std::string &input) {
	std::string result;
	result.reserve(input.size());
	for (size_t i = 0; i < input.size(); i++) {
		char c = input[i];
		if (c == '%' && i + 2 < input.size()) {
			int hi = HexValue(input[i + 1]);
			int lo = HexValue(input[i + 2]);
			if (hi >= 0 && lo >= 0) {
				result += static_cast<char>((hi << 4) | lo);
				i += 2;
				continue;
			}
		} else if (c == '+') {
			result += ' ';
			continue;
		}
		result += c;
	}
	return result;
}

std::vector<std::pair<std::string, std::string>> ParseQueryString(const std::string &query) {
	std::vector<std::pair<std::string, std::string>> result;
	if (query.empty()) {
		return result;
	}
	size_t pos = 0;
	while (pos <= query.size()) {
		size_t amp = query.find('&', pos);
		std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
		if (!pair.empty()) {
			size_t eq = pair.find('=');
			if (eq == std::string::npos) {
				result.emplace_back(UrlDecode(pair), "");
			} else {
				result.emplace_back(UrlDecode(pair.substr(0, eq)), UrlDecode(pair.substr(eq + 1)));
			}
		}
		if (amp == std::string::npos) {
			break;
		}
		pos = amp + 1;
	}
	return result;
}

std::string ToLower(const std::string &input) {
	std::string result = input;
	for (auto &c : result) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return result;
}

std::string Trim(const std::string &input) {
	size_t start = 0;
	while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
		start++;
	}
	size_t end = input.size();
	while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
		end--;
	}
	return input.substr(start, end - start);
}

bool StartsWith(const std::string &name, const std::string &prefix) {
	return name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0;
}

} // namespace duckdb_odata
