# Apple/libc++ only: DuckDB v1.5.1 profiling_utils.hpp fails to compile in a C++23 TU
# under libc++ (see scripts/duckdb-cxx23-patches/profiling_utils.hpp.patch).
if(APPLE)
    block()
        get_filename_component(_duckdb_dir "${CMAKE_CURRENT_LIST_DIR}/../duckdb" ABSOLUTE)
        set(_patch "${CMAKE_CURRENT_LIST_DIR}/duckdb-cxx23-patches/profiling_utils.hpp.patch")
        execute_process(COMMAND git -C "${_duckdb_dir}" apply --reverse --check "${_patch}"
                        RESULT_VARIABLE _already ERROR_QUIET)
        if(NOT _already EQUAL 0)
            execute_process(COMMAND git -C "${_duckdb_dir}" apply "${_patch}"
                            RESULT_VARIABLE _rc)
            if(NOT _rc EQUAL 0)
                message(FATAL_ERROR "[proto_iceberg] cannot apply ${_patch} "
                                    "(DuckDB submodule not at the expected v1.5.1 tree?)")
            endif()
            message(STATUS "[proto_iceberg] applied DuckDB C++23 patch: profiling_utils.hpp")
        endif()
    endblock()
endif()
