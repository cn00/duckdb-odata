#!/bin/sh
set -eu

database="${ODATA_DATABASE:-/data/odata.duckdb}"
port="${ODATA_PORT:-8080}"
token="${ODATA_TOKEN:-}"
init_sql="${ODATA_INIT_SQL:-}"

case "${port}" in
    ''|*[!0-9]*)
        echo "ODATA_PORT must be a numeric TCP port" >&2
        exit 1
        ;;
esac

if [ -z "${token}" ]; then
    echo "ODATA_TOKEN must be set" >&2
    exit 1
fi

if [ -n "${init_sql}" ] && [ ! -r "${init_sql}" ]; then
    echo "ODATA_INIT_SQL is not readable: ${init_sql}" >&2
    exit 1
fi

escaped_token=$(printf '%s' "${token}" | sed "s/'/''/g")

{
    printf "LOAD '/opt/duckdb-odata/odata.duckdb_extension';\n"
    if [ -n "${init_sql}" ]; then
        cat "${init_sql}"
    else
        cat /opt/duckdb-odata/init.sql
    fi
    printf "CALL odata_serve('http://0.0.0.0:%s', token := '%s');\n" "${port}" "${escaped_token}"
    tail -f /dev/null
} | duckdb -unsigned "${database}"
