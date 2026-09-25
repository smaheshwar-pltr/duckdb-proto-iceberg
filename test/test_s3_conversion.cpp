#include "catch.hpp"
#include "conversion.hpp"

#include <string>
#include <unordered_map>

using namespace duckdb;
using namespace duckdb::conversion;

namespace {

using Properties = std::unordered_map<std::string, std::string>;
using Secret = case_insensitive_tree_t<Value>;

std::string OptionString(const case_insensitive_map_t<Value> &options, const std::string &key) {
	return options.at(key).ToString();
}

bool OptionBool(const case_insensitive_map_t<Value> &options, const std::string &key) {
	const auto &value = options.at(key);
	REQUIRE(value.type() == LogicalType::BOOLEAN);
	return BooleanValue::Get(value);
}

} // namespace

TEST_CASE("DuckDB secret: plain keys pass through", "[s3_conversion]") {
	auto props = ConvertS3SecretToIcebergProperties(Secret {{"key_id", Value("AKIA")},
	                                                        {"secret", Value("shh")},
	                                                        {"session_token", Value("tok")},
	                                                        {"region", Value("eu-west-1")}});
	REQUIRE(props == Properties {{"s3.access-key-id", "AKIA"},
	                             {"s3.secret-access-key", "shh"},
	                             {"s3.session-token", "tok"},
	                             {"s3.region", "eu-west-1"}});
}

TEST_CASE("DuckDB secret: empty, null and unrelated keys are omitted", "[s3_conversion]") {
	auto props = ConvertS3SecretToIcebergProperties(Secret {{"key_id", Value("")},
	                                                        {"session_token", Value()},
	                                                        {"endpoint", Value("")},
	                                                        {"url_style", Value("")},
	                                                        {"use_ssl", Value(LogicalType::BOOLEAN)},
	                                                        {"kms_key_id", Value("kms")}});
	REQUIRE(props.empty());
}

TEST_CASE("DuckDB secret: URL style maps to path-style access", "[s3_conversion]") {
	REQUIRE(ConvertS3SecretToIcebergProperties(Secret {{"url_style", Value("path")}}) ==
	        Properties {{"s3.path-style-access", "true"}});
	REQUIRE(ConvertS3SecretToIcebergProperties(Secret {{"url_style", Value("vhost")}}) ==
	        Properties {{"s3.path-style-access", "false"}});
}

TEST_CASE("DuckDB secret: use_ssl maps to SSL enabled", "[s3_conversion]") {
	REQUIRE(ConvertS3SecretToIcebergProperties(Secret {{"use_ssl", Value::BOOLEAN(true)}}) ==
	        Properties {{"s3.ssl.enabled", "true"}});
	REQUIRE(ConvertS3SecretToIcebergProperties(Secret {{"use_ssl", Value::BOOLEAN(false)}}) ==
	        Properties {{"s3.ssl.enabled", "false"}});
}

TEST_CASE("DuckDB secret: scheme-less endpoint passes through with SSL", "[s3_conversion]") {
	// iceberg-cpp derives the endpoint's scheme from s3.ssl.enabled.
	auto props = ConvertS3SecretToIcebergProperties(Secret {
	    {"endpoint", Value("localhost:9000")}, {"url_style", Value("path")}, {"use_ssl", Value::BOOLEAN(false)}});
	REQUIRE(props == Properties {{"s3.endpoint", "localhost:9000"},
	                             {"s3.path-style-access", "true"},
	                             {"s3.ssl.enabled", "false"}});
}

TEST_CASE("DuckDB secret: keys match case-insensitively", "[s3_conversion]") {
	auto props = ConvertS3SecretToIcebergProperties(Secret {{"KEY_ID", Value("AKIA")}, {"Endpoint", Value("host")}});
	REQUIRE(props == Properties {{"s3.access-key-id", "AKIA"}, {"s3.endpoint", "host"}});
}

TEST_CASE("Iceberg properties: plain keys pass through", "[s3_conversion]") {
	auto options = ConvertIcebergPropertiesToS3Secret({{"s3.access-key-id", "AKIA"},
	                                                   {"s3.secret-access-key", "shh"},
	                                                   {"s3.session-token", "tok"},
	                                                   {"s3.region", "eu-west-1"}});
	REQUIRE(options.size() == 4);
	REQUIRE(OptionString(options, "key_id") == "AKIA");
	REQUIRE(OptionString(options, "secret") == "shh");
	REQUIRE(OptionString(options, "session_token") == "tok");
	REQUIRE(OptionString(options, "region") == "eu-west-1");
}

