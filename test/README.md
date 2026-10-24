# Testing duckdb-odata

`sql/` contains DuckDB SQLLogicTests and `http/` contains Python end-to-end
tests for the OData HTTP API and write operations.

Run the SQLLogicTests with `make test`, or run both HTTP suites with
`make test-http` after initializing submodules.