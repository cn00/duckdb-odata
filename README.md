# duckdb-odata

OData v4 server as a DuckDB extension — serve any exposed DuckDB table as an
OData EntitySet with `$select`, `$filter`, `$orderby`, `$top`, `$skip` and
`$count`, plus an auto-generated `$metadata` (EDMX) from the live DuckDB
catalog.

Design document: [docs/design.md](docs/design.md)

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
CALL odata_serve('http://0.0.0.0:8080');
```

```bash
curl http://localhost:8080/odata/$metadata
curl http://localhost:8080/odata/customers
curl 'http://localhost:8080/odata/customers?$filter=active%20eq%20true&$top=10'
curl http://localhost:8080/odata/customers(1)
```

## Feature scope

See [docs/architecture.md](docs/architecture.md), [docs/odata-support.md](docs/odata-support.md)
and [docs/security.md](docs/security.md). Version 0.1 is **read-only GET**
with the query options listed above; write support, `$expand`, pagination
tokens and the Quack gateway executor are staged for later versions.

## Building

See [docs/architecture.md](docs/architecture.md#building) for the local
development workflow.

## License

MIT — see [LICENSE](LICENSE).