TEST_CASE("Iceberg properties: empty and unrelated properties are omitted", "[s3_conversion]") {
	auto options = ConvertIcebergPropertiesToS3Secret({{"s3.access-key-id", ""},
	                                                   {"s3.endpoint", ""},
	                                                   {"s3.path-style-access", "maybe"},
	                                                   {"s3.ssl.enabled", "yes"},
	                                                   {"s3.connect-timeout-ms", "1000"},
	                                                   {"client.region", "us-east-1"}});
	REQUIRE(options.empty());
}

TEST_CASE("Iceberg properties: path-style access maps to URL style", "[s3_conversion]") {
	REQUIRE(OptionString(ConvertIcebergPropertiesToS3Secret({{"s3.path-style-access", "true"}}), "url_style") ==
	        "path");
	REQUIRE(OptionString(ConvertIcebergPropertiesToS3Secret({{"s3.path-style-access", "false"}}), "url_style") ==
	        "vhost");
}

TEST_CASE("Iceberg properties: SSL enabled maps to use_ssl", "[s3_conversion]") {
	REQUIRE(OptionBool(ConvertIcebergPropertiesToS3Secret({{"s3.ssl.enabled", "true"}}), "use_ssl"));
	REQUIRE_FALSE(OptionBool(ConvertIcebergPropertiesToS3Secret({{"s3.ssl.enabled", "false"}}), "use_ssl"));
}

TEST_CASE("Iceberg properties: http endpoint drops the scheme and disables SSL", "[s3_conversion]") {
	auto options = ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "http://minio:9000"}});
	REQUIRE(options.size() == 2);
	REQUIRE(OptionString(options, "endpoint") == "minio:9000");
	REQUIRE_FALSE(OptionBool(options, "use_ssl"));
}

TEST_CASE("Iceberg properties: https endpoint drops the scheme and enables SSL", "[s3_conversion]") {
	auto options = ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "https://s3.eu-west-1.amazonaws.com"}});
	REQUIRE(options.size() == 2);
	REQUIRE(OptionString(options, "endpoint") == "s3.eu-west-1.amazonaws.com");
	REQUIRE(OptionBool(options, "use_ssl"));
}

TEST_CASE("Iceberg properties: endpoint scheme takes precedence over SSL enabled", "[s3_conversion]") {
	REQUIRE_FALSE(OptionBool(
	    ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "http://minio:9000"}, {"s3.ssl.enabled", "true"}}),
	    "use_ssl"));
	REQUIRE(OptionBool(
	    ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "https://storage"}, {"s3.ssl.enabled", "false"}}),
	    "use_ssl"));
}

TEST_CASE("Iceberg properties: scheme-less endpoint keeps SSL from SSL enabled", "[s3_conversion]") {
	auto without_ssl =
	    ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "localhost:9000"}, {"s3.ssl.enabled", "false"}});
	REQUIRE(OptionString(without_ssl, "endpoint") == "localhost:9000");
	REQUIRE_FALSE(OptionBool(without_ssl, "use_ssl"));

	// Without s3.ssl.enabled, use_ssl is left unset so DuckDB's s3_use_ssl setting applies.
	auto unset = ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "localhost:9000"}});
	REQUIRE(OptionString(unset, "endpoint") == "localhost:9000");
	REQUIRE_FALSE(unset.contains("use_ssl"));
}

TEST_CASE("Iceberg properties: endpoint path is kept and trailing slashes dropped", "[s3_conversion]") {
	REQUIRE(OptionString(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "http://gateway:8080/s3"}}), "endpoint") ==
	        "gateway:8080/s3");
	REQUIRE(OptionString(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "https://gateway/s3/"}}), "endpoint") ==
	        "gateway/s3");
	REQUIRE(OptionString(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "http://minio:9000/"}}), "endpoint") ==
	        "minio:9000");
	REQUIRE(OptionString(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "localhost:9000/"}}), "endpoint") ==
	        "localhost:9000");
}

TEST_CASE("Iceberg properties: endpoint of only a scheme is omitted", "[s3_conversion]") {
	REQUIRE(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "http://"}}).empty());
}

TEST_CASE("DuckDB secret round-trips through iceberg-cpp properties", "[s3_conversion]") {
	auto options = ConvertIcebergPropertiesToS3Secret(
	    ConvertS3SecretToIcebergProperties(Secret {{"key_id", Value("AKIA")},
	                                               {"secret", Value("shh")},
	                                               {"endpoint", Value("localhost:9000")},
	                                               {"url_style", Value("path")},
	                                               {"use_ssl", Value::BOOLEAN(false)}}));
	REQUIRE(options.size() == 5);
	REQUIRE(OptionString(options, "key_id") == "AKIA");
	REQUIRE(OptionString(options, "secret") == "shh");
	REQUIRE(OptionString(options, "endpoint") == "localhost:9000");
	REQUIRE(OptionString(options, "url_style") == "path");
	REQUIRE_FALSE(OptionBool(options, "use_ssl"));
}
