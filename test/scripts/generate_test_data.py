#!/usr/bin/env python3
"""Generate test data in Iceberg tables via PyIceberg."""

import time
import datetime
import uuid
from decimal import Decimal
import pyarrow as pa
from pyiceberg.catalog import load_catalog
from pyiceberg.schema import Schema
from pyiceberg.types import (
    NestedField,
    BooleanType,
    IntegerType,
    LongType,
    FloatType,
    DoubleType,
    StringType,
    BinaryType,
    DateType,
    TimeType,
    TimestampType,
    TimestamptzType,
    DecimalType,
    UUIDType,
    StructType,
    ListType,
    MapType,
)
from pyiceberg.partitioning import PartitionSpec, PartitionField
from pyiceberg.transforms import IdentityTransform, DayTransform


def wait_for_catalog(catalog_uri, max_retries=30, delay=2):
    """Wait for the REST catalog to be ready."""
    import urllib.request

    for i in range(max_retries):
        try:
            urllib.request.urlopen(f"{catalog_uri}/v1/config", timeout=5)
            print("REST catalog is ready.")
            return
        except Exception:
            print(f"Waiting for REST catalog... ({i + 1}/{max_retries})")
            time.sleep(delay)
    raise RuntimeError("REST catalog did not become ready in time")


def create_catalog():
    catalog_uri = "http://localhost:8181"
    wait_for_catalog(catalog_uri)

    catalog = load_catalog(
        "rest",
        **{
            "type": "rest",
            "uri": catalog_uri,
            "s3.endpoint": "http://localhost:9000",
            "s3.access-key-id": "admin",
            "s3.secret-access-key": "password",
        },
    )

    try:
        catalog.create_namespace("default")
        print("Created namespace 'default'")
    except Exception:
        print("Namespace 'default' already exists")

    return catalog


def assert_file_count(catalog, table_id, expected):
    """Verify the table has the expected number of data files."""
    table = catalog.load_table(table_id)
    actual = len(table.inspect.files())
    assert actual == expected, f"{table_id}: expected {expected} data files, got {actual}"


def drop_and_create(catalog, table_id, schema, partition_spec=None, properties=None):
    """Drop and recreate a table."""
    try:
        catalog.drop_table(table_id)
    except Exception:
        pass

    kwargs = {"schema": schema}
    if partition_spec:
        kwargs["partition_spec"] = partition_spec
    if properties:
        kwargs["properties"] = properties

    table = catalog.create_table(table_id, **kwargs)
    print(f"Created table '{table_id}'")
    return table


