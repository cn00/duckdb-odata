# This file is included by DuckDB's build system. It specifies which extension to load.
#
# Build this extension against a DuckDB source tree with:
#
#   cmake -B <build-dir> -S <duckdb-src> \
#         -DDUCKDB_EXTENSION_CONFIGS="<path-to-this-repo>/extension_config.cmake"
#
# then build only the loadable extension with:
#
#   cmake --build <build-dir> --target odata_loadable_extension
#
# and load the produced .duckdb_extension into a matching duckdb CLI with `-unsigned`.

duckdb_extension_load(odata
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    DONT_LINK
    LOAD_TESTS
)
