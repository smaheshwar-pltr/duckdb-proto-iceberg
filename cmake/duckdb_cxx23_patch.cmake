# DuckDB v1.5.1's profiling_utils.hpp doesn't compile as C++23 under libc++, which instantiates unique_ptr members
# eagerly; the patch moves the functions using an incomplete ActiveTimer below its definition.

if(APPLE)
    block()
        set(patch "${CMAKE_CURRENT_LIST_DIR}/duckdb_cxx23.patch")
        execute_process(COMMAND git apply --reverse --check "${patch}"
                        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                        RESULT_VARIABLE already_applied
                        ERROR_QUIET)
        if(NOT already_applied EQUAL 0)
            execute_process(COMMAND git apply "${patch}"
                            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                            RESULT_VARIABLE result)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "[proto_iceberg] Cannot apply ${patch} to the DuckDB tree at "
                                    "${CMAKE_SOURCE_DIR}. Is it still DuckDB v1.5.1?")
            endif()
            message(STATUS "[proto_iceberg] Applied ${patch}")
        endif()
    endblock()
endif()
