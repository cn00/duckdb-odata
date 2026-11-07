#!/bin/sh
set -eu

database="${ODATA_DATABASE:-/data/odata.duckdb}"
port="${ODATA_PORT:-8080}"
token="${ODATA_TOKEN:-}"
init_sql="${ODATA_INIT_SQL:-}"
ui_port="${DUCKDB_UI_PORT:-4213}"
ui_proxy_port="${DUCKDB_UI_PROXY_PORT:-4214}"

validate_port() {
    port_name=$1
    port_value=$2

    case "${port_value}" in
        ''|*[!0-9]*)
        echo "${port_name} must be a numeric TCP port" >&2
        exit 1
        ;;
    esac
}

validate_port ODATA_PORT "${port}"
validate_port DUCKDB_UI_PORT "${ui_port}"
validate_port DUCKDB_UI_PROXY_PORT "${ui_proxy_port}"

if [ "${ui_port}" = "${ui_proxy_port}" ]; then
    echo "DUCKDB_UI_PORT and DUCKDB_UI_PROXY_PORT must differ" >&2
    exit 1
fi

if [ -z "${token}" ]; then
    echo "ODATA_TOKEN must be set" >&2
    exit 1
fi

if [ -n "${init_sql}" ] && [ ! -r "${init_sql}" ]; then
    echo "ODATA_INIT_SQL is not readable: ${init_sql}" >&2
    exit 1
fi

escaped_token=$(printf '%s' "${token}" | sed "s/'/''/g")

socat "TCP-LISTEN:${ui_proxy_port},fork,reuseaddr,bind=0.0.0.0" \
    "TCP6:[::1]:${ui_port}" &
ui_proxy_pid=$!
trap 'kill "${ui_proxy_pid}" >/dev/null 2>&1 || true' EXIT HUP INT TERM

{
    printf "LOAD '/opt/duckdb-odata/odata.duckdb_extension';\n"
    printf "LOAD ui;\n"
    printf "SET ui_local_port = %s;\n" "${ui_port}"
    printf "SELECT * FROM start_ui_server();\n"
    if [ -n "${init_sql}" ]; then
        cat "${init_sql}"
    else
        cat /opt/duckdb-odata/init.sql
    fi
    printf "CALL odata_serve('http://0.0.0.0:%s', token := '%s');\n" "${port}" "${escaped_token}"
    tail -f /dev/null
} | duckdb -unsigned "${database}"
