# Basic entity writes

The server defaults to read-only. Enable POST, PATCH and DELETE explicitly:

```sql
CREATE TABLE customers (id BIGINT PRIMARY KEY, name VARCHAR NOT NULL, active BOOLEAN DEFAULT true);
CALL odata_expose('customers', columns := ['id', 'name', 'active']);
CALL odata_serve('http://127.0.0.1:8080', token := 'secret', read_only := false);
```

```bash
curl -X POST -H 'Authorization: Bearer secret' -H 'Content-Type: application/json' \
  -d '{"id":1,"name":"Alice"}' 'http://127.0.0.1:8080/odata/customers'
curl -X PATCH -H 'Authorization: Bearer secret' -H 'Content-Type: application/json' \
  -d '{"name":"Updated"}' 'http://127.0.0.1:8080/odata/customers(1)'
curl -X DELETE -H 'Authorization: Bearer secret' \
  'http://127.0.0.1:8080/odata/customers(1)'
```

POST returns 201, the inserted public fields, and a relative `Location` URL.
Omitted fields use DuckDB defaults. PATCH modifies only supplied fields and
cannot change the key. PATCH and DELETE return 204 with no body; missing keys
return 404. The `/entity/key` route is also supported.

Every write uses a prepared statement with bound values and explicit casts to
the catalog column types. Strings (including numeric or date strings), numbers,
booleans and nullable values are supported. Objects, arrays and BLOB writes are
not supported. Unknown/hidden fields, duplicate JSON properties, invalid JSON,
invalid casts and malformed requests return 400; wrong content type returns 415;
constraint conflicts return 409. Each write runs in a transaction. A non-unique
key matching multiple rows causes rollback and 409. Use a single-column PRIMARY
KEY for writeable tables; a public key is required, including for POST.

Bearer authentication and entity/column allow-lists apply before writes. The
shared server token grants both reads and writes when enabled; there is no
per-user/per-operation grant or row-level policy yet. Exposing an entity with a
hidden key disables writes to that entity. Views and foreign tables are writable
only if DuckDB and their underlying extension support the requested DML.

Bodies require Content-Length framing and are limited to 1 MiB. Incomplete bodies
time out after 10 seconds of socket inactivity; oversized bodies return 413.
Chunked request bodies and duplicate Content-Length headers are rejected.
The concurrent-query limit includes writes. Existing query-timeout and response
size settings currently apply to the read path, not write execution/responses.

PUT, `$batch`, ETag/If-Match, conditional writes, compound keys and query options
on writes are not implemented. If-Match/If-None-Match are rejected rather than
silently ignoring a caller's concurrency condition.

Run `python3 test/http/odata_write_test.py [duckdb-cli] [extension-path]` after
building. `make test-http` includes both read and write integration suites.
