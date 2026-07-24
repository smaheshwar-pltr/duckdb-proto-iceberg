cmake_minimum_required(VERSION 3.25)

set(TARGET_NAME proto_iceberg)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

# Apple/libc++-only DuckDB C++23 compatibility patch
include(${CMAKE_CURRENT_SOURCE_DIR}/scripts/duckdb_cxx23_patches.cmake)

# ---------------------------------------------------------------------------
# iceberg-cpp isolated CMake invocation
# ---------------------------------------------------------------------------

# Standalone build (not add_subdirectory) so DuckDB's vendored fmt/zstd include dirs
# don't shadow iceberg-cpp's own. Drop once iceberg-cpp ships a package:
# https://github.com/apache/iceberg-cpp/issues/188

# Stamp-based skip: SHA-256 of submodule commit + uncommitted changes + cmake args.

set(ICEBERG_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/_iceberg_install")
set(ICEBERG_BUILD_DIR "${CMAKE_BINARY_DIR}/_iceberg_build")
set(ICEBERG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/iceberg-cpp")
set(ICEBERG_STAMP_FILE "${ICEBERG_INSTALL_PREFIX}/.iceberg_stamp")
set(ICEBERG_CONFIG_FILE "${ICEBERG_INSTALL_PREFIX}/lib/cmake/iceberg/iceberg-config.cmake")

set(ICEBERG_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_INSTALL_PREFIX=${ICEBERG_INSTALL_PREFIX}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
    -DICEBERG_BUILD_TESTS=OFF
    -DICEBERG_BUILD_STATIC=ON
    -DICEBERG_BUILD_SHARED=OFF
    -DICEBERG_BUILD_BUNDLE=ON
    -DICEBERG_BUILD_REST=ON
    -DICEBERG_BUILD_REST_INTEGRATION_TESTS=OFF
    -DICEBERG_S3=ON
    -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER
)

if(CMAKE_MAKE_PROGRAM)
    list(APPEND ICEBERG_CMAKE_ARGS -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
endif()
if(CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND ICEBERG_CMAKE_ARGS -DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER})
endif()
if(CMAKE_C_COMPILER_LAUNCHER)
    list(APPEND ICEBERG_CMAKE_ARGS -DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER})
endif()
if(CMAKE_OSX_ARCHITECTURES)
    list(APPEND ICEBERG_CMAKE_ARGS -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES})
endif()
if(CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND ICEBERG_CMAKE_ARGS -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()
if(CMAKE_OSX_SYSROOT)
    list(APPEND ICEBERG_CMAKE_ARGS -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT})
endif()
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND ICEBERG_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()

set(ICEBERG_SKIP_BUILD FALSE)

execute_process(
    COMMAND git -C "${ICEBERG_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE _iceberg_git_hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _iceberg_git_result
    ERROR_QUIET)

if(NOT _iceberg_git_result EQUAL 0)
    message(STATUS "[proto_iceberg] git not available, stamp skipped")
