# DuckDB ProtoIceberg extension

An experimental C++23 DuckDB extension that reads [Apache Iceberg](https://iceberg.apache.org/) tables using [Apache Iceberg C++ (iceberg-cpp)](https://github.com/apache/iceberg-cpp). It supports Iceberg REST catalogs with tables in S3, including vended credentials.

This project is largely inspired by [duckdb-iceberg](https://github.com/duckdb/duckdb-iceberg). While that extension implements its own Iceberg client, this project aims to delegate to Iceberg's C++ implementation instead. See https://github.com/duckdb/duckdb-iceberg/discussions/1699 for further discussion and motivation.

DuckDB's extension template README can be found [here](https://github.com/duckdb/extension-template?tab=readme-ov-file#duckdb-extension-template).

## Building

```sh
git clone --recurse-submodules https://github.com/smaheshwar-pltr/duckdb-proto-iceberg.git
cd duckdb-proto-iceberg

GEN=ninja make release  # or: GEN=ninja make debug
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
make unittest_release  # or: make unittest_debug

# Integration tests
python3 -m venv .venv && source .venv/bin/activate
pip install -r test/scripts/requirements.txt
make integration_test_release  # or: make integration_test_debug
```

If debug builds hang, use a release build instead.
