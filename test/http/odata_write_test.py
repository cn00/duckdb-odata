#!/usr/bin/env python3
"""HTTP write integration tests; same CLI arguments as odata_http_test.py."""
import contextlib
import http.client
import json
import socket
import subprocess
import time
from odata_http_test import DUCKDB, EXT


@contextlib.contextmanager
def server(read_only=True):
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    proc = subprocess.Popen([DUCKDB, "-unsigned"], stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    sql = f"""
LOAD '{EXT}';
CREATE SEQUENCE ids;
CREATE TABLE items (id BIGINT PRIMARY KEY DEFAULT nextval('ids'), name VARCHAR NOT NULL,
                    active BOOLEAN DEFAULT true, amount DECIMAL(18,2), secret VARCHAR DEFAULT 'hidden');
CALL odata_expose('items', columns := ['id','name','active','amount']);
CREATE TABLE duplicates (id INTEGER, name VARCHAR);
INSERT INTO duplicates VALUES (1, 'a'), (1, 'b');
CALL odata_expose('duplicates');
CREATE TABLE strings (id VARCHAR PRIMARY KEY, name VARCHAR);
CALL odata_expose('strings');
CREATE TABLE hidden_key (id INTEGER PRIMARY KEY, name VARCHAR);
CALL odata_expose('hidden_key', columns := ['name']);
CREATE TABLE defaults_only (id INTEGER PRIMARY KEY DEFAULT 10, name VARCHAR DEFAULT 'default');
CALL odata_expose('defaults_only');
CREATE SCHEMA extra;
CREATE TABLE extra.typed (id BIGINT PRIMARY KEY, date_value DATE);
CALL odata_expose('extra.typed');
CALL odata_serve('http://127.0.0.1:{port}', token := 'secret', read_only := {str(read_only).lower()});
"""
    proc.stdin.write(sql)
    proc.stdin.flush()
    try:
        deadline = time.monotonic() + 20
        while True:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=.2):
                    break
            except OSError:
                if proc.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("server did not start")
                time.sleep(.1)
        yield port
    finally:
        if proc.poll() is None:
            proc.stdin.write("CALL odata_stop();\n.exit\n")
            proc.stdin.flush()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        errors = proc.stderr.read()
        if errors:
            print(errors)


