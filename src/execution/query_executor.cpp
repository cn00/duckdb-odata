#include "execution/query_executor.hpp"

#include "duckdb.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/query_result.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace duckdb_odata {

std::unique_ptr<duckdb::QueryResult> LocalDuckDBExecutor::Execute(duckdb::Connection &con,
                                                                  const CompiledQuery &query) const {
	return con.Query(query.sql);
}

std::unique_ptr<duckdb::QueryResult> LocalDuckDBExecutor::Execute(duckdb::Connection &con,
                                                                    const CompiledQuery &query,
                                                                    int64_t timeout_ms) const {
	if (timeout_ms <= 0) {
		return Execute(con, query);
	}
	std::mutex mutex;
	std::condition_variable completed;
	bool done = false;
	std::thread watchdog([&] {
		std::unique_lock<std::mutex> lock(mutex);
		if (!completed.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return done; })) {
			con.Interrupt();
		}
	});
	auto result = Execute(con, query);
	{
		std::lock_guard<std::mutex> lock(mutex);
		done = true;
	}
	completed.notify_one();
	watchdog.join();
	return result;
}

} // namespace duckdb_odata
