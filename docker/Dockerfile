ARG BASE_IMAGE=debian:bookworm-slim
FROM ${BASE_IMAGE}

ARG DUCKDB_VERSION
ARG TARGETARCH

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl socat tar \
    && rm -rf /var/lib/apt/lists/*

RUN case "${TARGETARCH}" in \
        amd64|arm64) cli_arch="${TARGETARCH}" ;; \
        *) echo "Unsupported architecture: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && curl --fail --location --silent --show-error \
        "https://github.com/duckdb/duckdb/releases/download/v${DUCKDB_VERSION}/duckdb-cli-linux-${cli_arch}.tar.gz" \
        --output /tmp/duckdb.tar.gz \
    && tar -xzf /tmp/duckdb.tar.gz -C /usr/local/bin duckdb \
    && rm /tmp/duckdb.tar.gz \
    && chmod +x /usr/local/bin/duckdb

RUN duckdb -unsigned -c "INSTALL postgres_scanner; INSTALL ui"

COPY container-assets/odata-v${DUCKDB_VERSION}-linux_${TARGETARCH}.duckdb_extension \
    /opt/duckdb-odata/odata.duckdb_extension
COPY docker/entrypoint.sh /usr/local/bin/duckdb-odata
COPY docker/init.sql /opt/duckdb-odata/init.sql

RUN chmod +x /usr/local/bin/duckdb-odata \
    && mkdir -p /data

ENV ODATA_DATABASE=/data/odata.duckdb
ENV ODATA_PORT=8080
ENV DUCKDB_UI_PORT=4213
ENV DUCKDB_UI_PROXY_PORT=4214

VOLUME ["/data"]
EXPOSE 8080
EXPOSE 4214

ENTRYPOINT ["/usr/local/bin/duckdb-odata"]
