#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DOCKER_DIR="${SCRIPT_DIR}/../docker"

BUILD_TYPE="${BUILD_TYPE:-debug}"
UNITTEST="${UNITTEST:-${REPO_DIR}/build/${BUILD_TYPE}/test/unittest}"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_container_overflow=0}"

if [ ! -f "${UNITTEST}" ]; then
    echo "ERROR: DuckDB unittest runner not found at ${UNITTEST}"
    echo "Run 'GEN=ninja make ${BUILD_TYPE}' first."
    exit 1
fi

echo "=== Starting Docker services ==="
docker compose -f "${DOCKER_DIR}/docker-compose.yml" up -d

cleanup() {
    echo ""
    echo "=== Stopping Docker services ==="
    docker compose -f "${DOCKER_DIR}/docker-compose.yml" down -v
}
trap cleanup EXIT

echo "=== Waiting for services to be healthy ==="

for i in $(seq 1 30); do
    if curl -sf http://localhost:8181/v1/config > /dev/null 2>&1; then
        echo "REST catalog is ready."
        break
    fi
    if [ "$i" -eq 30 ]; then
        echo "ERROR: REST catalog did not become ready in time."
        exit 1
    fi
    echo "  Waiting for REST catalog... ($i/30)"
    sleep 2
done

for i in $(seq 1 15); do
    if curl -sf http://localhost:9000/minio/health/live > /dev/null 2>&1; then
        echo "MinIO is ready."
        break
    fi
    if [ "$i" -eq 15 ]; then
        echo "ERROR: MinIO did not become ready in time."
        exit 1
    fi
    echo "  Waiting for MinIO... ($i/15)"
    sleep 2
done

echo ""
echo "=== Generating test data ==="
python3 "${SCRIPT_DIR}/generate_test_data.py"

export ICEBERG_SERVER_AVAILABLE=1

echo ""
echo "=== Running integration tests ==="
echo ""

# Run all .test files under test/sql/ via DuckDB's unittest runner.
# Tests gate on `require-env ICEBERG_SERVER_AVAILABLE` to skip when Docker is down.
"${UNITTEST}" "test/sql/*"
