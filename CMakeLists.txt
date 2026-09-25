cmake_minimum_required(VERSION 3.25)

set(TARGET_NAME proto_iceberg)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

# iceberg-cpp's headers need C++23. This only applies to targets defined in this directory, not to
# DuckDB itself.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/duckdb_cxx23_patch.cmake)
include(cmake/iceberg_cpp.cmake)

# ---------------------------------------------------------------------------
# Extension
# ---------------------------------------------------------------------------

set(EXTENSION_SOURCES
    src/proto_iceberg_extension.cpp
    src/conversion.cpp
    src/s3_conversion.cpp
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

foreach(target ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME})
    target_include_directories(${target} PRIVATE src/include)
    # DuckDB links these targets with the plain signature, so no PRIVATE/PUBLIC keyword here.
    target_link_libraries(${target} iceberg::iceberg_bundle_static iceberg::iceberg_rest_static)
endforeach()

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
    test/test_unwrap.cpp
    test/test_s3_conversion.cpp)
target_include_directories(proto_iceberg_unittest PRIVATE
    src/include
    "${CMAKE_SOURCE_DIR}/third_party/catch")
target_link_libraries(proto_iceberg_unittest PRIVATE ${EXTENSION_NAME} duckdb)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(
    TARGETS ${EXTENSION_NAME}
    LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
    ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
