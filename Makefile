PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXT_NAME=proto_iceberg
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

include extension-ci-tools/makefiles/duckdb_extension.Makefile

export UBSAN_OPTIONS ?= print_stacktrace=1

.PHONY: unittest_release unittest_debug integration_test_release integration_test_debug

unittest_release unittest_debug: unittest_%:
	cmake --build build/$* --target proto_iceberg_unittest
	build/$*/extension/proto_iceberg/proto_iceberg_unittest

integration_test_release integration_test_debug: integration_test_%:
	BUILD_TYPE=$* test/scripts/run_integration_test.sh
