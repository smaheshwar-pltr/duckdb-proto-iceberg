# Builds the iceberg-cpp submodule at configure time and imports it with find_package().
#
# iceberg-cpp gets its own CMake invocation rather than add_subdirectory() so that DuckDB's
# directory-wide include paths (its vendored fmt, zstd, ...) cannot shadow iceberg-cpp's bundled
# dependencies. Once iceberg-cpp is packaged this can become a plain find_package():
# https://github.com/apache/iceberg-cpp/issues/188
#
# The build is skipped while the submodule revision, its local changes and the settings below
# match the last successful install.

block()
    set(source_dir "${PROJECT_SOURCE_DIR}/third_party/iceberg-cpp")
    set(build_dir "${CMAKE_BINARY_DIR}/_iceberg_build")
    set(install_dir "${CMAKE_BINARY_DIR}/_iceberg_install")
    set(stamp_file "${install_dir}/.iceberg_stamp")

    # Settings are passed as an initial cache file so that list values survive.
    set(init_cache [==[
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "")
set(CMAKE_INSTALL_LIBDIR lib CACHE PATH "")
# Build every dependency from source rather than picking up system packages.
set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER CACHE STRING "")
set(ICEBERG_BUILD_STATIC ON CACHE BOOL "")
set(ICEBERG_BUILD_SHARED OFF CACHE BOOL "")
set(ICEBERG_BUILD_TESTS OFF CACHE BOOL "")
set(ICEBERG_BUILD_BUNDLE ON CACHE BOOL "")
set(ICEBERG_BUILD_REST ON CACHE BOOL "")
set(ICEBERG_S3 ON CACHE BOOL "")
]==])
    string(APPEND init_cache "set(CMAKE_INSTALL_PREFIX [==[${install_dir}]==] CACHE PATH \"\")\n")
    foreach(var IN ITEMS
            CMAKE_BUILD_TYPE
            CMAKE_C_COMPILER
            CMAKE_CXX_COMPILER
            CMAKE_C_COMPILER_LAUNCHER
            CMAKE_CXX_COMPILER_LAUNCHER
            CMAKE_MAKE_PROGRAM
            CMAKE_TOOLCHAIN_FILE
            CMAKE_PREFIX_PATH
            CMAKE_OSX_ARCHITECTURES
            CMAKE_OSX_DEPLOYMENT_TARGET
            CMAKE_OSX_SYSROOT)
        if(NOT "${${var}}" STREQUAL "")
            string(APPEND init_cache "set(${var} [==[${${var}}]==] CACHE STRING \"\")\n")
        endif()
    endforeach()

    execute_process(COMMAND git rev-parse HEAD
                    WORKING_DIRECTORY "${source_dir}"
                    OUTPUT_VARIABLE revision
                    RESULT_VARIABLE git_result
                    ERROR_QUIET)
    execute_process(COMMAND git diff HEAD
                    WORKING_DIRECTORY "${source_dir}"
                    OUTPUT_VARIABLE local_changes
                    ERROR_QUIET)
    string(SHA256 stamp "${revision}${local_changes}${init_cache}")

    # Outside a git checkout changes can't be detected, so the (incremental) build always runs.
    set(previous_stamp "")
    if(git_result EQUAL 0 AND EXISTS "${stamp_file}"
       AND EXISTS "${install_dir}/lib/cmake/iceberg/iceberg-config.cmake")
        file(READ "${stamp_file}" previous_stamp)
    endif()

    if(stamp STREQUAL previous_stamp)
        message(STATUS "[proto_iceberg] iceberg-cpp is up to date")
    else()
        if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
            set(jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
        else()
            cmake_host_system_information(RESULT jobs QUERY NUMBER_OF_LOGICAL_CORES)
        endif()
        file(WRITE "${build_dir}/init_cache.cmake" "${init_cache}")

        message(STATUS "[proto_iceberg] Building iceberg-cpp into ${install_dir}")
        # --fresh drops the cache of a previous configuration, so only the settings above apply.
        # iceberg-cpp turns on CMAKE_COMPILE_WARNING_AS_ERROR, which only --compile-no-warning-as-error overrides.
        execute_process(COMMAND ${CMAKE_COMMAND} --fresh --compile-no-warning-as-error -G "${CMAKE_GENERATOR}"
                                -C "${build_dir}/init_cache.cmake" -S "${source_dir}" -B "${build_dir}"
                        COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND ${CMAKE_COMMAND} --build "${build_dir}" --config "${CMAKE_BUILD_TYPE}"
                                --parallel ${jobs}
                        COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND ${CMAKE_COMMAND} --install "${build_dir}" --config "${CMAKE_BUILD_TYPE}"
                                --prefix "${install_dir}"
                        COMMAND_ERROR_IS_FATAL ANY)
        file(WRITE "${stamp_file}" "${stamp}")
    endif()

    find_package(iceberg CONFIG REQUIRED PATHS "${install_dir}" NO_DEFAULT_PATH)
endblock()