def create_basic_table(catalog):
    """Basic types: int, string, double. 5 rows."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="name", field_type=StringType(), required=False),
        NestedField(field_id=3, name="value", field_type=DoubleType(), required=False),
    )

    table = drop_and_create(catalog, "default.test_table", schema)

    data = pa.table(
        {
            "id": pa.array([1, 2, 3, 4, 5], type=pa.int32()),
            "name": pa.array(["Alice", "Bob", "Charlie", "Diana", "Eve"], type=pa.string()),
            "value": pa.array([1.1, 2.2, 3.3, 4.4, 5.5], type=pa.float64()),
        }
    )

    table.append(data)
    print(f"  Inserted {len(data)} rows into 'default.test_table'")


def create_all_types_table(catalog):
    """All primitive types for schema mapping verification."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="col_bool", field_type=BooleanType(), required=False),
        NestedField(field_id=3, name="col_int", field_type=IntegerType(), required=False),
        NestedField(field_id=4, name="col_long", field_type=LongType(), required=False),
        NestedField(field_id=5, name="col_float", field_type=FloatType(), required=False),
        NestedField(field_id=6, name="col_double", field_type=DoubleType(), required=False),
        NestedField(field_id=7, name="col_string", field_type=StringType(), required=False),
        NestedField(field_id=8, name="col_binary", field_type=BinaryType(), required=False),
        NestedField(field_id=9, name="col_date", field_type=DateType(), required=False),
        NestedField(
            field_id=10,
            name="col_timestamp",
            field_type=TimestampType(),
            required=False,
        ),
        NestedField(
            field_id=11,
            name="col_timestamptz",
            field_type=TimestamptzType(),
            required=False,
        ),
        NestedField(
            field_id=12,
            name="col_decimal",
            field_type=DecimalType(precision=10, scale=2),
            required=False,
        ),
        NestedField(field_id=13, name="col_uuid", field_type=UUIDType(), required=False),
    )

    table = drop_and_create(catalog, "default.all_types", schema)

    data = pa.table(
        {
            "id": pa.array([1, 2, 3], type=pa.int32()),
            "col_bool": pa.array([True, False, True], type=pa.bool_()),
            "col_int": pa.array([42, -100, 0], type=pa.int32()),
            "col_long": pa.array([9999999999, -9999999999, 0], type=pa.int64()),
            "col_float": pa.array([3.14, -2.71, 0.0], type=pa.float32()),
            "col_double": pa.array([1.23456789012345, -9.87654321098765, 0.0], type=pa.float64()),
            "col_string": pa.array(["hello world", "", "unicode: こんにちは"], type=pa.string()),
            "col_binary": pa.array([b"\x00\x01\x02", b"", b"\xff\xfe"], type=pa.binary()),
            "col_date": pa.array(
                [
                    datetime.date(2024, 1, 15),
                    datetime.date(2000, 6, 30),
                    datetime.date(1970, 1, 1),
                ],
                type=pa.date32(),
            ),
            "col_timestamp": pa.array(
                [
                    datetime.datetime(2024, 1, 15, 10, 30, 0),
                    datetime.datetime(2000, 6, 30, 23, 59, 59),
                    datetime.datetime(1970, 1, 1, 0, 0, 0),
                ],
                type=pa.timestamp("us"),
            ),
            "col_timestamptz": pa.array(
                [
                    datetime.datetime(2024, 1, 15, 10, 30, 0),
                    datetime.datetime(2000, 6, 30, 23, 59, 59),
                    datetime.datetime(1970, 1, 1, 0, 0, 0),
                ],
                type=pa.timestamp("us", tz="UTC"),
            ),
            "col_decimal": pa.array(
                [Decimal("12345.67"), Decimal("-9999.99"), Decimal("0.01")],
                type=pa.decimal128(10, 2),
            ),
            "col_uuid": pa.array(
                [
                    uuid.UUID("550e8400-e29b-41d4-a716-446655440000").bytes,
                    uuid.UUID("6ba7b810-9dad-11d1-80b4-00c04fd430c8").bytes,
                    uuid.UUID("00000000-0000-0000-0000-000000000000").bytes,
                ],
                type=pa.binary(16),
            ),
        }
    )

    table.append(data)
    print(f"  Inserted {len(data)} rows into 'default.all_types'")


def create_nullable_table(catalog):
    """NULLs across multiple column types."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="name", field_type=StringType(), required=False),
        NestedField(field_id=3, name="score", field_type=DoubleType(), required=False),
        NestedField(field_id=4, name="active", field_type=BooleanType(), required=False),
    )

    table = drop_and_create(catalog, "default.nullable_table", schema)

    data = pa.table(
        {
            "id": pa.array([1, 2, 3, 4, 5, 6], type=pa.int32()),
            "name": pa.array(["Alice", None, "Charlie", None, "Eve", "Frank"], type=pa.string()),
            "score": pa.array([95.5, 87.3, None, None, 72.1, 88.0], type=pa.float64()),
            "active": pa.array([True, False, True, None, None, False], type=pa.bool_()),
        }
    )

    table.append(data)
    print(f"  Inserted {len(data)} rows into 'default.nullable_table'")


def create_partitioned_table(catalog):
    """Partitioned by category (identity transform)."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="category", field_type=StringType(), required=False),
        NestedField(field_id=3, name="value", field_type=DoubleType(), required=False),
    )

    partition_spec = PartitionSpec(
        PartitionField(
            source_id=2,
            field_id=1000,
            transform=IdentityTransform(),
            name="category_identity",
        ),
    )

    table = drop_and_create(catalog, "default.partitioned_table", schema, partition_spec=partition_spec)

    data = pa.table(
        {
            "id": pa.array([1, 2, 3, 4, 5, 6, 7, 8], type=pa.int32()),
            "category": pa.array(["A", "A", "B", "B", "C", "C", "A", "B"], type=pa.string()),
            "value": pa.array([10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0], type=pa.float64()),
        }
    )

    table.append(data)
    print(f"  Inserted {len(data)} rows into 'default.partitioned_table'")


