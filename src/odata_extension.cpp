#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

class ODataExtension : public Extension {
public:
	std::string Name() override {
		return "odata";
	}
	std::string Version() const override {
		return "0.1.0";
	}
	void Load(ExtensionLoader &loader) override {
		// placeholder: no functions registered yet
	}
};

} // namespace duckdb

extern "C" {
DUCKDB_EXTENSION_API void odata_duckdb_cpp_init(duckdb::ExtensionLoader &loader) {
	duckdb::ODataExtension ext;
	ext.Load(loader);
}
}
