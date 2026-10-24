cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME odata)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_CXX_STANDARD
    "17"
    CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)

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
    src/execution/write_executor.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} duckdb_yyjson)
target_link_libraries(${LOADABLE_EXTENSION_NAME} duckdb_yyjson)

if(WIN32)
  target_link_libraries(${EXTENSION_NAME} ws2_32)
  target_link_libraries(${LOADABLE_EXTENSION_NAME} ws2_32)
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
