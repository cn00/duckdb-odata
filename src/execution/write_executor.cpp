#include "execution/write_executor.hpp"
#include "common/string_util.hpp"
#include "execution/result_serializer.hpp"
#include "duckdb.hpp"
#include "yyjson.hpp"
#include <set>

namespace duckdb_odata {
using namespace duckdb_yyjson;

namespace {
HttpResponse Error(int status, const std::string &message) {
	HttpResponse response;
	response.status = status;
	response.headers["Content-Type"] = "application/json";
	response.headers["OData-Version"] = "4.0";
	response.body = "{\"error\":{\"code\":\"WriteError\",\"message\":\"" + JsonEscape(message) + "\"}}";
	return response;
}
std::string EncodePath(const std::string &text) {
	const char *hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
			out += c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}
struct Transaction {
	duckdb::Connection &con;
	explicit Transaction(duckdb::Connection &connection) : con(connection) {}
	bool committed = false;
	~Transaction() {
		if (!committed) {
			try {
				con.Query("ROLLBACK");
			} catch (...) {
				// The connection destructor also aborts any remaining transaction.
			}
		}
	}
};
}

HttpResponse ExecuteWrite(duckdb::Connection &con, const HttpRequest &request,
                          const EdmEntity &entity, const std::vector<std::string> &keys,
                          const std::string &base_path) {
	const bool insert = request.method == "POST";
	const bool update = request.method == "PATCH";
	if (!request.query.empty()) return Error(400, "query options are not supported on writes");
	if (request.HasHeader("If-Match") || request.HasHeader("If-None-Match"))
		return Error(400, "conditional writes are not supported");
	if ((insert && !keys.empty()) || (!insert && keys.size() != 1))
		return Error(400, "POST requires a collection; PATCH and DELETE require one key");
	const EdmProperty *key = nullptr;
	for (auto &p : entity.properties) {
		if (p.is_key) {
			if (key) return Error(400, "compound keys are not supported");
			key = &p;
		}
	}
	if (!key) return Error(400, "writes require an exposed key column");
	if (!key->serializable || key->edm_type == EdmType::BINARY)
		return Error(400, "unsupported key type for writes");
	std::string table = QuoteIdentifier(entity.catalog) + "." + QuoteIdentifier(entity.schema) + "." + QuoteIdentifier(entity.table);
	duckdb::vector<duckdb::Value> values;
	std::string columns, placeholders, assignments;
	if (insert || update) {
		auto media = ToLower(Trim(request.GetHeader("Content-Type").substr(0, request.GetHeader("Content-Type").find(';'))));
		if (media != "application/json") return Error(415, "Content-Type must be application/json");
		std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
		    yyjson_read(request.body.data(), request.body.size(), YYJSON_READ_NUMBER_AS_RAW), yyjson_doc_free);
		if (!doc || !yyjson_is_obj(yyjson_doc_get_root(doc.get()))) return Error(400, "body must be a JSON object");
		auto root = yyjson_doc_get_root(doc.get());
		std::set<std::string> seen;
		size_t i, max;
		yyjson_val *name, *value;
		yyjson_obj_foreach(root, i, max, name, value) {
			std::string field(yyjson_get_str(name), yyjson_get_len(name));
			if (!seen.insert(ToLower(field)).second) return Error(400, "duplicate property: " + field);
			const EdmProperty *property = nullptr;
			for (auto &p : entity.properties) {
				if (ToLower(p.name) == ToLower(field)) property = &p;
			}
			if (!property) return Error(400, "unknown or hidden property: " + field);
			if (!property->serializable || property->edm_type == EdmType::BINARY)
				return Error(400, "unsupported write type: " + field);
			if (update && property->is_key) return Error(400, "PATCH cannot change the key");
			duckdb::Value parameter;
			if (yyjson_is_null(value)) {
				if (!property->nullable || property->is_key) return Error(400, "null is not allowed: " + field);
			} else if (yyjson_is_str(value)) parameter = duckdb::Value(std::string(yyjson_get_str(value), yyjson_get_len(value)));
			else if (yyjson_is_raw(value)) parameter = duckdb::Value(std::string(yyjson_get_raw(value), yyjson_get_len(value)));
			else if (yyjson_is_bool(value)) parameter = duckdb::Value::BOOLEAN(yyjson_get_bool(value));
			else return Error(400, "property must be a scalar: " + field);
			if (!columns.empty()) {
				columns += ",";
				placeholders += ",";
				assignments += ",";
			}
			columns += QuoteIdentifier(property->name);
			std::string cast = "CAST(? AS " + property->duckdb_type + ")";
			placeholders += cast;
			assignments += QuoteIdentifier(property->name) + "=" + cast;
			values.push_back(parameter);
		}
		if (update && values.empty()) return Error(400, "PATCH requires at least one property");
	} else if (!request.body.empty()) return Error(400, "DELETE must not contain a body");
	std::string sql;
	if (insert) {
		sql = "INSERT INTO " + table;
		if (values.empty()) sql += " DEFAULT VALUES";
		else sql += " (" + columns + ") VALUES (" + placeholders + ")";
	} else {
		std::string raw = keys[0];
		if (raw.size() >= 2 && raw.front() == '\'' && raw.back() == '\'') {
			raw = raw.substr(1, raw.size() - 2);
			std::string unescaped;
			for (size_t i = 0; i < raw.size(); i++) {
				unescaped += raw[i];
				if (raw[i] == '\'' && i + 1 < raw.size() && raw[i + 1] == '\'') i++;
			}
			raw = unescaped;
		}
		values.push_back(duckdb::Value(raw));
		sql = update ? "UPDATE " + table + " SET " + assignments : "DELETE FROM " + table;
		sql += " WHERE " + QuoteIdentifier(key->name) + " = CAST(? AS " + key->duckdb_type + ")";
	}
	sql += " RETURNING ";
	for (size_t i = 0; i < entity.properties.size(); i++) {
		if (i) sql += ",";
		sql += QuoteIdentifier(entity.properties[i].name);
	}
	auto begin = con.Query("BEGIN TRANSACTION");
	if (begin->HasError()) return Error(500, "could not start write transaction");
	Transaction transaction(con);
	auto prepared = con.Prepare(sql);
	if (prepared->HasError()) return Error(400, prepared->GetError());
	auto result = prepared->Execute(values, false);
	if (result->HasError()) {
		auto type = result->GetErrorObject().Type();
		return Error(type == duckdb::ExceptionType::CONSTRAINT || type == duckdb::ExceptionType::TRANSACTION ? 409 : 400,
		             result->GetError());
	}
	auto chunk = result->Fetch();
	if (!chunk || !chunk->size()) return Error(404, "entity with key not found");
	auto extra = result->Fetch();
	if (chunk->size() != 1 || (extra && extra->size())) return Error(409, "key is not unique; write rolled back");
	HttpResponse response;
	response.status = insert ? 201 : 204;
	response.headers["OData-Version"] = "4.0";
	if (insert) {
		response.headers["Content-Type"] = "application/json";
		response.body = "{";
		for (size_t i = 0; i < entity.properties.size(); i++) {
			if (i) response.body += ",";
			auto value = chunk->GetValue(i, 0);
			response.body += "\"" + JsonEscape(entity.properties[i].name) + "\":" + ValueToJson(value);
			if (entity.properties[i].is_key) {
				if (value.IsNull()) return Error(409, "inserted entity has a null key; write rolled back");
				auto literal = value.ToString();
				if (key->edm_type == EdmType::STRING || key->edm_type == EdmType::GUID ||
				    key->edm_type == EdmType::DATE || key->edm_type == EdmType::TIME_OF_DAY ||
				    key->edm_type == EdmType::DATETIME_OFFSET) literal = QuoteStringLiteral(literal);
				response.headers["Location"] = base_path + "/" + EncodePath(entity.name) + "(" + EncodePath(literal) + ")";
			}
		}
		response.body += "}";
	}
	auto commit = con.Query("COMMIT");
	if (commit->HasError()) return Error(409, commit->GetError());
	transaction.committed = true;
	return response;
}
}
