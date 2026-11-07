#!/bin/sh
set -eu

database="${ODATA_DATABASE:-/tmp/odata.duckdb}"
port="${ODATA_PORT:-8080}"
token="${ODATA_TOKEN:-}"
init_sql="${ODATA_INIT_SQL:-}"
init_sql_file=/opt/duckdb-odata/init.sql
temporary_init_sql=

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

if [ -n "${init_sql}" ]; then
    temporary_init_sql=$(mktemp /tmp/odata-init.XXXXXX.sql)
    if ! printf '%s' "${init_sql}" | base64 -d > "${temporary_init_sql}"; then
        rm -f "${temporary_init_sql}"
        echo "ODATA_INIT_SQL must contain Base64-encoded SQL" >&2
        exit 1
    fi
    init_sql_file=${temporary_init_sql}
fi

escaped_token=$(printf '%s' "${token}" | sed "s/'/''/g")
trap 'rm -f "${temporary_init_sql}"' EXIT

{
    printf "LOAD '/opt/duckdb-odata/odata.duckdb_extension';\n"
    printf "LOAD httpfs;\n"
    cat "${init_sql_file}"
    printf "CALL odata_serve('http://0.0.0.0:%s', token := '%s');\n" "${port}" "${escaped_token}"
    tail -f /dev/null
} | duckdb -unsigned "${database}"
