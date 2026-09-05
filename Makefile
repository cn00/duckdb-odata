# duckdb-odata development helpers.
#
# DUCKDB_SRC points at a DuckDB source tree of the version you want to load the
# extension into. Defaults to .dev/duckdb (clone/extract v1.5.5 there).
DUCKDB_SRC ?= $(CURDIR)/.dev/duckdb
BUILD_DIR ?= $(DUCKDB_SRC)/build

.PHONY: build test-http clean configure

configure:
	cmake -B $(BUILD_DIR) -S $(DUCKDB_SRC) -DCMAKE_BUILD_TYPE=Release \
		-DDUCKDB_EXTENSION_CONFIGS="$(CURDIR)/extension_config.cmake"

build: configure
	cmake --build $(BUILD_DIR) --target odata_loadable_extension -j
	@echo "extension built at: $(BUILD_DIR)/extension/odata/odata.duckdb_extension"

test-http: build
	python3 test/http/odata_http_test.py

clean:
	rm -rf $(BUILD_DIR)
