cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME odata)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

cmake_host_system_information(RESULT CORE_COUNT QUERY NUMBER_OF_LOGICAL_CORES)
set(CMAKE_BUILD_PARALLEL_LEVEL ${CORE_COUNT} CACHE STRING "Number of parallel build jobs")

project(${TARGET_NAME})

# DuckDB's own build pins CMAKE_CXX_STANDARD=11 (see duckdb root CMakeLists).
# Extensions must stay compatible with that standard, so we do not bump it.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(EXTENSION_SOURCES
    src/odata_extension.cpp
    src/common/string_util.cpp
    src/parser/lexer.cpp
    src/parser/odata_parser.cpp
    src/compiler/expression_compiler.cpp
    src/compiler/sql_compiler.cpp
    src/metadata/edm_model.cpp
    src/metadata/metadata_generator.cpp
    src/execution/query_executor.cpp
    src/execution/result_serializer.cpp
    src/server/http_request.cpp
    src/server/http_response.cpp
    src/server/http_server.cpp
    src/server/odata_server.cpp
    src/auth/auth.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
