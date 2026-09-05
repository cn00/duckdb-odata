-- duckdb-odata SQL-level smoke script (run against a duckdb CLI with the
-- extension loaded, e.g.:  duckdb -unsigned < test/sql/server.sql)
--
-- The sqllogictest flavor of these checks lives in the CI workflow; this file
-- is kept runnable by a plain CLI for quick manual verification.

-- LOAD '/absolute/path/to/odata.duckdb_extension';

CREATE TABLE IF NOT EXISTS t_serve (id BIGINT, name VARCHAR);
DELETE FROM t_serve;
INSERT INTO t_serve VALUES (1, 'a'), (2, 'b');

CALL odata_expose('t_serve');                       -- register entity set
-- CALL odata_expose('missing_table_xyz');           -- expect: error
CALL odata_entity('t_serve', key := 'id');          -- configure key

SELECT running, address, base_path, token_required, exposed_entities
FROM odata_status();                                -- expect 1 exposed entity

CALL odata_serve('http://127.0.0.1:18099');         -- start server
SELECT running FROM odata_status();                 -- expect true

-- meanwhile: curl http://127.0.0.1:18099/odata/t_serve

CALL odata_stop();                                  -- stop server
SELECT running FROM odata_status();                 -- expect false