else()
    set(_iceberg_diff "")
    execute_process(
        COMMAND git -C "${ICEBERG_SOURCE_DIR}" diff --quiet HEAD --
        RESULT_VARIABLE _iceberg_diff_result
        ERROR_QUIET)
    if(NOT _iceberg_diff_result EQUAL 0)
        execute_process(
            COMMAND git -C "${ICEBERG_SOURCE_DIR}" diff HEAD
            OUTPUT_VARIABLE _iceberg_diff
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()

    string(SHA256 _iceberg_stamp_hash "${_iceberg_git_hash}\n${_iceberg_diff}\n${ICEBERG_CMAKE_ARGS}")

    if(EXISTS "${ICEBERG_STAMP_FILE}" AND EXISTS "${ICEBERG_CONFIG_FILE}")
        file(READ "${ICEBERG_STAMP_FILE}" _iceberg_existing_stamp)
        string(STRIP "${_iceberg_existing_stamp}" _iceberg_existing_stamp)
        if(_iceberg_existing_stamp STREQUAL _iceberg_stamp_hash)
            set(ICEBERG_SKIP_BUILD TRUE)
            message(STATUS "[proto_iceberg] iceberg-cpp up to date (stamp match), skipping build")
        endif()
    endif()
endif()

if(NOT ICEBERG_SKIP_BUILD)
    message(STATUS "[proto_iceberg] Configuring iceberg-cpp (isolated build)...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --compile-no-warning-as-error -G "${CMAKE_GENERATOR}"
            ${ICEBERG_CMAKE_ARGS} -S "${ICEBERG_SOURCE_DIR}" -B "${ICEBERG_BUILD_DIR}"
        RESULT_VARIABLE ICEBERG_CONFIGURE_RESULT
    )
    if(NOT ICEBERG_CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "[proto_iceberg] iceberg-cpp configure failed (exit ${ICEBERG_CONFIGURE_RESULT})")
    endif()

    message(STATUS "[proto_iceberg] Building iceberg-cpp...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${ICEBERG_BUILD_DIR}" --config ${CMAKE_BUILD_TYPE} --parallel
        RESULT_VARIABLE ICEBERG_BUILD_RESULT
    )
    if(NOT ICEBERG_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "[proto_iceberg] iceberg-cpp build failed (exit ${ICEBERG_BUILD_RESULT})")
    endif()

    message(STATUS "[proto_iceberg] Installing iceberg-cpp to ${ICEBERG_INSTALL_PREFIX}...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --install "${ICEBERG_BUILD_DIR}" --config ${CMAKE_BUILD_TYPE}
            --prefix "${ICEBERG_INSTALL_PREFIX}"
        RESULT_VARIABLE ICEBERG_INSTALL_RESULT
    )
    if(NOT ICEBERG_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "[proto_iceberg] iceberg-cpp install failed (exit ${ICEBERG_INSTALL_RESULT})")
    endif()

    # Write stamp only after successful install
    file(WRITE "${ICEBERG_STAMP_FILE}" "${_iceberg_stamp_hash}")
endif()

find_package(iceberg CONFIG REQUIRED PATHS "${ICEBERG_INSTALL_PREFIX}" NO_DEFAULT_PATH)
message(STATUS "[proto_iceberg] Found iceberg at ${ICEBERG_INSTALL_PREFIX}")

# ---------------------------------------------------------------------------
# Extension
# ---------------------------------------------------------------------------

set(EXTENSION_SOURCES
    src/proto_iceberg_extension.cpp
    src/adapters.cpp
    src/proto_iceberg_catalog.cpp
    src/proto_iceberg_catalog_attach.cpp
    src/proto_iceberg_transaction.cpp
    src/proto_iceberg_schema_entry.cpp
    src/proto_iceberg_table_entry.cpp
    src/proto_iceberg_multi_file_reader.cpp
    src/proto_iceberg_multi_file_list.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})

# The " " second argument is required by DuckDB's build_loadable_extension()
# signature (PARAMETERS, which is unused for out-of-tree extensions).
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

set(ICEBERG_LIBS iceberg::iceberg_bundle_static iceberg::iceberg_rest_static)

target_include_directories(${EXTENSION_NAME} PRIVATE src/include)
target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE src/include)
target_link_libraries(${EXTENSION_NAME} ${ICEBERG_LIBS})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${ICEBERG_LIBS})

set_target_properties(${EXTENSION_NAME} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
set_target_properties(${LOADABLE_EXTENSION_NAME} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------

add_executable(proto_iceberg_unittest EXCLUDE_FROM_ALL
    test/test_type_mapping.cpp
    test/test_literal_conversion.cpp
    test/test_filter_translation.cpp
    test/test_signed_store.cpp
    test/test_column_definition.cpp
    test/test_mutex.cpp
    test/test_unwrap.cpp)
set_target_properties(proto_iceberg_unittest PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
target_include_directories(proto_iceberg_unittest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/duckdb/third_party/catch
    ${CMAKE_CURRENT_SOURCE_DIR}/duckdb/src/include)
target_link_libraries(proto_iceberg_unittest PRIVATE
    ${EXTENSION_NAME} duckdb)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(
    TARGETS ${EXTENSION_NAME}
    LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
    ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
