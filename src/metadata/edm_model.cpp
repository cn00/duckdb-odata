#include "metadata/edm_model.hpp"

#include "common/string_util.hpp"

namespace duckdb_odata {

EdmType DuckDBTypeToEdm(const std::string &duckdb_type, bool &serializable) {
	serializable = true;
	auto t = ToLower(duckdb_type);
	// Strip any parameters like DECIMAL(18,3), VARCHAR(10), TIME(3)
	auto paren = t.find('(');
	if (paren != std::string::npos) {
		t = t.substr(0, paren);
	}
	t = Trim(t);

	if (t == "boolean" || t == "bool") {
		return EdmType::BOOLEAN;
	}
	if (t == "tinyint" || t == "int1") {
		return EdmType::SBYTE;
	}
	if (t == "smallint" || t == "int2") {
		return EdmType::INT16;
	}
	if (t == "integer" || t == "int4" || t == "int" || t == "signed") {
		return EdmType::INT32;
	}
	if (t == "bigint" || t == "int8" || t == "long") {
		return EdmType::INT64;
	}
	if (t == "hugeint") {
		return EdmType::DECIMAL; // precision beyond Int64; Edm.Decimal is closest
	}
	if (t == "utinyint" || t == "usmallint" || t == "uinteger" || t == "ubigint" || t == "uhugeint") {
		serializable = true;
		return EdmType::DECIMAL;
	}
	if (t == "float" || t == "real") {
		return EdmType::SINGLE;
	}
	if (t == "double") {
		return EdmType::DOUBLE;
	}
	if (t == "decimal" || t == "numeric" || t == "money") {
		return EdmType::DECIMAL;
	}
	if (t == "varchar" || t == "char" || t == "bpchar" || t == "text" || t == "string") {
		return EdmType::STRING;
	}
	if (t == "date") {
		return EdmType::DATE;
	}
	if (t == "time" || t == "timetz") {
		return EdmType::TIME_OF_DAY;
	}
	if (t == "timestamp" || t == "timestamp with time zone" || t == "timestamptz" ||
	    t == "datetime" || t == "datetime2") {
		return EdmType::DATETIME_OFFSET;
	}
	if (t == "uuid") {
		return EdmType::GUID;
	}
	if (t == "blob" || t == "bytea" || t == "binary" || t == "varbinary" || t == "bit" || t == "bitstring") {
		return EdmType::BINARY;
	}
	// Everything else is not supported in v0.1 (LIST, STRUCT, MAP, UNION,
	// JSON, INTERVAL, GEOMETRY, ...) per the design doc's type notes.
	serializable = false;
	return EdmType::UNSUPPORTED;
}

const char *EdmTypeName(EdmType type) {
	switch (type) {
	case EdmType::BOOLEAN:
		return "Edm.Boolean";
	case EdmType::SBYTE:
		return "Edm.SByte";
	case EdmType::INT16:
		return "Edm.Int16";
	case EdmType::INT32:
		return "Edm.Int32";
	case EdmType::INT64:
		return "Edm.Int64";
	case EdmType::SINGLE:
		return "Edm.Single";
	case EdmType::DOUBLE:
		return "Edm.Double";
	case EdmType::DECIMAL:
		return "Edm.Decimal";
	case EdmType::STRING:
		return "Edm.String";
	case EdmType::DATE:
		return "Edm.Date";
	case EdmType::TIME_OF_DAY:
		return "Edm.TimeOfDay";
	case EdmType::DATETIME_OFFSET:
		return "Edm.DateTimeOffset";
	case EdmType::GUID:
		return "Edm.Guid";
	case EdmType::BINARY:
		return "Edm.Binary";
	default:
		return "Edm.String";
	}
}

bool IsSupportedEdmType(EdmType type) {
	return type != EdmType::UNSUPPORTED;
}

std::string XmlEscape(const std::string &s) {
	std::string out;
	for (char c : s) {
		switch (c) {
		case '&':
			out += "&amp;";
			break;
		case '<':
			out += "&lt;";
			break;
		case '>':
			out += "&gt;";
			break;
		case '"':
			out += "&quot;";
			break;
		case '\'':
			out += "&apos;";
			break;
		default:
			out += c;
		}
	}
	return out;
}

std::string GenerateEdmx(const EdmModel &model) {
	std::string xml;
	xml += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	xml += "<edmx:Edmx Version=\"4.0\" xmlns:edmx=\"http://docs.oasis-open.org/odata/ns/edmx\">\n";
	xml += "  <edmx:DataServices>\n";
	xml += "    <Schema Namespace=\"DuckDB\" xmlns=\"http://docs.oasis-open.org/odata/ns/edm\">\n";

	for (auto &entity : model.entities) {
		// EntityType
		xml += "      <EntityType Name=\"" + XmlEscape(entity.name) + "\">\n";
		if (entity.has_key) {
			xml += "        <Key>\n";
			for (auto &prop : entity.properties) {
				if (prop.is_key) {
					xml += "          <PropertyRef Name=\"" + XmlEscape(prop.name) + "\"/>\n";
				}
			}
			xml += "        </Key>\n";
		}
		for (auto &prop : entity.properties) {
			if (!prop.serializable) {
				continue; // complex types not modelled in v0.1
			}
			xml += "        <Property Name=\"" + XmlEscape(prop.name) + "\" Type=\"" +
			       std::string(EdmTypeName(prop.edm_type)) + "\"";
			if (!prop.nullable) {
				xml += " Nullable=\"false\"";
			}
			xml += "/>\n";
		}
		xml += "      </EntityType>\n";
	}
	// container
	xml += "      <EntityContainer Name=\"DuckDBContext\">\n";
	for (auto &entity : model.entities) {
		xml += "        <EntitySet Name=\"" + XmlEscape(entity.name) + "\" EntityType=\"DuckDB." +
		       XmlEscape(entity.name) + "\"/>\n";
	}
	xml += "      </EntityContainer>\n";
	xml += "    </Schema>\n";
	xml += "  </edmx:DataServices>\n";
	xml += "</edmx:Edmx>\n";
	return xml;
}

} // namespace duckdb_odata
