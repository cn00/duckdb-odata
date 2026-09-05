#!/usr/bin/env python3
"""End-to-end smoke test for the odata extension (v0.1).

Starts a duckdb CLI (with -unsigned) that loads the built odata extension,
creates a table, exposes it and starts the OData server. Then performs HTTP
requests against the running server and prints results.
"""
import json
import subprocess
import sys
import time
import urllib.request
import urllib.error

DUCKDB = "/Users/cn/.local/bin/duckdb"
EXT = "/Users/cn/phl/duckdb-odata/.dev/duckdb/build/extension/odata/odata.duckdb_extension"
HOST = "127.0.0.1"
PORT = 18080

sql = f"""
LOAD '{EXT}';
CREATE TABLE customers (id BIGINT, name VARCHAR, active BOOLEAN);
INSERT INTO customers VALUES (1, 'Alice', true), (2, 'Bob', false), (3, 'Carol', true);
CALL odata_expose('customers');
CALL odata_entity('customers', key := 'id');
CALL odata_serve('http://{HOST}:{PORT}');
"""

proc = subprocess.Popen(
    [DUCKDB, "-unsigned"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
)

try:
    proc.stdin.write(sql)
    proc.stdin.flush()
    # CLI is now interactive; server thread lives in the same process
    time.sleep(2.0)

    def get(path):
        url = f"http://{HOST}:{PORT}{path}"
        try:
            with urllib.request.urlopen(url, timeout=10) as r:
                return r.status, json.loads(r.read().decode())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read().decode())

    status, body = get("/health")
    print("health:", status, body)

    status, body = get("/odata")
    print("service doc:", status, body)

    status, body = get("/odata/customers")
    print("customers:", status, body)

    status, body = get('/odata/customers?$filter=active%20eq%20true&$select=id,name&$orderby=id')
    print("filter/select/orderby:", status, body)

    status, body = get('/odata/customers?$count=true')
    print("count:", status, body)

    status, body = get("/odata/customers(1)")
    print("by key:", status, body)

    status, body = get("/odata/nope")
    print("missing entity:", status, body)

finally:
    if proc.stdin:
        proc.stdin.write("\nCALL odata_stop();\n.exit\n")
        proc.stdin.flush()
        proc.stdin.close()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
