include_guard(GLOBAL)

option(HLCLIENT_WARNINGS_AS_ERRORS "Treat warnings in hl-client-engine code as errors" OFF)

function(hlclient_enable_warnings target)
    if(MSVC)
        # Visual Studio/MSBuild may compile several translation units in one
        # target concurrently. Serialize access to the compiler PDB so the
        # documented plain `cmake --build` command is reliable.
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /FS)
        if(HLCLIENT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
        )
        if(HLCLIENT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
