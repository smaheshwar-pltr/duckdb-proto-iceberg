#include "catch.hpp"
#include "s3_conversion.hpp"

#include <map>
#include <string>
#include <unordered_map>

using namespace duckdb;
using namespace duckdb::conversion;

namespace {

using Properties = std::unordered_map<std::string, std::string>;
using Secret = case_insensitive_tree_t<Value>;

/// Joins entries as key=value, sorted by key and separated by ", ".
std::string Join(const std::map<std::string, std::string> &entries) {
	std::string joined;
	for (const auto &[key, value] : entries) {
		if (!joined.empty()) {
			joined += ", ";
		}
		joined += key + "=" + value;
	}
	return joined;
}

/// Renders iceberg-cpp properties with values verbatim, e.g. "s3.endpoint=localhost:9000, s3.ssl.enabled=false".
std::string Render(const Properties &properties) {
	return Join(std::map<std::string, std::string>(properties.begin(), properties.end()));
}

/// Renders DuckDB secret options with values as SQL literals, so that e.g. the string 'false' and the boolean false
/// differ: "endpoint='minio:9000', use_ssl=false".
template <typename Options>
std::string Render(const Options &options) {
	std::map<std::string, std::string> entries;
	for (const auto &[key, value] : options) {
		entries[key] = value.ToSQLString();
	}
	return Join(entries);
}

} // namespace

TEST_CASE("secret to properties: plain keys pass through", "[s3_conversion]") {
	REQUIRE(Render(ConvertS3SecretToIcebergProperties(Secret {{"key_id", Value("AKIA")},
	                                                          {"secret", Value("shh")},
	                                                          {"session_token", Value("tok")},
	                                                          {"region", Value("eu-west-1")}})) ==
	        "client.region=eu-west-1, s3.access-key-id=AKIA, s3.secret-access-key=shh, s3.session-token=tok");
}

TEST_CASE("secret to properties: empty, null and unrelated keys are omitted", "[s3_conversion]") {
	REQUIRE(Render(ConvertS3SecretToIcebergProperties(Secret {{"key_id", Value("")},
	                                                          {"session_token", Value()},
	                                                          {"endpoint", Value("")},
	                                                          {"url_style", Value("")},
	                                                          {"use_ssl", Value(LogicalType::BOOLEAN)},
	                                                          {"kms_key_id", Value("kms")}})) == "");
}

TEST_CASE("secret to properties: URL style maps to path-style access", "[s3_conversion]") {
	REQUIRE(Render(ConvertS3SecretToIcebergProperties(Secret {{"url_style", Value("path")}})) ==
	        "s3.path-style-access=true");
	REQUIRE(Render(ConvertS3SecretToIcebergProperties(Secret {{"url_style", Value("vhost")}})) ==
	        "s3.path-style-access=false");
}

TEST_CASE("secret to properties: use_ssl maps to SSL enabled", "[s3_conversion]") {
	REQUIRE(Render(ConvertS3SecretToIcebergProperties(Secret {{"use_ssl", Value::BOOLEAN(true)}})) ==
	        "s3.ssl.enabled=true");
	REQUIRE(Render(ConvertS3SecretToIcebergProperties(Secret {{"use_ssl", Value::BOOLEAN(false)}})) ==
	        "s3.ssl.enabled=false");
}

TEST_CASE("secret to properties: endpoint passes through verbatim", "[s3_conversion]") {
	REQUIRE(Render(ConvertS3SecretToIcebergProperties(
	            Secret {{"endpoint", Value("localhost:9000")}, {"use_ssl", Value::BOOLEAN(false)}})) ==
	        "s3.endpoint=localhost:9000, s3.ssl.enabled=false");
}

TEST_CASE("secret to properties: keys match case-insensitively", "[s3_conversion]") {
	REQUIRE(Render(ConvertS3SecretToIcebergProperties(Secret {
	            {"KEY_ID", Value("AKIA")}, {"Endpoint", Value("host")}})) == "s3.access-key-id=AKIA, s3.endpoint=host");
}

