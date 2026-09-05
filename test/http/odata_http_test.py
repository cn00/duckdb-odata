#!/usr/bin/env python3
"""End-to-end HTTP test for the odata extension.

Usage:
    python3 test/http/odata_http_test.py [path/to/duckdb] [path/to/odata.duckdb_extension]

Starts duckdb, exposes a demo table, starts the OData server, then asserts on
the JSON/XML responses. Requires a `duckdb` binary able to load the extension
(run the CLI with `-unsigned`).
"""
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def resolve_duckdb():
    # argv -> DUCKDB env -> PATH
    if len(sys.argv) > 1:
        return sys.argv[1]
    env = os.environ.get("DUCKDB")
    if env:
        return env
    return shutil.which("duckdb")


def resolve_extension():
    if len(sys.argv) > 2:
        return sys.argv[2]
    return os.path.join(REPO, ".dev", "duckdb", "build", "extension", "odata", "odata.duckdb_extension")


DUCKDB = resolve_duckdb()
EXT = resolve_extension()
HOST = os.environ.get("ODATA_TEST_HOST", "127.0.0.1")
PORT = int(os.environ.get("ODATA_TEST_PORT", "18080"))
BASE = f"http://{HOST}:{PORT}"

failures = []


def check(name, cond, detail=""):
    status = "ok" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" -- {detail}" if detail and not cond else ""))
    if not cond:
        failures.append(name)


def get(path, token=None):
    req = urllib.request.Request(BASE + path)
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def wait_for_server(proc, timeout=30):
    """Poll /health until the duckdb server is listening.

    On timeout, dump the duckdb process output: silent startup failures (for
    example a version-mismatched LOAD) would otherwise surface only as
    ConnectionRefused with no diagnostics.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            return get("/health", token="secret")
        except urllib.error.URLError:
            if proc.poll() is not None:
                # process exited early: report its output
                out = proc.stdout.read() if proc.stdout else ""
                raise RuntimeError(f"duckdb exited early (rc={proc.returncode}). Output:\n{out}")
            time.sleep(0.2)
    out = proc.stdout.read() if proc.stdout else ""
    raise RuntimeError(f"timed out waiting for OData server. duckdb output:\n{out}")


def main():
    if not DUCKDB:
        print("duckdb binary not found; pass it as argv[1] or set DUCKDB env / PATH", file=sys.stderr)
        sys.exit(2)
    sql = f"""
LOAD '{EXT}';
CREATE TABLE customers (id BIGINT, name VARCHAR, active BOOLEAN);
INSERT INTO customers VALUES
  (1, 'Alice', true),
  (2, 'Bob', false),
  (3, 'Carol', true);
CALL odata_expose('customers');
CALL odata_entity('customers', key := 'id');

-- multi-schema: s1.customers is exposed as the "s1_customers" entity set
CREATE SCHEMA s1;
CREATE TABLE s1.customers (id BIGINT, name VARCHAR);
INSERT INTO s1.customers VALUES (10, 'SchemaAlice'), (11, 'SchemaBob');
CALL odata_expose('s1.customers');
CALL odata_entity('s1.customers', key := 'id');

CALL odata_serve('http://{HOST}:{PORT}', token := 'secret');
"""
    proc = subprocess.Popen([DUCKDB, "-unsigned"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    try:
        proc.stdin.write(sql)
        proc.stdin.flush()

        s, b = wait_for_server(proc)
        check("health", s == 200 and json.loads(b) == {"status": "ok"}, b)

        s, b = get("/odata", token="secret")
        doc = json.loads(b)
        check("service document lists customers", s == 200 and any(e["name"] == "customers" for e in doc["value"]), b)

        s, b = get("/odata/$metadata", token="secret")
        check("metadata is EDMX", s == 200 and "<edmx:Edmx" in b and "customers" in b and "Edm.Int64" in b, b[:100])

        s, b = get("/odata/customers", token="secret")
        doc = json.loads(b)
        check("entity set has 3 rows", s == 200 and len(doc["value"]) == 3, b)
        check("keys in row", doc["value"][0]["id"] == 1 and doc["value"][0]["name"] == "Alice", b)

        s, b = get("/odata/customers?$filter=active%20eq%20true&$select=id,name&$orderby=id%20asc", token="secret")
        doc = json.loads(b)
        check("filter+select+orderby", s == 200 and [r["id"] for r in doc["value"]] == [1, 3]
              and all(set(r) == {"id", "name"} for r in doc["value"]), b)

        s, b = get("/odata/customers?$count=true", token="secret")
        doc = json.loads(b)
        check("$count", s == 200 and doc.get("@odata.count") == 3 and len(doc["value"]) == 3, b)

        s, b = get("/odata/customers?$top=1&$skip=1", token="secret")
        doc = json.loads(b)
        check("$top/$skip", s == 200 and [r["id"] for r in doc["value"]] == [2], b)

        s, b = get("/odata/customers(2)", token="secret")
        doc = json.loads(b)
        check("key lookup", s == 200 and doc.get("id") == 2 and doc.get("name") == "Bob", b)

        s, b = get("/odata/customers(999)", token="secret")
        check("missing key -> 404", s == 404, b)

        s, b = get("/odata/nope", token="secret")
        check("unexposed entity -> 404", s == 404, b)

        s, b = get("/odata/customers?$filter=unknown%20eq%201", token="secret")
        check("unknown property -> 400", s == 400 and "unknown" in b, b)

        s, b = get("/odata/customers?$expand=customer", token="secret")
        check("unsupported option -> 400", s == 400, b)

        s, b = get("/odata/customers", token="wrong")
        check("bad token -> 401", s == 401, b)

        s, b = get("/odata/customers")
        check("no token -> 401", s == 401, b)

        # multi-schema checks
        s, b = get("/odata/s1_customers", token="secret")
        doc = json.loads(b)
        check("schema-qualified entity set", s == 200 and [r["id"] for r in doc["value"]] == [10, 11], b)

        s, b = get("/odata/s1_customers(10)", token="secret")
        check("schema-qualified key lookup", s == 200 and json.loads(b).get("name") == "SchemaAlice", b)

        s, b = get("/odata/$metadata", token="secret")
        check("metadata has schema-qualified entity", s == 200 and "s1_customers" in b and "customers" in b, b[:200])
    finally:
        try:
            if proc.stdin:
                proc.stdin.write("\nCALL odata_stop();\n.exit\n")
                proc.stdin.flush()
                proc.stdin.close()
        except (BrokenPipeError, OSError):
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    if failures:
        print(f"\n{len(failures)} FAILURES")
        sys.exit(1)
    print("\nall ok")


if __name__ == "__main__":
    main()
