# AWS Lambda Web Adapter

`docker/Dockerfile.lambda` packages the OData HTTP server behind the
[AWS Lambda Web Adapter](https://github.com/awslabs/aws-lambda-web-adapter).
It is a separate image from the long-running Compose/GHCR image: it does not
start DuckDB UI, a local PostgreSQL container, or a TCP proxy.

The Lambda image is suitable for read-heavy OData APIs. It keeps the existing
OData HTTP protocol and bearer-token policy, while the adapter translates
Lambda Function URL, API Gateway HTTP API, or ALB invocations to port 8080.

## Build locally

Stage a matching Linux extension artifact under its OData release tag:

```bash
mkdir -p container-assets
gh release download v0.1.1 \
  --repo cn00/duckdb-odata \
  --pattern 'odata-v0.1.1-linux_amd64.duckdb_extension.gz' \
  --dir container-assets
gzip -dc container-assets/odata-v0.1.1-linux_amd64.duckdb_extension.gz \
  > container-assets/odata-v0.1.1-linux_amd64.duckdb_extension

docker buildx build --platform linux/amd64 --load \
  --build-arg BASE_IMAGE=public.ecr.aws/ubuntu/ubuntu:latest \
  --build-arg DUCKDB_VERSION=2.0.0 \
  --build-arg ODATA_VERSION=v0.1.1 \
  --tag duckdb-odata-lambda:local \
  --file docker/Dockerfile.lambda .
```

Run the web server directly to smoke-test the image:

```bash
docker run --rm -p 8080:8080 \
  -e ODATA_TOKEN=local-test-token \
  duckdb-odata-lambda:local

curl -H 'Authorization: Bearer local-test-token' \
  http://localhost:8080/health
curl -H 'Authorization: Bearer local-test-token' \
  http://localhost:8080/odata/lambda_customers
```

The adapter needs no special local command for this HTTP smoke test. In AWS,
it detects the Lambda runtime environment and bridges invocations to the same
listener. Use the Lambda Runtime Interface Emulator or SAM for an
invocation-level integration test before deployment.

## Lambda configuration

Configure the function with an `x86_64` image for the first deployment, at
least 2 GiB of memory, and a 60-second timeout. The image configures:

| Variable | Value | Purpose |
| --- | --- | --- |
| `AWS_LWA_PORT` | `8080` | OData listener port |
| `AWS_LWA_READINESS_CHECK_PATH` | `/health` | waits until OData has loaded |
| `AWS_LWA_READINESS_CHECK_TIMEOUT_SECONDS` | `60` | bounds a failed initialization |
| `AWS_LWA_INVOKE_MODE` | `buffered` | matches current fully buffered OData responses |

Set `ODATA_TOKEN` from AWS Secrets Manager. Do not put it in the image,
Dockerfile, or initialization SQL.

For production, push the image to Amazon ECR in the target Region and expose
it through a Function URL with `AWS_IAM` authorization or API Gateway HTTP
API. The Lambda service cannot directly deploy the GHCR image.

Lambda does not accept a Buildx multi-platform image index or an image index
that includes provenance attestations. Use the included publisher to build one
`linux/amd64` image, disable provenance and SBOM attestations, then push a
single Docker schema2 manifest:

```bash
export ODATA_VERSION=v0.1.1
export AWS_REGION=us-east-1
export ECR_REPOSITORY=duckdb-odata
./docker/push-lambda-ecr.sh
```

The script requires configured AWS credentials with access to ECR. It prints
the image URI only after verifying the pushed manifest type. Use the printed
`lambda-v0.1.1` image URI when creating the Lambda function; do not use the
multi-architecture GHCR image or an ECR tag produced with Buildx `--push`.

## Data sources and writes

The default database path is `/tmp/odata.duckdb`, which is ephemeral and
should be treated as a demo or cache. Use `ODATA_INIT_SQL` to provide a
deployment-specific initialization script. Production read paths should expose
S3-backed files, a read-only RDS/Aurora catalog, or an EFS-backed DuckDB file.

Keep `odata_serve` read-only for the first Lambda deployment. If writes are
required, use a database designed for concurrent writes (for example RDS) or
set Lambda reserved concurrency to one before enabling OData writes. Do not
allow several Lambda execution environments to write the same DuckDB file.