def create_date_partitioned_table(catalog):
    """Partitioned by day(event_date)."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="event_date", field_type=DateType(), required=False),
        NestedField(field_id=3, name="event_type", field_type=StringType(), required=False),
        NestedField(field_id=4, name="amount", field_type=DoubleType(), required=False),
    )

    partition_spec = PartitionSpec(
        PartitionField(
            source_id=2,
            field_id=1001,
            transform=DayTransform(),
            name="event_date_day",
        ),
    )

    table = drop_and_create(catalog, "default.date_partitioned", schema, partition_spec=partition_spec)

    data = pa.table(
        {
            "id": pa.array([1, 2, 3, 4, 5, 6], type=pa.int32()),
            "event_date": pa.array(
                [
                    datetime.date(2024, 1, 1),
                    datetime.date(2024, 1, 1),
                    datetime.date(2024, 1, 2),
                    datetime.date(2024, 1, 2),
                    datetime.date(2024, 1, 3),
                    datetime.date(2024, 1, 3),
                ],
                type=pa.date32(),
            ),
            "event_type": pa.array(
                ["click", "view", "click", "purchase", "view", "click"],
                type=pa.string(),
            ),
            "amount": pa.array([0.0, 0.0, 0.0, 99.99, 0.0, 0.0], type=pa.float64()),
        }
    )

    table.append(data)
    print(f"  Inserted {len(data)} rows into 'default.date_partitioned'")


def create_multi_append_table(catalog):
    """3 separate appends to produce multiple data files."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="batch", field_type=StringType(), required=False),
        NestedField(field_id=3, name="value", field_type=IntegerType(), required=False),
    )

    table = drop_and_create(catalog, "default.multi_append", schema)

    for batch_num in range(1, 4):
        batch_data = pa.table(
            {
                "id": pa.array(
                    list(range((batch_num - 1) * 5 + 1, batch_num * 5 + 1)),
                    type=pa.int32(),
                ),
                "batch": pa.array([f"batch_{batch_num}"] * 5, type=pa.string()),
                "value": pa.array([batch_num * 100 + i for i in range(5)], type=pa.int32()),
            }
        )
        table.append(batch_data)
        print(f"  Inserted batch {batch_num} (5 rows) into 'default.multi_append'")


def create_join_tables(catalog):
    """Orders + customers tables for join tests."""
    # Orders table
    orders_schema = Schema(
        NestedField(field_id=1, name="order_id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="customer_id", field_type=IntegerType(), required=False),
        NestedField(field_id=3, name="order_date", field_type=DateType(), required=False),
        NestedField(field_id=4, name="total", field_type=DoubleType(), required=False),
    )

    orders_table = drop_and_create(catalog, "default.orders", orders_schema)

    orders_data = pa.table(
        {
            "order_id": pa.array([101, 102, 103, 104, 105, 106], type=pa.int32()),
            "customer_id": pa.array([1, 2, 1, 3, 2, 4], type=pa.int32()),
            "order_date": pa.array(
                [
                    datetime.date(2024, 1, 10),
                    datetime.date(2024, 1, 15),
                    datetime.date(2024, 2, 1),
                    datetime.date(2024, 2, 10),
                    datetime.date(2024, 3, 1),
                    datetime.date(2024, 3, 15),
                ],
                type=pa.date32(),
            ),
            "total": pa.array([150.00, 250.50, 75.25, 320.00, 180.75, 99.99], type=pa.float64()),
        }
    )

    orders_table.append(orders_data)
    print(f"  Inserted {len(orders_data)} rows into 'default.orders'")

    # Customers table
    customers_schema = Schema(
        NestedField(field_id=1, name="customer_id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="name", field_type=StringType(), required=False),
        NestedField(field_id=3, name="city", field_type=StringType(), required=False),
    )

    customers_table = drop_and_create(catalog, "default.customers", customers_schema)

    customers_data = pa.table(
        {
            "customer_id": pa.array([1, 2, 3, 5], type=pa.int32()),
            "name": pa.array(["Alice", "Bob", "Charlie", "Eve"], type=pa.string()),
            "city": pa.array(["New York", "London", "Tokyo", "Paris"], type=pa.string()),
        }
    )

    customers_table.append(customers_data)
    print(f"  Inserted {len(customers_data)} rows into 'default.customers'")


def create_schema_evolution_table(catalog):
    """Table with a renamed column to test schema evolution.

    Creates a table with (id, details), writes 3 rows, renames 'details' to
    'description', then writes 3 more rows.  The earlier Parquet files still
    have the physical column name 'details', while the current Iceberg schema
    says 'description'.  A correct reader must resolve columns by field-id,
    not by name.
    """
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="details", field_type=StringType(), required=False),
    )

    table = drop_and_create(catalog, "default.schema_evolution", schema)

    # First batch — written under the original column name 'details'
    batch1 = pa.table(
        {
            "id": pa.array([1, 2, 3], type=pa.int32()),
            "details": pa.array(["original-one", "original-two", "original-three"], type=pa.string()),
        }
    )
    table.append(batch1)
    print(f"  Inserted {len(batch1)} rows into 'default.schema_evolution' (before rename)")

    # Rename 'details' -> 'description' via schema evolution
    with table.update_schema() as update:
        update.rename_column("details", "description")
    print("  Renamed column 'details' -> 'description'")

    # Reload the table so the PyIceberg writer sees the updated schema
    table = catalog.load_table("default.schema_evolution")

    # Second batch — written under the new column name 'description'
    batch2 = pa.table(
        {
            "id": pa.array([4, 5, 6], type=pa.int32()),
            "description": pa.array(["renamed-four", "renamed-five", "renamed-six"], type=pa.string()),
        }
    )
    table.append(batch2)
    print(f"  Inserted {len(batch2)} rows into 'default.schema_evolution' (after rename)")


