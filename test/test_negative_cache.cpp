#include "catch.hpp"
#include "fake_catalog.hpp"

#include "duckdb.hpp"

#include <memory>

using namespace duckdb;

TEST_CASE("a missing table is looked up once and then cached", "[negative-cache]") {
	DuckDB db(nullptr);
	auto fake = std::make_shared<FakeCatalog>(/*table_count=*/0);
	RegisterFakeIcebergCatalog(*db.instance, "iceberg_fake", fake);
	Connection conn(db);

	// The negative cache is transaction-scoped, so hold a single transaction across both lookups.
	REQUIRE_FALSE(conn.Query("ATTACH '' AS lake (TYPE iceberg_fake); BEGIN")->HasError());

	// First reference resolves the table from the catalog and caches the miss.
	REQUIRE(conn.Query("SELECT * FROM lake.default.absent")->HasError());
	REQUIRE(fake->load_table_calls.load() == 1);

	// Subsequent references hit the negative cache without querying the catalog again.
	REQUIRE(conn.Query("SELECT * FROM lake.default.absent")->HasError());
	REQUIRE(fake->load_table_calls.load() == 1);

	REQUIRE_FALSE(conn.Query("COMMIT")->HasError());
}
