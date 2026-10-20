PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXT_NAME=proto_iceberg
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Runtime options for sanitized builds (`make debug`, or EXT_FLAGS=-DFORCE_ASSERT=1); the environment overrides them.
export UBSAN_OPTIONS ?= print_stacktrace=1

.PHONY: unittest_release unittest_debug integration_test_release integration_test_debug

# Catch2 unit tests. Run `make release` or `make debug` first.
unittest_release unittest_debug: unittest_%:
	cmake --build build/$* --target proto_iceberg_unittest
	build/$*/extension/proto_iceberg/proto_iceberg_unittest

# SQLLogicTests in test/sql, against a REST catalog and object store in Docker.
integration_test_release integration_test_debug: integration_test_%:
	BUILD_TYPE=$* test/scripts/run_integration_test.sh
