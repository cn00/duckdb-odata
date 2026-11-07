#!/bin/sh
set -eu

database="${ODATA_DATABASE:-/tmp/odata.duckdb}"
port="${ODATA_PORT:-8080}"
token="${ODATA_TOKEN:-}"
init_sql="${ODATA_INIT_SQL:-}"
init_sql_file=/opt/duckdb-odata/init.sql
temporary_init_sql=
log_dir=
log_pipe=
logger_pid=

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
log_dir=$(mktemp -d /tmp/odata-duckdb-log.XXXXXX)
log_pipe=${log_dir}/output
mkfifo "${log_pipe}"

forward_duckdb_logs() {
    while IFS= read -r line || [ -n "${line}" ]; do
        printf '[duckdb] %s\n' "${line}"
    done < "${log_pipe}"
}

cleanup() {
    rm -f "${temporary_init_sql}" "${log_pipe}"
    rmdir "${log_dir}" 2>/dev/null || true
    if [ -n "${logger_pid}" ]; then
        kill "${logger_pid}" >/dev/null 2>&1 || true
    fi
}

forward_duckdb_logs &
logger_pid=$!
trap cleanup EXIT HUP INT TERM

{
    printf "LOAD '/opt/duckdb-odata/odata.duckdb_extension';\n"
    printf "LOAD '/opt/duckdb/extensions/httpfs.duckdb_extension';\n"
    cat "${init_sql_file}"
    printf "\n.output /dev/null\n"
    printf "CALL odata_serve('http://0.0.0.0:%s', token := '%s');\n" "${port}" "${escaped_token}"
    printf ".output stdout\n"
    tail -f /dev/null
} | stdbuf -oL -eL duckdb -unsigned "${database}" > "${log_pipe}" 2>&1
duckdb_status=$?
wait "${logger_pid}" || true
exit "${duckdb_status}"
