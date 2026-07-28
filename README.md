# DuckDB ProtoIceberg extension

An experimental, C++23 DuckDB extension for [Apache Iceberg](https://iceberg.apache.org/), that leverages [Apache Iceberg C++ (iceberg-cpp)](https://github.com/apache/iceberg-cpp) to provide read-only access to tables. Currently, the extension supports Iceberg REST catalogs with tables in S3; credential vending enables access delegation.

This project is largely inspired by [duckdb-iceberg](https://github.com/duckdb/duckdb-iceberg). While that extension implements its own Iceberg client, this project aims to delegate to Iceberg's C++ implementation instead.

See [DuckDB's extension template README](https://github.com/duckdb/extension-template?tab=readme-ov-file#duckdb-extension-template) for more details.

## Architecture

Our catalog system has three levels: catalog, schema, and table. These are mapped to Iceberg's REST catalog, namespaces, and tables respectively.

```
DuckDB Engine
 ├─ ProtoIcebergCatalog                           (wraps iceberg::rest::RestCatalog)
 │   └─ ProtoIcebergSchemaEntry                   (Iceberg namespace + table store)
 │       └─ ProtoIcebergTableEntry                (wraps Iceberg table)
 │           └─ parquet_scan                      (via MultiFileReader API)
 │               └─ ProtoIcebergMultiFileReader   (creates file list + field-ID schema binding)
 │                   └─ ProtoIcebergMultiFileList (lazy file planning + filter pushdown)
 └─ ProtoIcebergTransactionManager
     └─ ProtoIcebergTransaction                   (schema store + timestamp + secrets)
```

## Building

```sh
git clone --recurse-submodules https://github.com/smaheshwar-pltr/duckdb-iceberg-cpp.git
cd duckdb-iceberg-cpp

# Debug
GEN=ninja make debug

# Release
GEN=ninja make release
```

The first build takes longer, because iceberg-cpp and its vendored dependencies are compiled. Subsequent builds check changes and skip this step if there are none.

### Updating iceberg-cpp

Submodule updates are detected automatically, after pulling:

```sh
git submodule update --init --recursive
GEN=ninja make debug
```

To force a full rebuild, delete the install directory:

```sh
rm -rf build/debug/_iceberg_install
GEN=ninja make debug
```

## Usage

```
$ ./build/release/duckdb -unsigned
```

```sql
LOAD 'build/release/extension/proto_iceberg/proto_iceberg.duckdb_extension';

ATTACH '' AS my_catalog (
  TYPE ICEBERG,
  ENDPOINT 'http://localhost:8181',
  TOKEN '<bearer-token>'
);

SELECT * FROM my_catalog.my_namespace.my_table LIMIT 5;
```

## Testing

```sh
# Unit tests
cmake --build build/debug --target proto_iceberg_unittest   # or build/release
build/debug/extension/proto_iceberg/proto_iceberg_unittest  # or build/release

# Integration tests (requires Docker)
test/scripts/run_integration_test.sh                        # BUILD_TYPE=release for a release build
```

Note: If debug builds hang, try using a release build instead.
