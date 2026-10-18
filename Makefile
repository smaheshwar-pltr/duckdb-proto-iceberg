PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=proto_iceberg
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

.PHONY: unittest_release unittest_debug integration_test_release integration_test_debug

# Catch2 unit tests. Run `make release` or `make debug` first.
unittest_release unittest_debug: unittest_%:
	cmake --build build/$* --target proto_iceberg_unittest
	build/$*/extension/proto_iceberg/proto_iceberg_unittest

# SQLLogicTests in test/sql, against a REST catalog and object store in Docker.
integration_test_release integration_test_debug: integration_test_%:
	BUILD_TYPE=$* test/scripts/run_integration_test.sh
