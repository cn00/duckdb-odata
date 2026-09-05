# duckdb-odata architecture

> Current version: **0.1.0** (design doc "Milestones 0–5": skeleton, HTTP,
> metadata, entity API, query options, v0.1 security). The full design
> rationale lives in [design.md](design.md).

## Goals

`duckdb-odata` is a DuckDB C++ extension that exposes DuckDB tables as an
OData v4 service in the same process:

```
DuckDB ── odata extension ──► HTTP / OData v4 ──► Power BI / Excel / .NET / curl
```

The OData layer never hands raw client text to SQL: URL query strings are
parsed into an AST, semantically validated against the live catalog, compiled
to SQL with full identifier/literal quoting, and executed read-only.

## Layered pipeline

The request path follows the design doc's core decision (§8, §12):

```
HTTP GET
   │
   ▼
HTTP server (socket backend, swappable)
   │
   ▼
OData request router        (src/server/odata_server.*)
   │   - method/auth/route checks
   │   - $select/$filter/$orderby/$top/$skip/$count handling
   ▼
ODataParser                 (src/parser/*)
   │   - lexer (src/parser/lexer.*)
   │   - AST (src/parser/ast.hpp)
   │   - $filter parser + query-option parsing (src/parser/odata_parser.*)
   ▼
Entity model                (src/metadata/*)
   │   - EDM model from the live DuckDB catalog (duckdb_columns()/tables()/...)
   │   - EDMX XML generation for $metadata
   ▼
Expression/SQL compiler     (src/compiler/*)
   │   - identifier validation + escaping (QuoteIdentifier)
   │   - typed literals, server-side $top cap
   ▼
QueryExecutor               (src/execution/query_executor.*)
   │   - LocalDuckDBExecutor on a per-request connection (v0.1)
   │   - QuackExecutor slot reserved for later versions
   ▼
ResultSerializer            (src/execution/result_serializer.*)
   │   - DuckDB value → OData JSON
   ▼
HTTP response
```

The HTTP server itself is intentionally small and isolated
(`src/server/http_request.*`, `src/server/http_response.*`,
`src/server/http_server.*`) so a different backend can be dropped in without
touching OData logic (design doc §27 "方案 B with abstraction").

## SQL-facing surface

Registered by `src/odata_extension.cpp` (see `ODataExtension::Load`):

| SQL | purpose |
| --- | --- |
| `CALL odata_serve()` | start server with defaults: `localhost` on a free port, auto-generated bearer token; returns one row (`listen_uri`/`listen_url`/`auth_token`) |
| `CALL odata_serve('http://0.0.0.0:8080', token := '…', base_path := '/odata')` | start HTTP server on the calling database instance (pinned address / token / base path) |
| `CALL odata_stop()` | stop the server for this instance |
| `SELECT * FROM odata_status()` | server/registry state |
| `CALL odata_expose('customers')` | whitelist a table as an entity set (`'s1.customers'` / `'db1.s1.customers'` also accepted); returns one `(entity, table)` row |
| `CALL odata_expose_schema('main')` | whitelist every base table of a schema; `'db1.main'` exposes a schema of an attached catalog; returns a `(entity, table)` row per table |
| `CALL odata_entity('customers', key := 'id')` | configure entity key columns |

Entity-set naming is relative to the calling scope: a table in the current
catalog+schema keeps its bare name (`customers`); non-current schemas prefix
`<schema>_` (`s1_items`) and non-current catalogs (attached databases) prefix
`<catalog>_<schema>_` (`db1_main_dorders`). Explicitly qualified `odata_expose`
inputs follow the same scheme (`'s1.x'` → `s1_x`, `'db1.s1.x'` → `db1_s1_x`).

Configuration options registered via `DBConfig::AddExtensionOption`
(`SET odata_max_top = 10000;` etc.) are documented in
[security.md](security.md).

## HTTP surface (v0.1)

Base path defaults to `/odata` (customizable via `base_path`):

- `GET /health`
- `GET /odata` — service document (list of exposed entity sets)
- `GET /odata/$metadata` — generated EDMX
- `GET /odata/{entity}` — entity set query
- `GET /odata/{entity}({key})` — single entity lookup

Query options: `$select`, `$filter`, `$orderby`, `$top`, `$skip`, `$count`.

Only `GET` is implemented (read-only v0.1); other methods return 405. Unknown
`$…` system options return 400 instead of being silently ignored, and
semantically invalid references (unknown property in `$filter`/`$select`/
`$orderby`) are rejected before any SQL is built.

## Entity model

Exposing nothing by default is deliberate (§15). Entity bindings keep only the
public name, table, optional schema and configured key columns; column and
type metadata are re-read from the catalog on every request via
`duckdb_columns()`/`duckdb_constraints()`/`duckdb_tables()`, so `CREATE TABLE`
and `ALTER TABLE` are reflected without restarting the server.

Key resolution order (v0.1): `odata_entity` configuration → single-column
`PRIMARY KEY` from the catalog → first column (documented fallback for
keyless tables).

## Security notes

See [security.md](security.md): bearer-token auth, whitelist default, read-only
GET, resource limits, identifier/literal quoting, and the explicit absence of
raw user input in SQL.

## Building

Requires a DuckDB source checkout of the version you load the extension into
(the extension is compiled against DuckDB internal headers).

```bash
# 1) have duckdb sources ready, e.g. v1.5.5
# 2) configure duckdb with this repo as an out-of-tree extension
cmake -B .dev/duckdb/build -S .dev/duckdb \
      -DCMAKE_BUILD_TYPE=Release \
      -DDUCKDB_EXTENSION_CONFIGS="$PWD/extension_config.cmake"
cmake --build .dev/duckdb/build --target odata_loadable_extension
# -> .dev/duckdb/build/extension/odata/odata.duckdb_extension
```

Run it with any matching duckdb CLI:

```sql
LOAD '/absolute/path/to/odata.duckdb_extension';
CREATE TABLE customers (id BIGINT, name VARCHAR, active BOOLEAN);
CALL odata_expose('customers');
CALL odata_entity('customers', key := 'id');
CALL odata_serve();                     -- or pin address/token:
-- CALL odata_serve('http://127.0.0.1:8080', token := 'secret');
```

The `Makefile` wraps the steps above (`make build`, `make test-http`).
