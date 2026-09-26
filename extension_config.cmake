# Extensions built by `make`: this one, plus httpfs, which the SQL tests use to read table data from S3.
# httpfs is pinned to the commit DuckDB v1.5.5 builds against.

duckdb_extension_load(proto_iceberg
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

duckdb_extension_load(httpfs
    GIT_URL https://github.com/duckdb/duckdb-httpfs
    GIT_TAG 827222fb45a043a7a852d1f7aae46901492a3cda
)
