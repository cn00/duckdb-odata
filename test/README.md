# Testing duckdb-odata

`sql/server.test` is the DuckDB SQLLogicTest discovered by the official CI
runner. It requires the OData extension and checks registration, empty and
multi-row schema exposure, named parameters, and the server lifecycle.
`sql/server.sql` is a separate CLI-only smoke script, not a SQLLogicTest.
`http/` contains Python end-to-end tests for the OData HTTP API and writes.
The HTTP server uses Winsock on Windows and POSIX sockets on other native
platforms; the SQL lifecycle tests exercise native socket startup and shutdown.

Run the SQLLogicTests with `make test`, or run both HTTP suites with
`make test-http` after initializing submodules. Build first with `make`;
use `make debug` and `make test_debug` for a debug build. The extension config
enables `LOAD_TESTS` so DuckDB registers these tests as extension tests.