def create_multi_partition_table(catalog):
    """Two partition columns (region, year) for filter pushdown tests.

    3 regions x 3 years = 9 partition combos, each appended separately to
    produce 9 data files.  2 rows per combo = 18 rows total.
    """
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="region", field_type=StringType(), required=False),
        NestedField(field_id=3, name="year", field_type=IntegerType(), required=False),
        NestedField(field_id=4, name="revenue", field_type=DoubleType(), required=False),
        NestedField(field_id=5, name="tag", field_type=DecimalType(precision=10, scale=2), required=False),
    )

    partition_spec = PartitionSpec(
        PartitionField(
            source_id=2,
            field_id=1000,
            transform=IdentityTransform(),
            name="region_identity",
        ),
        PartitionField(
            source_id=3,
            field_id=1001,
            transform=IdentityTransform(),
            name="year_identity",
        ),
    )

    table = drop_and_create(catalog, "default.multi_partition", schema, partition_spec=partition_spec)

    regions = ["us", "eu", "asia"]
    years = [2023, 2024, 2025]
    row_id = 1
    for region in regions:
        for year in years:
            data = pa.table(
                {
                    "id": pa.array([row_id, row_id + 1], type=pa.int32()),
                    "region": pa.array([region, region], type=pa.string()),
                    "year": pa.array([year, year], type=pa.int32()),
                    "revenue": pa.array(
                        [float(row_id * 100), float((row_id + 1) * 100)],
                        type=pa.float64(),
                    ),
                    "tag": pa.array(
                        [Decimal(f"{row_id}.00"), Decimal(f"{row_id + 1}.00")],
                        type=pa.decimal128(10, 2),
                    ),
                }
            )
            table.append(data)
            row_id += 2

    print(f"  Inserted 18 rows into 'default.multi_partition' (3 regions x 3 years = 9 files)")


def create_empty_table(catalog):
    """Empty table (no rows) with a multi-type schema."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="name", field_type=StringType(), required=False),
        NestedField(field_id=3, name="value", field_type=DoubleType(), required=False),
        NestedField(field_id=4, name="created_date", field_type=DateType(), required=False),
        NestedField(field_id=5, name="ts", field_type=TimestampType(), required=False),
        NestedField(field_id=6, name="active", field_type=BooleanType(), required=False),
    )

    drop_and_create(catalog, "default.empty_table", schema)
    print("  Created empty table 'default.empty_table' (no rows, 6 columns)")


def create_pinned_schema_table(catalog):
    """Table where schema evolves (column renamed) but NO data is written after."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="old_name", field_type=StringType(), required=False),
    )

    table = drop_and_create(catalog, "default.pinned_schema", schema)

    data = pa.table(
        {
            "id": pa.array([1, 2, 3], type=pa.int32()),
            "old_name": pa.array(["alpha", "beta", "gamma"], type=pa.string()),
        }
    )
    table.append(data)
    print(f"  Inserted {len(data)} rows into 'default.pinned_schema' (before rename)")

    # Rename column without writing new data — no new snapshot is created.
    with table.update_schema() as update:
        update.rename_column("old_name", "new_name")
    print("  Renamed column 'old_name' -> 'new_name' (no new data written)")


