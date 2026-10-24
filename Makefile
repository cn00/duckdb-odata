PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=odata
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

DUCKDB_CLI ?= $(PROJ_DIR)build/release/duckdb
EXTENSION_PATH ?= $(PROJ_DIR)build/release/extension/odata/odata.duckdb_extension

.PHONY: test-http
test-http: all
	python3 test/http/odata_http_test.py "$(DUCKDB_CLI)" "$(EXTENSION_PATH)"
	python3 test/http/odata_write_test.py "$(DUCKDB_CLI)" "$(EXTENSION_PATH)"