TEST_CASE("properties to secret: plain keys pass through", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.access-key-id", "AKIA"},
	                                                   {"s3.secret-access-key", "shh"},
	                                                   {"s3.session-token", "tok"},
	                                                   {"client.region", "eu-west-1"}})) ==
	        "key_id='AKIA', region='eu-west-1', secret='shh', session_token='tok'");
}

TEST_CASE("properties to secret: empty and unrelated properties are omitted", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.access-key-id", ""},
	                                                   {"s3.endpoint", ""},
	                                                   {"s3.connect-timeout-ms", "1000"},
	                                                   {"s3.region", "us-east-1"}})) == "");
}

TEST_CASE("properties to secret: non-boolean path-style access and SSL enabled are ignored", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret(
	            {{"s3.path-style-access", "True"}, {"s3.ssl.enabled", "FALSE"}})) == "");
}

TEST_CASE("properties to secret: path-style access maps to URL style", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.path-style-access", "true"}})) == "url_style='path'");
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.path-style-access", "false"}})) == "url_style='vhost'");
}

TEST_CASE("properties to secret: SSL enabled maps to use_ssl", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.ssl.enabled", "true"}})) == "use_ssl=true");
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.ssl.enabled", "false"}})) == "use_ssl=false");
}

TEST_CASE("properties to secret: http endpoint drops the scheme and disables SSL", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "http://minio:9000"}})) ==
	        "endpoint='minio:9000', use_ssl=false");
}

TEST_CASE("properties to secret: https endpoint drops the scheme and enables SSL", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "https://s3.eu-west-1.amazonaws.com"}})) ==
	        "endpoint='s3.eu-west-1.amazonaws.com', use_ssl=true");
}

TEST_CASE("properties to secret: endpoint scheme takes precedence over SSL enabled", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret(
	            {{"s3.endpoint", "http://minio:9000"}, {"s3.ssl.enabled", "true"}})) ==
	        "endpoint='minio:9000', use_ssl=false");
	REQUIRE(
	    Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "https://storage"}, {"s3.ssl.enabled", "false"}})) ==
	    "endpoint='storage', use_ssl=true");
}

TEST_CASE("properties to secret: scheme-less endpoint takes SSL from SSL enabled", "[s3_conversion]") {
	REQUIRE(
	    Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "localhost:9000"}, {"s3.ssl.enabled", "false"}})) ==
	    "endpoint='localhost:9000', use_ssl=false");
}

TEST_CASE("properties to secret: scheme-less endpoint without SSL enabled leaves use_ssl unset", "[s3_conversion]") {
	// DuckDB's s3_use_ssl setting then applies.
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "localhost:9000"}})) ==
	        "endpoint='localhost:9000'");
}

TEST_CASE("properties to secret: endpoint path is kept", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "http://gateway:8080/s3"}})) ==
	        "endpoint='gateway:8080/s3', use_ssl=false");
}

TEST_CASE("properties to secret: trailing slashes are dropped", "[s3_conversion]") {
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "https://gateway/s3/"}})) ==
	        "endpoint='gateway/s3', use_ssl=true");
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "localhost:9000/"}})) ==
	        "endpoint='localhost:9000'");
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret({{"s3.endpoint", "http://minio:9000//"}})) ==
	        "endpoint='minio:9000', use_ssl=false");
}

TEST_CASE("secret round-trips through iceberg-cpp properties", "[s3_conversion]") {
	Secret secret {{"key_id", Value("AKIA")},
	               {"secret", Value("shh")},
	               {"session_token", Value("tok")},
	               {"region", Value("eu-west-1")},
	               {"endpoint", Value("localhost:9000")},
	               {"url_style", Value("path")},
	               {"use_ssl", Value::BOOLEAN(false)}};
	REQUIRE(Render(ConvertIcebergPropertiesToS3Secret(ConvertS3SecretToIcebergProperties(secret))) == Render(secret));
}
