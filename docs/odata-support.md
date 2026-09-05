# duckdb-odata — OData support matrix (v0.1)

What a client can rely on in the current version, and what is staged.

## Service & discovery

| Endpoint | Status |
| --- | --- |
| `GET /health` | ✅ `{"status":"ok"}` |
| `GET /odata` (service document) | ✅ lists exposed entity sets |
| `GET /odata/$metadata` | ✅ generated EDMX/CSDL XML from the live DuckDB catalog |
| `$metadata` entity types, keys, properties | ✅ scalar properties; complex types skipped with `Edm.String` fallback in XML (not projected) |

## Entity access

| Operation | Status |
| --- | --- |
| `GET /odata/{entity}` (collection) | ✅ |
| `GET /odata/{entity}({key})` single entity | ✅ single-key lookups |
| Compound keys | ⏳ v0.2 |
| Navigation properties / `$expand` | ⏳ v0.3 |
| CRUD (POST/PATCH/DELETE) | ⏳ v0.4 |

## Query options

| Option | Status | Notes |
| --- | --- | --- |
| `$select=id,name` | ✅ | validated against the catalog |
| `$filter` | ✅ | operators below |
| `$orderby=name desc,id asc` | ✅ | direction keywords `asc`/`desc` |
| `$top=n` | ✅ | capped by `odata_max_top` |
| `$skip=n` | ✅ | |
| `$count=true` | ✅ | runs a second `COUNT(*)` query (MVP, documented) |
| `$format=json` | ✅ tolerated | others rejected with 400 |
| `$expand`, `$search`, `$apply`, `$skiptoken`, … | ❌ 400 | staged |
| `@odata.nextLink` pagination | ⏳ v0.2 |

## `$filter` grammar

v0.1 comparisons: `eq ne gt ge lt le`, boolean `and or not`, parentheses,
plus function-call syntax resolved against a small whitelist:

`contains` `startswith` `endswith` `tolower` `toupper` `length` `substring`
`concat` `indexof` `trim` `year` `month` `day` `hour` `minute` `second` `now`
`round` `floor` `ceiling` `abs`

Examples:

```
$filter=age gt 30
$filter=active eq true and (city eq 'Berlin' or city eq 'Paris')
$filter=contains(name,'Ali')
$filter=not (total lt 100)
```

Literals: integers, floats, `'single-quoted strings'`, `true/false`, `null`
(`eq null` compiles to `IS NULL`; `ne null` to `IS NOT NULL`). String literals
compared against numeric/date columns are cast explicitly so DuckDB binds the
comparison correctly.

Anything outside this grammar is rejected with 400 — no expression text is
ever concatenated into SQL.

## Types

DuckDB → EDM mapping (see `src/metadata/edm_model.*`):

| DuckDB | EDM |
| --- | --- |
| BOOLEAN | Edm.Boolean |
| TINYINT | Edm.SByte |
| SMALLINT | Edm.Int16 |
| INTEGER | Edm.Int32 |
| BIGINT | Edm.Int64 |
| HUGEINT/unsigned ints | Edm.Decimal (raw digits) |
| FLOAT/REAL | Edm.Single |
| DOUBLE | Edm.Double |
| DECIMAL/NUMERIC | Edm.Decimal |
| VARCHAR/TEXT/CHAR | Edm.String |
| DATE | Edm.Date |
| TIME | Edm.TimeOfDay |
| TIMESTAMP / TIMESTAMPTZ | Edm.DateTimeOffset |
| UUID | Edm.Guid |
| BLOB/BINARY | Edm.Binary (base64) |
| LIST/STRUCT/MAP/UNION/JSON/… | not projected in v0.1 |

## Conventions

- Entity set name defaults to the DuckDB table name; keys come from
  `odata_entity(...)` or the catalog (single PK), falling back to the first
  column for keyless tables.
- Schema-qualified exposure: `CALL odata_expose('sales.customers')` registers
  the table from schema `sales` under the public entity-set name
  `sales_customers`, i.e. `GET /odata/sales_customers`. Unqualified names
  resolve in the current schema, or in the single schema that owns the table;
  if the same table name exists in several schemas, `odata_expose` raises an
  ambiguity error asking for `schema.table`.
- `CALL odata_expose_schema('sales')` exposes every base table of that schema
  in the current catalog (public names stay bare; schema is retained for
  resolution).
- Exposed table names are matched case-insensitively; responses echo the
  catalog's canonical casing.
- JSON follows OData v4 minimal metadata: `@odata.context`,
  `@odata.count` when `$count=true`, `value` arrays, OData error payloads
  `{"error":{"code","message"}}`.

## Known v0.1 limitations

- responses are fully buffered before sending (streaming staged for v0.2);
- `odata_query_timeout_ms` is accepted as a setting but not yet enforced;
- `odata_max_response_bytes` is enforced as a guardrail, not a hard cap;
- non-scalar columns are not exposed.
