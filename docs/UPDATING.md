# Updating dependencies

## DuckDB

The extension targets a DuckDB release, which is pinned in several places that must move together. Check out submodule commits explicitly rather than using `make update` or `make pull`, which move every submodule to the tip of its tracked branch.

- The `duckdb` submodule: check out the release tag, and set its `branch` in `.gitmodules` to the release branch.
- The `extension-ci-tools` submodule: check out the branch named after the release (e.g. `v1.5.1`), and set its `branch` in `.gitmodules` to match.
- `.github/workflows/MainDistributionPipeline.yml`: the reusable workflow refs and the `duckdb_version` and `ci_tools_version` inputs.
- `extension_config.cmake`: the httpfs `GIT_TAG`, which should match `duckdb/.github/config/extensions/httpfs.cmake`.
- `cmake/duckdb_cxx23.patch`: check whether it is still needed. On macOS, configuring fails if it no longer applies.

Extensions are built against DuckDB's internal C++ API, which can change between releases without a changelog. If the extension no longer compiles, these help to find out what changed:

- DuckDB's [release notes](https://github.com/duckdb/duckdb/releases)
- The history of DuckDB's [core extension patches](https://github.com/duckdb/duckdb/commits/main/.github/patches/extensions)
- The git history of the affected DuckDB headers

## iceberg-cpp

Check out the new commit in `third_party/iceberg-cpp` and run `make`. The build notices the new revision and rebuilds iceberg-cpp during configuration. To force a clean rebuild, delete `build/<type>/_iceberg_install` and `build/<type>/_iceberg_build`.