def create_rollback_table(catalog):
    """Table that is rolled back to a previous snapshot.

    Creates a table, writes batch1, writes batch2, then rolls back to batch1's
    snapshot (so a reader should see only batch1 data).
    """
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="value", field_type=StringType(), required=False),
    )

    table = drop_and_create(catalog, "default.rollback_table", schema)

    # Batch 1
    batch1 = pa.table(
        {
            "id": pa.array([1, 2, 3], type=pa.int32()),
            "value": pa.array(["a", "b", "c"], type=pa.string()),
        }
    )
    table.append(batch1)
    table = catalog.load_table("default.rollback_table")
    snapshot_after_batch1 = table.metadata.current_snapshot_id
    print(f"  Inserted batch1 (3 rows) into 'default.rollback_table', snapshot={snapshot_after_batch1}")

    # Batch 2
    batch2 = pa.table(
        {
            "id": pa.array([4, 5, 6], type=pa.int32()),
            "value": pa.array(["d", "e", "f"], type=pa.string()),
        }
    )
    table.append(batch2)
    table = catalog.load_table("default.rollback_table")
    snapshot_after_batch2 = table.metadata.current_snapshot_id
    print(f"  Inserted batch2 (3 rows) into 'default.rollback_table', snapshot={snapshot_after_batch2}")

    # Rollback to batch1's snapshot
    table.manage_snapshots().rollback_to_snapshot(snapshot_id=snapshot_after_batch1).commit()
    table = catalog.load_table("default.rollback_table")
    print(f"  Rolled back to snapshot={snapshot_after_batch1}, current={table.metadata.current_snapshot_id}")
    print(f"  Snapshot log has {len(table.metadata.snapshot_log)} entries")


def create_testing_namespace(catalog):
    """Create a second namespace 'testing' with a simple table for schema listing tests."""
    try:
        catalog.create_namespace("testing")
        print("Created namespace 'testing'")
    except Exception:
        print("Namespace 'testing' already exists")

    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="value", field_type=StringType(), required=False),
    )

    table = drop_and_create(catalog, "testing.sample", schema)

    data = pa.table(
        {
            "id": pa.array([1, 2, 3], type=pa.int32()),
            "value": pa.array(["x", "y", "z"], type=pa.string()),
        }
    )
    table.append(data)
    print(f"  Inserted {len(data)} rows into 'testing.sample'")


