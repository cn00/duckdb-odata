#!/bin/sh
set -eu

: "${ODATA_VERSION:?Set ODATA_VERSION to an OData release tag, for example v0.1.1}"

aws_region="${AWS_REGION:-us-east-1}"
ecr_repository="${ECR_REPOSITORY:-duckdb-odata}"
duckdb_version="${DUCKDB_VERSION:-2.0.0}"
base_image="${BASE_IMAGE:-public.ecr.aws/ubuntu/ubuntu:latest}"
image_tag="${IMAGE_TAG:-lambda-${ODATA_VERSION}}"
artifact="container-assets/odata-${ODATA_VERSION}-linux_amd64.duckdb_extension"

if [ ! -f "${artifact}" ]; then
    echo "Missing staged Lambda extension: ${artifact}" >&2
    exit 1
fi

account_id=$(aws sts get-caller-identity --query Account --output text)
registry="${account_id}.dkr.ecr.${aws_region}.amazonaws.com"
image="${registry}/${ecr_repository}:${image_tag}"

docker buildx build \
    --platform linux/amd64 \
    --provenance=false \
    --sbom=false \
    --load \
    --build-arg "BASE_IMAGE=${base_image}" \
    --build-arg "DUCKDB_VERSION=${duckdb_version}" \
    --build-arg "ODATA_VERSION=${ODATA_VERSION}" \
    --tag "${image}" \
    --file docker/Dockerfile.lambda \
    .

aws ecr get-login-password --region "${aws_region}" \
    | docker login --username AWS --password-stdin "${registry}"
docker push "${image}"

if ! docker buildx imagetools inspect --raw "${image}" \
    | grep -Eq '"mediaType"[[:space:]]*:[[:space:]]*"application/vnd\.docker\.distribution\.manifest\.v2\+json"'; then
    echo "ECR image is not a single Docker schema2 manifest: ${image}" >&2
    exit 1
fi

printf '%s\n' "${image}"
