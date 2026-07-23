# DuckDB ProtoIceberg extension

An experimental, C++23 DuckDB extension for [Apache Iceberg](https://iceberg.apache.org/), that leverages [Apache Iceberg C++ (iceberg-cpp)](https://github.com/apache/iceberg-cpp) to provide read-only access to tables. Currently, the extension supports Iceberg REST catalogs with tables in S3; credential vending enables access delegation.

This project is largely inspired by [duckdb-iceberg](https://github.com/duckdb/duckdb-iceberg). While that extension implements its own Iceberg client, this project aims to delegate to Iceberg's C++ implementation instead.

See [DuckDB's extension template README](https://github.com/duckdb/extension-template?tab=readme-ov-file#duckdb-extension-template) for more details.

## Architecture

Our catalog system has three levels — catalog, schema, and table. We map these to Iceberg's REST catalog, namespaces, and tables respectively.

| Component | Description                                                                                                                                                                                                                                                                |
|-----------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `ProtoIcebergCatalog` | Created at `ATTACH` time with a REST endpoint and credentials. Creates schema entries optimistically — namespace existence is only verified when a table load fails.                                                                                                       |
| `ProtoIcebergSchemaEntry` | One per Iceberg namespace. Lists and looks up tables via `iceberg-cpp`'s REST catalog client. On lookup, loads the table's metadata, resolves the scan mode (current or snapshot-pinned), converts the Iceberg schema to DuckDB column definitions, and stores the result. |
| `ProtoIcebergTableEntry` | One per table. Carries the pre-resolved scan mode and schema. `GetScanFunction()` sets up per-table S3 credentials from `iceberg-cpp`'s table metadata (credential vending) and wires up the scan function backed by a DuckDB `parquet_scan`.                              |
| `ProtoIcebergTransaction` | Captures the transaction's start timestamp, which pins reads to specific Iceberg snapshots. Owns an in-memory store of schema entries (avoiding repeated REST calls within a transaction), and tracks temporary S3 secrets for cleanup at commit.                          |
| `ProtoIcebergScanInfo` | A `{table, scan_mode, schema}` structure passed from the catalog layer down to the scan layer. The scan mode is either the current table, or pinned to a specific snapshot ID.                                                                                             |
| `ProtoIcebergMultiFileList` | Discovers which Parquet files to read by delegating to `iceberg-cpp`'s scan planning. DuckDB's `WHERE`-clause filters are translated to `iceberg-cpp` expressions, so that irrelevant files are pruned at plan time.                                                       |
| `ProtoIcebergMultiFileReader` | Resolves the Iceberg schema for the scan and instructs DuckDB to match Parquet columns by Iceberg field ID (*not* by name, which would break schema evolution and column renaming).                                                                                        |

```
DuckDB Engine
 └─ ProtoIcebergCatalog              (wraps iceberg::rest::RestCatalog)
     ├─ ProtoIcebergSchemaEntry       (wraps Iceberg namespace, owns table store)
     │   └─ ProtoIcebergTableEntry    (wraps Iceberg table)
     │       └─ parquet_scan           (via MultiFileReader pattern)
     │           ├─ ProtoIcebergMultiFileReader  (schema via field-ID mapping)
     │           └─ ProtoIcebergMultiFileList    (lazy file discovery + filter pushdown)
     └─ ProtoIcebergTransactionManager
         └─ ProtoIcebergTransaction   (schema store + start timestamp + secret tracking)
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

Submodule updates are detected automatically — just build after pulling:

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
