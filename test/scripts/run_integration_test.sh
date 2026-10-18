#!/usr/bin/env bash
# Runs the SQLLogicTests in test/sql against a REST catalog and object store in Docker.
# Tests gate on `require-env ICEBERG_SERVER_AVAILABLE`, so they skip when run without this script.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-debug}"
UNITTEST="${UNITTEST:-${REPO_DIR}/build/${BUILD_TYPE}/test/unittest}"
COMPOSE=(docker compose -f "${REPO_DIR}/test/docker/docker-compose.yml")

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_container_overflow=0}"

if [[ ! -x "${UNITTEST}" ]]; then
    echo "error: DuckDB test runner not found at ${UNITTEST}; run 'make ${BUILD_TYPE}' first" >&2
    exit 1
fi

cleanup() {
    local status=$?
    if [[ ${status} -ne 0 ]]; then
        "${COMPOSE[@]}" logs || true
    fi
    "${COMPOSE[@]}" down -v
}
trap cleanup EXIT

wait_for() {
    local name=$1 url=$2 attempts=$3
    for ((i = 1; i <= attempts; i++)); do
        if curl -sf "${url}" >/dev/null; then
            return
        fi
        sleep 2
    done
    echo "error: ${name} did not become ready" >&2
    return 1
}

"${COMPOSE[@]}" up -d
wait_for "REST catalog" http://localhost:8181/v1/config 30
wait_for "RustFS" http://localhost:9000/health/ready 15

python3 "${REPO_DIR}/test/scripts/generate_test_data.py"

cd "${REPO_DIR}"
ICEBERG_SERVER_AVAILABLE=1 "${UNITTEST}" "test/sql/*"
