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

target_include_directories(${EXTENSION_NAME} PRIVATE src/include)
target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE src/include)

# iceberg-cpp is a private dependency: DuckDB's executables that link the static extension need its archives, but
# not its include paths or its C++ standard. DuckDB links the extension targets with the plain
# target_link_libraries() signature, which can't be combined with PRIVATE, so set the properties PRIVATE would.
set(ICEBERG_LIBRARIES iceberg::iceberg_bundle_static iceberg::iceberg_rest_static)
set_property(TARGET ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME} APPEND PROPERTY LINK_LIBRARIES ${ICEBERG_LIBRARIES})
foreach(library ${ICEBERG_LIBRARIES})
    set_property(TARGET ${EXTENSION_NAME} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:${library}>")
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
target_link_libraries(proto_iceberg_unittest PRIVATE ${EXTENSION_NAME} ${ICEBERG_LIBRARIES} duckdb)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(
    TARGETS ${EXTENSION_NAME}
    LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
    ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