def create_nested_types_table(catalog):
    """Nested types: STRUCT, LIST, MAP columns."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(
            field_id=2,
            name="info",
            field_type=StructType(
                NestedField(field_id=5, name="name", field_type=StringType(), required=False),
                NestedField(field_id=6, name="score", field_type=IntegerType(), required=False),
            ),
            required=False,
        ),
        NestedField(
            field_id=3,
            name="tags",
            field_type=ListType(element_id=7, element_type=StringType(), element_required=False),
            required=False,
        ),
        NestedField(
            field_id=4,
            name="metadata",
            field_type=MapType(
                key_id=8, key_type=StringType(), value_id=9, value_type=IntegerType(), value_required=False
            ),
            required=False,
        ),
    )

    table = drop_and_create(catalog, "default.nested_types", schema)

    # PyArrow struct, list, and map arrays
    info_array = pa.StructArray.from_arrays(
        [
            pa.array(["Alice", "Bob", "Charlie", None], type=pa.string()),
            pa.array([95, 87, 72, None], type=pa.int32()),
        ],
        names=["name", "score"],
    )
    tags_array = pa.array(
        [["python", "data"], ["java"], ["rust", "cpp", "go"], None],
        type=pa.list_(pa.string()),
    )
    metadata_array = pa.array(
        [{"level": 5, "xp": 1200}, {"level": 3}, {}, None],
        type=pa.map_(pa.string(), pa.int32()),
    )

    data = pa.table(
        {
            "id": pa.array([1, 2, 3, 4], type=pa.int32()),
            "info": info_array,
            "tags": tags_array,
            "metadata": metadata_array,
        }
    )

    table.append(data)
    print(f"  Inserted {len(data)} rows into 'default.nested_types'")


def create_add_column_table(catalog):
    """ADD COLUMN via schema evolution after writing data.

    Writes 3 rows under (id, name), adds column 'extra', then writes 3 more rows
    with 'extra' populated. The first Parquet files lack 'extra', so a correct
    reader NULL-fills them.
    """
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="name", field_type=StringType(), required=False),
    )

    table = drop_and_create(catalog, "default.add_column", schema)

    batch1 = pa.table(
        {
            "id": pa.array([1, 2, 3], type=pa.int32()),
            "name": pa.array(["a", "b", "c"], type=pa.string()),
        }
    )
    table.append(batch1)

    with table.update_schema() as update:
        update.add_column("extra", IntegerType())
    print("  Added column 'extra' to 'default.add_column'")

    table = catalog.load_table("default.add_column")
    batch2 = pa.table(
        {
            "id": pa.array([4, 5, 6], type=pa.int32()),
            "name": pa.array(["d", "e", "f"], type=pa.string()),
            "extra": pa.array([40, 50, 60], type=pa.int32()),
        }
    )
    table.append(batch2)
    print("  Inserted 6 rows total into 'default.add_column' (3 before, 3 after ADD COLUMN)")


def create_drop_column_table(catalog):
    """DROP COLUMN via schema evolution after writing data.

    Writes 3 rows under (id, name, obsolete), drops 'obsolete', then writes 3
    more rows under (id, name). The dropped column must not appear in reads.
    """
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="name", field_type=StringType(), required=False),
        NestedField(field_id=3, name="obsolete", field_type=StringType(), required=False),
    )

    table = drop_and_create(catalog, "default.drop_column", schema)

    batch1 = pa.table(
        {
            "id": pa.array([1, 2, 3], type=pa.int32()),
            "name": pa.array(["a", "b", "c"], type=pa.string()),
            "obsolete": pa.array(["x", "y", "z"], type=pa.string()),
        }
    )
    table.append(batch1)

    with table.update_schema() as update:
        update.delete_column("obsolete")
    print("  Dropped column 'obsolete' from 'default.drop_column'")

    table = catalog.load_table("default.drop_column")
    batch2 = pa.table(
        {
            "id": pa.array([4, 5, 6], type=pa.int32()),
            "name": pa.array(["d", "e", "f"], type=pa.string()),
        }
    )
    table.append(batch2)
    print("  Inserted 6 rows total into 'default.drop_column' (3 before, 3 after DROP COLUMN)")


def create_time_table(catalog):
    """TIME type round-trip (time-of-day microseconds)."""
    schema = Schema(
        NestedField(field_id=1, name="id", field_type=IntegerType(), required=False),
        NestedField(field_id=2, name="col_time", field_type=TimeType(), required=False),
    )

    table = drop_and_create(catalog, "default.time_table", schema)

    data = pa.table(
        {
            "id": pa.array([1, 2, 3], type=pa.int32()),
            "col_time": pa.array(
                [
                    datetime.time(0, 0, 0),
                    datetime.time(12, 30, 15),
                    datetime.time(23, 59, 59, 123456),
                ],
                type=pa.time64("us"),
            ),
        }
    )
    table.append(data)
    print(f"  Inserted {len(data)} rows into 'default.time_table'")


def main():
    catalog = create_catalog()

    create_basic_table(catalog)
    create_all_types_table(catalog)
    create_nullable_table(catalog)
    create_partitioned_table(catalog)
    create_date_partitioned_table(catalog)
    create_multi_append_table(catalog)
    create_join_tables(catalog)
    create_schema_evolution_table(catalog)

    create_multi_partition_table(catalog)
    assert_file_count(catalog, "default.multi_partition", 9)

    create_empty_table(catalog)
    create_pinned_schema_table(catalog)
    create_rollback_table(catalog)
    create_testing_namespace(catalog)
    create_nested_types_table(catalog)
    create_add_column_table(catalog)
    create_drop_column_table(catalog)
    create_time_table(catalog)

    print("\nAll test data generated.")


if __name__ == "__main__":
    main()
