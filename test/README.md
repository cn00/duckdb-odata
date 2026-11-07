# Testing duckdb-odata

`sql/server.test` and `sql/parser_compiler.test` are DuckDB SQLLogicTests
discovered by the official CI runner. They require the OData extension and
cover registration, schema exposure, lifecycle behavior, filter parsing and
SQL compilation without starting an HTTP server.
`sql/server.sql` is a separate CLI-only smoke script, not a SQLLogicTest.
`http/` contains Python end-to-end tests for the OData HTTP API and writes.
The HTTP server uses Winsock on Windows and POSIX sockets on other native
platforms; the SQL lifecycle tests exercise native socket startup and shutdown.

Run the SQLLogicTests with `make test`, or run both HTTP suites with
`make test-http` after initializing submodules. Build first with `make`;
use `make debug` and `make test_debug` for a debug build. The extension config
enables `LOAD_TESTS` so DuckDB registers these tests as extension tests.