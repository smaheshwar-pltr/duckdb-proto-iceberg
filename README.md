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

Requirements:

- CMake 3.25+ and Ninja
- A C++23 compiler (CI uses GCC 14)
- Development packages for libcurl, OpenSSL and zlib (`libcurl4-openssl-dev libssl-dev zlib1g-dev` on Debian/Ubuntu)

```sh
git clone --recurse-submodules https://github.com/smaheshwar-pltr/duckdb-proto-iceberg.git
cd duckdb-proto-iceberg

GEN=ninja make release  # or: GEN=ninja make debug
```

The first build takes longer: CMake downloads and builds iceberg-cpp and its vendored dependencies (Arrow, the AWS SDK and others) while configuring. Later builds skip this unless the submodule or its build settings change. Set `CMAKE_BUILD_PARALLEL_LEVEL` to limit build parallelism on machines with little memory.

CI also builds loadable binaries for Linux (amd64, arm64) and macOS 13.3+ (amd64, arm64) with DuckDB's [extension-ci-tools](https://github.com/duckdb/extension-ci-tools); each run of the Main Extension Distribution Pipeline workflow attaches them as artifacts.

See [docs/UPDATING.md](docs/UPDATING.md) for updating DuckDB or iceberg-cpp.

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
make unittest_release  # or: make unittest_debug

# Integration tests, against a REST catalog and object store in Docker
python3 -m venv .venv && source .venv/bin/activate
pip install -r test/scripts/requirements.txt
make integration_test_release  # or: make integration_test_debug
```

Note: If debug builds hang, try using a release build instead.