def request(port, method, path, data=None, token="secret", content_type="application/json", raw=None, extra_headers=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    body = raw if raw is not None else (json.dumps(data).encode() if data is not None else None)
    headers = {"Content-Type": content_type}
    if token:
        headers["Authorization"] = "Bearer " + token
    headers.update(extra_headers or {})
    conn.request(method, path, body, headers)
    response = conn.getresponse()
    result = response.status, dict(response.getheaders()), response.read()
    conn.close()
    return result


def expect(port, method, path, status, **kwargs):
    result = request(port, method, path, **kwargs)
    assert result[0] == status, (method, path, status, result)
    if status == 204:
        assert result[2] == b""
        assert "Content-Length" not in result[1]
    print("ok", method, path, status)
    return result


def main():
    with server() as port:
        for method in ("POST", "PATCH", "DELETE"):
            expect(port, method, "/odata/items", 405, data={"name": "blocked"})
    with server(False) as port:
        expect(port, "POST", "/odata/items", 401, data={"name": "blocked"}, token="wrong")
        expect(port, "POST", "/odata/unknown", 404, data={})
        expect(port, "PUT", "/odata/items(1)", 405, data={})
        expect(port, "POST", "/health", 405, data={})
        expect(port, "POST", "/odata/hidden_key", 400, data={"name": "blocked"})
        expect(port, "POST", "/odata/defaults_only", 201, data={})
        expect(port, "POST", "/odata/extra_typed", 201, data={"id": 9007199254740993, "date_value": "2026-09-10"})
        typed = json.loads(expect(port, "GET", "/odata/extra_typed(9007199254740993)", 200)[2])
        assert typed["id"] == 9007199254740993 and typed["date_value"] == "2026-09-10"
        expect(port, "DELETE", "/odata/extra_typed(9007199254740993)", 204)
        _, headers, body = expect(port, "POST", "/odata/items", 201, data={"name": "O'Brien 中文", "amount": "12.34"})
        row = json.loads(body)
        assert row == {"id": 1, "name": "O'Brien 中文", "active": True, "amount": 12.34}, row
        location = headers["Location"]
        expect(port, "GET", location, 200)
        expect(port, "PATCH", "/odata/items(1)", 204, data={"name": "changed", "active": False, "amount": None})
        row = json.loads(expect(port, "GET", location, 200)[2])
        assert row["name"] == "changed" and row["active"] is False and row["amount"] is None
        for payload in ({"secret": "leak"}, {"unknown": 1}, {"name": None}, {"amount": {}}, {"amount": "bad"}):
            expect(port, "PATCH", "/odata/items(1)", 400, data=payload)
        expect(port, "PATCH", "/odata/items(1)", 400, data={"id": 9})
        expect(port, "PATCH", "/odata/items(1)", 400, data={})
        expect(port, "PATCH", "/odata/items(1)?$filter=id%20eq%201", 400, data={"name": "wrong"})
        expect(port, "PATCH", "/odata/items(1)", 400, data={"name": "wrong"}, extra_headers={"If-Match": "*"})
        expect(port, "PATCH", "/odata/items(1)", 400, data={"name": "wrong", "amount": "bad"})
        assert json.loads(expect(port, "GET", location, 200)[2])["name"] == "changed"
        expect(port, "PATCH", "/odata/items(1)/garbage", 400, data={"name": "wrong"})
        expect(port, "PATCH", "/odata/items", 400, data={"name": "wrong"})
        expect(port, "POST", "/odata/items(1)", 400, data={"name": "wrong"})
        expect(port, "POST", "/odata/items", 409, data={"id": 1, "name": "duplicate"})
        for raw in (b'{', b'[]', b'null', b'{"name":"a","name":"b"}', b'{"name":"a"} trailing'):
            expect(port, "POST", "/odata/items", 400, raw=raw)
        expect(port, "POST", "/odata/items", 415, data={}, content_type="text/plain")
        # Fragmented payload exercises body reads beyond the request-head packet.
        body = b'{"name":"fragmented"}'
        with socket.create_connection(("127.0.0.1", port)) as sock:
            sock.sendall(("POST /odata/items HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer secret\r\n"
                          "Content-Type: application/json\r\nContent-Length: " + str(len(body)) + "\r\n\r\n").encode())
            time.sleep(.05)
            sock.sendall(body[:5])
            time.sleep(.05)
            sock.sendall(body[5:])
            response = http.client.HTTPResponse(sock)
            response.begin()
            assert response.status == 201, response.read()
            created = json.loads(response.read())
        expect(port, "DELETE", f'/odata/items({created["id"]})', 204)
        for framing in ("Content-Length: 1\r\nContent-Length: 2", "Transfer-Encoding: chunked", "Content-Length: -1"):
            with socket.create_connection(("127.0.0.1", port)) as sock:
                sock.sendall(("POST /odata/items HTTP/1.1\r\nHost: localhost\r\n" + framing + "\r\n\r\n").encode())
                response = http.client.HTTPResponse(sock)
                response.begin()
                assert response.status == 400
                response.read()
        expect(port, "POST", "/odata/items", 413, raw=b'x' * (1048576 + 1))
        expect(port, "PATCH", "/odata/duplicates(1)", 409, data={"name": "bad"})
        expect(port, "DELETE", "/odata/duplicates(1)", 409)
        rows = json.loads(expect(port, "GET", "/odata/duplicates", 200)[2])["value"]
        assert [r["name"] for r in rows] == ["a", "b"], rows
        expect(port, "PATCH", "/odata/items(999)", 404, data={"name": "missing"})
        expect(port, "DELETE", "/odata/items(999)", 404)
        expect(port, "DELETE", "/odata/items(1)", 204)
        expect(port, "GET", location, 404)
        injection = "x'); DELETE FROM items; --"
        expect(port, "POST", "/odata/items", 201, data={"name": injection})
        assert json.loads(expect(port, "GET", "/odata/items", 200)[2])["value"][0]["name"] == injection
        _, headers, _ = expect(port, "POST", "/odata/strings", 201, data={"id": "a%27+中文", "name": "s"})
        expect(port, "GET", headers["Location"], 200)
        expect(port, "PATCH", headers["Location"], 204, data={"name": "updated"})
        expect(port, "DELETE", headers["Location"], 204)
        _, headers, _ = expect(port, "POST", "/odata/strings", 201, data={"id": "O'Brien)", "name": "quoted key"})
        expect(port, "GET", headers["Location"], 200)
        expect(port, "PATCH", "/odata/strings('O''Brien)')", 204, data={"name": "updated"})
        expect(port, "DELETE", "/odata/strings('O''Brien)')", 204)
    print("all write tests passed")


if __name__ == "__main__":
    main()
