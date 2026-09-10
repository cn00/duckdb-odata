#pragma once

#include "metadata/edm_model.hpp"
#include "server/http_request.hpp"
#include "server/http_response.hpp"

namespace duckdb { class Connection; }
namespace duckdb_odata {
HttpResponse ExecuteWrite(duckdb::Connection &con, const HttpRequest &request,
                          const EdmEntity &entity, const std::vector<std::string> &keys,
                          const std::string &base_path);
}
