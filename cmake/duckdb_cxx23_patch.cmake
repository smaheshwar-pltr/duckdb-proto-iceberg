# DuckDB v1.5.1's profiling_utils.hpp does not compile in a C++23 translation unit under libc++
# (Apple's standard library): QueryMetrics resets a unique_ptr<ActiveTimer> in an inline member
# function before ActiveTimer is complete, and libc++ instantiates the constexpr unique_ptr members
# eagerly in C++23. The patch moves those member functions below ActiveTimer's definition. It is
# applied once to the DuckDB tree being configured.

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
