FROM debian:bookworm-slim

ARG DUCKDB_VERSION
ARG TARGETARCH

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl tar \
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

COPY container-assets/odata-v${DUCKDB_VERSION}-linux_${TARGETARCH}.duckdb_extension \
    /opt/duckdb-odata/odata.duckdb_extension
COPY docker/entrypoint.sh /usr/local/bin/duckdb-odata
COPY docker/init.sql /opt/duckdb-odata/init.sql

RUN chmod +x /usr/local/bin/duckdb-odata \
    && mkdir -p /data

ENV ODATA_DATABASE=/data/odata.duckdb
ENV ODATA_PORT=8080

VOLUME ["/data"]
EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/duckdb-odata"]
