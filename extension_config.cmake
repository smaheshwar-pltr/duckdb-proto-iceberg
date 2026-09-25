# Extensions built by `make`: this one, plus httpfs, which the SQL tests use to read table data from S3.
# httpfs is pinned to the commit DuckDB v1.5.1 builds against.

duckdb_extension_load(proto_iceberg
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

duckdb_extension_load(httpfs
    GIT_URL https://github.com/duckdb/duckdb-httpfs
    GIT_TAG 7e86e7a5e5a1f01f458361bebdfa9b0a9a73a619
)
