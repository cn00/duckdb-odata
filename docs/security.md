# duckdb-odata security model (v0.1)

Security is layered in front of the parser and compiler so that no client
input can reach SQL unvalidated (design doc §34-§40).

## Layer order

```
HTTP
 ├─ read-only enforcement (only GET)
 ├─ authentication   (optional bearer token)
 ├─ entity whitelist (expose nothing by default)
 ├─ $ query-option validation
 ├─ OData parse  -> AST
 ├─ semantic validation against the live catalog
 └─ SQL generation (quoted identifiers / typed literals only)
```

## Authentication

Optional shared-secret bearer token (design doc §35):

```sql
CALL odata_serve('http://0.0.0.0:8080', token := 'secret');
```

Leaving the token out does **not** disable auth: `odata_serve()` generates a
random 128-bit token (32 uppercase hex chars) and returns it in the
`auth_token` column of its one-row result (`listen_uri`/`listen_url`/
`auth_token`). Pass `token := ''` to run with authentication disabled — local
development only.

Requests must then send `Authorization: Bearer secret`; otherwise the server
answers `401`. The scheme match is case-insensitive and the token comparison
is constant-time. Tokens are compared in memory only; enable TLS via a reverse
proxy in front of the extension (the socket backend has no TLS in v0.1).

## Authorization / exposure control

The default is **expose nothing** (design doc §15):

```sql
CALL odata_expose('customers');        -- exactly one table
CALL odata_expose_schema('main');      -- whole schema, explicitly
```

Every endpoint resolves tables against this whitelist; non-exposed names get
`404`. Per-entity column lists (`columns := [...]`) are part of the design
(§36) but not yet implemented.

## Read-only by construction

- Only the HTTP `GET` method is routed (other methods → 405).
- The SQL compiler can only emit `SELECT` statements.
- A separate DuckDB `Connection` per request keeps requests isolated.

## Injection resistance

- `$select`, `$orderby` and `$filter` identifiers are looked up in the live
  catalog columns and then emitted through `QuoteIdentifier` (double-quote
  escaping).
- Literals are re-typed by the compiler: strings via single-quote escaping,
  numbers/booleans directly, and string literals compared to numeric/temporal
  columns are emitted as explicit `CAST(... AS <type>)`.
- Unknown properties, functions outside the whitelist, unknown `$`-options and
  malformed filter syntax produce `400` before any SQL is built.
- No code path concatenates raw user text into a SQL string.

## Resource limits

`SET odata_*` variables (design doc §39):

| Setting | default | enforced |
| --- | --- | --- |
| `odata_max_top` | 10000 | ✅ caps `$top` |
| `odata_max_filter_depth` | 64 | parser uses recursion limits |
| `odata_max_response_bytes` | 100 MiB | ✅ guardrail while buffering |
| `odata_query_timeout_ms` | 0 | accepted, not yet enforced |

Reading happens once when `odata_serve` is invoked, so set the variables
before starting the server.

## Deployment notes

- Run DuckDB + this extension behind a TLS-terminating reverse proxy (nginx,
  Envoy, …) for anything beyond localhost (§3).
- The extension performs no row-level filtering yet (tenant isolation is
  staged; §37).
