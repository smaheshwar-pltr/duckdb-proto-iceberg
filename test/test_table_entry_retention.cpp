#include "catch.hpp"
#include "fake_catalog.hpp"

#include "duckdb.hpp"

#include <memory>

using namespace duckdb;

// This tests a lifetime edge case that's easy to get wrong: duckdb_tables() holds references to table entries while
// it streams, so loading a table mid-scan must not free the listing placeholder that the loaded entry replaces.
TEST_CASE("listing placeholders survive replacement during a streaming scan", "[retention]") {
	// Enough tables to span several result chunks.
	constexpr idx_t table_count = 5000;

	DuckDB db(nullptr);
	RegisterFakeIcebergCatalog(*db.instance, "iceberg_fake", std::make_shared<FakeCatalog>(table_count));
	Connection conn(db);

	// A small streaming buffer pauses the scan after the first chunk.
	REQUIRE_FALSE(conn.Query("SET threads=1; SET streaming_buffer_size='1KB'; "
	                         "ATTACH '' AS lake (TYPE iceberg_fake); BEGIN")
	                  ->HasError());

	auto scan = conn.SendQuery("SELECT table_name FROM duckdb_tables() WHERE database_name = 'lake'");
	REQUIRE_FALSE(scan->HasError());
	auto first = scan->Fetch();
	REQUIRE(first);

	// Load a table the scan hasn't reached yet, replacing its placeholder.
	REQUIRE_FALSE(conn.TableInfo("lake", "default", FakeCatalog::TableName(table_count - 1)));

	idx_t rows = first->size();
	while (auto chunk = scan->Fetch()) {
		rows += chunk->size();
	}
	REQUIRE_FALSE(scan->HasError());
	REQUIRE(rows == table_count);
	REQUIRE_FALSE(conn.Query("COMMIT")->HasError());
}
