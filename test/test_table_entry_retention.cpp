#include "catch.hpp"
#include "fake_catalog.hpp"

#include "duckdb.hpp"

#include <memory>

using namespace duckdb;

// duckdb_tables() captures references to a schema's table entries and reads them as result chunks are
// fetched, so loading a table mid-scan (which replaces its listing placeholder) must not free the
// placeholder the scan still references.
TEST_CASE("listing entries survive replacement during a streaming scan", "[retention]") {
	// Enough tables to span several result chunks so the scan can be paused partway.
	constexpr idx_t table_count = 5000;

	DuckDB db(nullptr);
	RegisterFakeIcebergCatalog(*db.instance, "iceberg_fake", std::make_shared<FakeCatalog>(table_count));
	Connection conn(db);

	// streaming_buffer_size makes the producer block after the first fetch, so the table loaded afterwards is
	// streamed only during the drain, once its placeholder has been replaced.
	REQUIRE_FALSE(conn.Query("SET threads=1; SET streaming_buffer_size='1KB'; "
	                         "ATTACH '' AS lake (TYPE iceberg_fake); BEGIN")
	                  ->HasError());

	auto scan = conn.SendQuery("SELECT table_name FROM duckdb_tables() WHERE database_name = 'lake'");
	REQUIRE_FALSE(scan->HasError());
	auto first = scan->Fetch();
	REQUIRE(first);

	// Replace a not-yet-streamed table's placeholder while the scan still references it.
	REQUIRE_FALSE(conn.TableInfo("lake", "default", FakeCatalog::TableName(table_count - 1)));

	// Draining the rest reads every captured reference, including the replaced table's placeholder.
	idx_t rows = first->size();
	while (auto chunk = scan->Fetch()) {
		rows += chunk->size();
	}
	REQUIRE_FALSE(scan->HasError());
	REQUIRE(rows == table_count);
	REQUIRE_FALSE(conn.Query("COMMIT")->HasError());
}
