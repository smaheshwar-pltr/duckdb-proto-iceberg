# Building

Requirements:

- CMake 3.25+ and Ninja
- A C++23 compiler (CI uses GCC 14)
- libcurl, OpenSSL and zlib development packages (`libcurl4-openssl-dev libssl-dev zlib1g-dev` on Debian/Ubuntu)

The first build also builds iceberg-cpp and its dependencies, so it takes a while. Set `CMAKE_BUILD_PARALLEL_LEVEL` to limit parallelism if memory is tight.

The Main Extension Distribution Pipeline workflow attaches Linux and macOS binaries to each run as artifacts.
