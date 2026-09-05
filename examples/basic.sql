-- duckdb-odata quick start (v0.1)
-- Run with a duckdb CLI that can load unsigned extensions:
--
--   duckdb -unsigned < examples/basic.sql

LOAD '/absolute/path/to/odata.duckdb_extension';

-- demo data
CREATE TABLE customers (
    id     BIGINT,
    name   VARCHAR,
    city   VARCHAR,
    age    INTEGER,
    active BOOLEAN
);

INSERT INTO customers VALUES
    (1, 'Alice',   'Berlin', 34, true),
    (2, 'Bob',     'Paris',  22, false),
    (3, 'Carol',   'London', 41, true),
    (4, 'David',   'Berlin', 29, true),
    (5, 'Erin',    'Rome',   37, false);

-- v0.1: expose nothing by default; whitelist what you want
CALL odata_expose('customers');
CALL odata_entity('customers', key := 'id');

-- resource limits (optional)
SET odata_max_top = 10000;

-- start the server (default base path /odata)
-- (no-arg form: CALL odata_serve() binds localhost to a free port, generates a
--  bearer token and returns listen_url/auth_token — see README.md)
CALL odata_serve('http://0.0.0.0:8080', token := 'secret');

-- then, from another terminal:
--
--   curl http://localhost:8080/odata/$metadata
--   curl -H "Authorization: Bearer secret" http://localhost:8080/odata/customers
--   curl -H "Authorization: Bearer secret" \
--        'http://localhost:8080/odata/customers?$filter=active%20eq%20true&$orderby=age%20desc&$top=3'
--   curl -H "Authorization: Bearer secret" http://localhost:8080/odata/customers(1)
--
-- to stop:
--   CALL odata_stop();
