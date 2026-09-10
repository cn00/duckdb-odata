# duckdb-odata

> English | [中文版](README.zh.md)

OData v4 server as a DuckDB extension — serve any exposed DuckDB table as an
OData EntitySet with `$select`, `$filter`, `$orderby`, `$top`, `$skip` and
`$count`, plus an auto-generated `$metadata` (EDMX) from the live DuckDB
catalog.

Design document: [docs/design.md](docs/design.md)

```
DuckDB ── odata extension ──► HTTP / OData v4 ──► BI / ERP / Power BI / Excel / REST clients
```

## Quick start (v0.2)

```sql
INSTALL odata;
LOAD odata;

CREATE TABLE customers (id BIGINT, name VARCHAR, active BOOLEAN);
CALL odata_expose('customers', columns := ['id', 'name', 'active']);
CALL odata_entity('customers', key := 'id');
SET odata_page_size = 500;
CALL odata_serve('http://0.0.0.0:8080', token := 'secret');
```

Calling `odata_serve()` with no arguments binds `localhost` to a free port and
auto-generates a bearer token; it returns one row
(`listen_uri` / `listen_url` / `auth_token`) so the endpoint and token are easy
to read back:

```sql
CALL odata_serve();
-- ┌─────────────────┬──────────────────────────┬──────────────────────────────┐
-- │   listen_uri    │        listen_url        │          auth_token          │
-- ├─────────────────┼──────────────────────────┼──────────────────────────────┤
-- │ odata:localhost │ http://localhost:54321   │ 120C318F6F3E221DFEE5D6A7860… │
-- └─────────────────┴──────────────────────────┴──────────────────────────────┘
```

Requests must carry the token unless you disabled auth explicitly
(`token := ''`):

```bash
curl -H "Authorization: Bearer secret" http://localhost:8080/odata/$metadata
curl -H "Authorization: Bearer secret" http://localhost:8080/odata/customers
curl -H "Authorization: Bearer secret" 'http://localhost:8080/odata/customers?$filter=active%20eq%20true&$top=10'
curl -H "Authorization: Bearer secret" http://localhost:8080/odata/customers(1)
```

## Expose any data source (compose with other DuckDB extensions)

`odata_expose` / `odata_expose_schema` whitelist *any* table **or view** in the
DuckDB catalog — including views over files and databases opened by other
DuckDB extensions. Whatever DuckDB can `SELECT`, you can serve as a
read-only OData v4 API in a few lines:

```sql
-- Excel workbook (requires: INSTALL excel; LOAD excel)
CREATE VIEW msxlsx AS SELECT * FROM read_xlsx('/data/Active.xlsx');
CALL odata_expose('msxlsx');

-- Attached SQL Server / Postgres / SQLite databases
ATTACH 'Server=localhost,1433;Database=weconnect;User Id=sa;Password=xxx' AS ms1 (TYPE mssql);
CALL odata_expose_schema('ms1.dbo');        -- every table of ms1.dbo

ATTACH 'host=db.example.com port=5432 dbname=app user=svc password=yyy' AS pg (TYPE postgres);
CALL odata_expose_schema('pg.public');

ATTACH 'local.db' AS sq (TYPE sqlite);
CALL odata_expose_schema('sq.main');

-- Parquet / CSV on S3 (requires: INSTALL httpfs; LOAD httpfs)
CREATE VIEW s3_events AS SELECT * FROM read_parquet('s3://bucket/events/*.parquet');
CALL odata_expose('s3_events');

CALL odata_serve();   -- read listen_url / auth_token from the result row
```

Notes:

- **Live data, no copy**: every HTTP request runs a `SELECT` against the view
  or attached table, so updates are reflected immediately.
- **Catalog-qualified exposure** (`'ms1.dbo'`, `'db.schema.table'`) works for
  attached databases; entity names stay collision-free across sources
  (`expose_schema('ms1.dbo')` → entity `ms1_dbo_<table>`).
- **Safe by default**: nothing is exposed until you whitelist it, only `GET` by default
  is served, and `odata_serve()` turns on bearer-token auth automatically.
- **Column policy**: `odata_expose(..., columns := [...])` restricts the
  visible metadata and every query expression to the listed fields.
- Tables without a primary key can still be exposed; add
  `CALL odata_entity('msxlsx', key := 'col')` to enable `(key)` lookups.

## Basic writes

Set `read_only := false` in `odata_serve` to enable POST (201 + Location),
PATCH and DELETE (204). The default remains read-only. Writes use Bearer auth,
the existing entity/column allow-lists and per-request transactions. See
[write API and examples](docs/writes.md) for input types, errors and limitations.

## Feature scope

See [docs/architecture.md](docs/architecture.md), [docs/odata-support.md](docs/odata-support.md)
and [docs/security.md](docs/security.md). The server defaults to **read-only GET**
with bearer authentication, column-level allow-lists, resource caps and
optional server-driven pagination; basic writes can be enabled explicitly. `$expand`, opaque paging
tokens and the Quack gateway executor are staged for later versions.

## Building

See [docs/architecture.md](docs/architecture.md#building) for the local
development workflow.

## License

MIT — see [LICENSE](LICENSE).
