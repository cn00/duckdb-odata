# duckdb-odata

> English | [中文版](README.zh.md)

OData v4 server as a DuckDB extension — serve any exposed DuckDB table as an
OData EntitySet with `$select`, `$filter`, `$orderby`, `$top`, `$skip` and
`$count`, plus an auto-generated `$metadata` (EDMX) from the live DuckDB
catalog.

Architecture: [docs/architecture.md](docs/architecture.md)

```
DuckDB ── odata extension ──► HTTP / OData v4 ──► BI / ERP / Power BI / Excel / REST clients
```

## Quick start (v0.1)

```sql
INSTALL odata;
LOAD odata;

CREATE TABLE customers (id BIGINT, name VARCHAR, active BOOLEAN);
CALL odata_expose('customers');
CALL odata_entity('customers', key := 'id');
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

### PostgreSQL

Run this in a fresh DuckDB session started with `duckdb -unsigned` when loading
an unsigned OData binary. Replace the connection values and extension path;
the `postgres` connector must be available for your DuckDB version/platform.

```sql
INSTALL postgres;
LOAD postgres;
ATTACH 'host=localhost port=5432 dbname=app user=odata_reader password=REPLACE_ME'
    AS pg (TYPE postgres, READ_ONLY);

LOAD '/path/to/odata.duckdb_extension';
CALL odata_expose_schema('pg.public');
FROM odata_serve();
```

For example, `pg.public.customers` is exposed as `pg_public_customers` when
`pg` is not the current catalog. Use the returned `entity` / `table` rows as
the authoritative mapping.

### Microsoft SQL Server: expose two databases

The community `mssql` connector attaches each database as a separate catalog.
Run this in a fresh session (`duckdb -unsigned` for an unsigned OData binary):

```sql
INSTALL mssql FROM community;
LOAD mssql;
ATTACH 'Server=$MSSQL_HOST,$MSSQL_PORT;Database=$MSSQL_DATABASE;User Id=$MSSQL_USER;Password=$MSSQL_PASSWORD;TrustServerCertificate=yes'
    AS mdm (TYPE mssql);
ATTACH 'Server=$MSSQL_HOST,$MSSQL_PORT;Database=$MSSQL_DATABASE2;User Id=$MSSQL_USER;Password=$MSSQL_PASSWORD;TrustServerCertificate=yes'
    AS isales (TYPE mssql);

LOAD '/path/to/odata.duckdb_extension';
CALL odata_expose_schema('mdm.dbo');
FROM odata_expose_schema('isales.dbo');
FROM odata_serve();
```

`$MSSQL_*` values above are placeholders: DuckDB does **not** expand environment
variables inside SQL string literals. Substitute them securely in your SQL
client before executing, replace the extension path, and do not commit real
credentials. Use a database account with only the required read permissions.
`TrustServerCertificate=yes` skips server certificate validation; use a trusted
certificate and remove that override for production. Check connector availability
for your DuckDB version and platform before installing.

Without changing the current catalog/schema, a table `mdm.dbo.customers`
maps to `mdm_dbo_customers`, and `isales.dbo.orders` to `isales_dbo_orders`.
`CALL` and `FROM` both execute these table functions and return their result
rows. Both databases share the same OData endpoint; no data copy is needed.

### Access the endpoint

In either example, `odata_serve()` returns `listen_uri`, `listen_url` and
`auth_token`. Copy `listen_url` and `auth_token` into a second terminal:

```bash
export ODATA_URL='http://localhost:54321'  # replace with listen_url
export ODATA_TOKEN='REPLACE_WITH_AUTH_TOKEN'
curl -H "Authorization: Bearer $ODATA_TOKEN" "$ODATA_URL/odata/"
curl -H "Authorization: Bearer $ODATA_TOKEN" "$ODATA_URL/odata/\$metadata"
# SQL Server example; use an entity name returned by odata_expose_schema:
curl -H "Authorization: Bearer $ODATA_TOKEN" "$ODATA_URL/odata/mdm_dbo_customers?\$top=10"
```

Keep the DuckDB process and attached databases open while serving; do not use
a one-shot CLI invocation that exits immediately after running the SQL.
The examples retain localhost binding, automatic bearer auth and read-only
OData requests. Exposing a whole schema publishes all its discovered tables
and views; use `odata_expose` with a column allow-list for narrower access.
Stop the server before detaching its data sources:

```sql
CALL odata_stop();
```

Connector references: [PostgreSQL](https://duckdb.org/docs/stable/core_extensions/postgres/overview.html)
and [mssql](https://duckdb.org/community_extensions/extensions/mssql).

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

## Container quick start

Release tags publish a multi-architecture image to GitHub Container Registry:
`ghcr.io/cn00/duckdb-odata:<tag>` (`linux/amd64` and `linux/arm64`). The image
contains the DuckDB CLI and the matching static OData extension. Start the
included demo with Docker Compose:

```bash
export ODATA_TOKEN="$(openssl rand -hex 32)"
docker compose -f examples/docker-compose.yml up -d

curl -H "Authorization: Bearer $ODATA_TOKEN" \
  http://localhost:8080/odata/customers
```

By default, Compose pulls `ghcr.io/cn00/duckdb-odata:latest`, starts a
`postgres:16-alpine` demo database and exposes its `products` table as
`pg_public_products`. It persists both databases in named volumes and binds
services only to `127.0.0.1`.

The official DuckDB UI is available at <http://localhost:4213>; use this exact
`localhost` URL, not `127.0.0.1`, because the official UI validates its local
browser origin. It can inspect the OData DuckDB database and query the attached
PostgreSQL demo. The UI can execute arbitrary SQL, so it intentionally has no
public port binding and should be used only for local development. Set
`ODATA_IMAGE=ghcr.io/cn00/duckdb-odata:<tag>` to pin a published release. To
initialize your own database, mount a SQL file and set `ODATA_INIT_SQL` to its
path; it must create/expose entities, while the container loads the extension
and starts the server. Keep GHCR package visibility public for anonymous
pulls.

## Building

Initialize the official DuckDB extension-template submodules, then use its
standard Makefile:

```bash
git submodule update --init --recursive
make
make test
make test-http
```

See [docs/architecture.md](docs/architecture.md#building) for details.

Native CI/Release builds retain the official `EXTENSION_STATIC_BUILD=1`
default: they statically link DuckDB into the loadable extension for portability,
so the binary can be tens of MB. For local Linux/macOS development,
`make EXTENSION_STATIC_BUILD=0` produces a smaller binary that resolves DuckDB
symbols from the host. It requires a host exporting all needed C++ symbols;
even a matching official CLI may not export them, and `RTLD_LOCAL` embedding
can also prevent loading. The pinned DuckDB version forces static linking on
Windows. See the [linking details](docs/architecture.md#building) before choosing
a local dynamic build.

## Releasing

Push a version tag to run the official `duckdb-stable-build` distribution
pipeline and publish its extension binaries as GitHub Release assets:

```bash
git tag v0.1.0 && git push origin v0.1.0
```

After the stable build succeeds, the release job attaches
`odata-<duckdb-version>-<platform>.duckdb_extension` (or
`.duckdb_extension.wasm` for WebAssembly), plus `sha256sums.txt`.
The DuckDB version and platforms come from the official distribution pipeline;
historical DuckDB versions are no longer rebuilt by a separate matrix.

## License

MIT — see [LICENSE](LICENSE).
