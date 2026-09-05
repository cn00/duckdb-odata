#!/usr/bin/env bash
# Fetch a DuckDB source tree used for local extension development.
#
# Usage: scripts/fetch_duckdb.sh [version] [target-dir]
#   version    e.g. v1.5.5 (must match the duckdb CLI you intend to LOAD into)
#   target-dir default: <repo>/.dev/duckdb
set -euo pipefail

VERSION="${1:-v1.5.5}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${2:-$REPO_DIR/.dev/duckdb}"

if [ -f "$TARGET/CMakeLists.txt" ]; then
	echo "duckdb source already present at $TARGET"
	exit 0
fi

mkdir -p "$(dirname "$TARGET")"
echo "Downloading duckdb ${VERSION} sources..."
curl -fL --retry 3 -o /tmp/duckdb-${VERSION}.tar.gz \
	"https://github.com/duckdb/duckdb/archive/refs/tags/${VERSION}.tar.gz"
rm -rf "$TARGET"
mkdir -p "$TARGET"
tar -xzf /tmp/duckdb-${VERSION}.tar.gz -C "$(dirname "$TARGET")"
mv "$(dirname "$TARGET")/duckdb-${VERSION#v}" "$TARGET"
rm -f /tmp/duckdb-${VERSION}.tar.gz
echo "duckdb ${VERSION} ready at $TARGET"
