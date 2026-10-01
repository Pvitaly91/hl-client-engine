foreach(case IN ITEMS allowed direct alias transitive generator source object include)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -S "${HLCLIENT_ROOT}/tests/cmake/game_boundary_fixture"
            -B "${HLCLIENT_TEST_BINARY}/${case}"
            "-DHLCLIENT_ROOT=${HLCLIENT_ROOT}" "-DCASE=${case}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(case STREQUAL "allowed")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Allowed API dependency was rejected: ${output}${error}")
        endif()
    elseif(result EQUAL 0 OR NOT "${output}${error}" MATCHES "Game boundary violation:")
        message(FATAL_ERROR "Guard did not reject ${case}: ${output}${error}")
    endif()
endforeach()
message(STATUS "Game boundary guard: allowed + direct/alias/transitive/generator/source/object/include cases passed")
