include_guard(GLOBAL)

include(FetchContent)

# SDL 3.4.14, pinned to the release commit.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG 147a8ee32dbf9ac02f3794964490687b6bbda1bc
    GIT_PROGRESS TRUE
    SYSTEM
)

FetchContent_MakeAvailable(SDL3)

foreach(target SDL3-shared SDL3_test SDL_uclibc)
    if(TARGET ${target})
        set_property(TARGET ${target} PROPERTY FOLDER "ThirdParty/SDL3")
    endif()
endforeach()

# Official bzip2 1.0.8 release, pinned by the Sourceware-published SHA-512.
# GoldSrc Protocol 48 uses a BZ2\0 envelope for the captured initial service
# batch. Build only an in-process static library and compile out stdio entry
# points so the sign-on decoder has no filesystem surface.
FetchContent_Declare(
    bzip2
    URL https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    URL_HASH
        SHA512=083f5e675d73f3233c7930ebe20425a533feedeaaa9d8cc86831312a6581cefbe6ed0d08d2fa89be81082f2a5abdabca8b3c080bf97218a1bd59dc118a30b9f3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(bzip2)

set(HLCLIENT_BZIP2_NO_STDIO_SOURCE
    "${PROJECT_SOURCE_DIR}/src/third_party/bzip2_no_stdio.c")
add_library(
    hlclient_bzip2 STATIC
    ${bzip2_SOURCE_DIR}/blocksort.c
    ${bzip2_SOURCE_DIR}/bzlib.c
    ${bzip2_SOURCE_DIR}/compress.c
    ${bzip2_SOURCE_DIR}/crctable.c
    ${bzip2_SOURCE_DIR}/decompress.c
    ${bzip2_SOURCE_DIR}/huffman.c
    ${bzip2_SOURCE_DIR}/randtable.c
    ${HLCLIENT_BZIP2_NO_STDIO_SOURCE}
    ${bzip2_SOURCE_DIR}/bzlib.h
    ${bzip2_SOURCE_DIR}/bzlib_private.h
    ${bzip2_SOURCE_DIR}/LICENSE
)
add_library(hlclient::bzip2 ALIAS hlclient_bzip2)
target_compile_definitions(hlclient_bzip2 PUBLIC BZ_NO_STDIO)
target_include_directories(hlclient_bzip2 SYSTEM PUBLIC ${bzip2_SOURCE_DIR})
# Upstream C stays isolated from first-party warning policy, but the small
# project-owned no-stdio invariant hook must satisfy the same strict gate as
# the rest of hl-client-engine. `/permissive-` is intentionally C++-only.
if(MSVC)
    set_property(
        SOURCE ${HLCLIENT_BZIP2_NO_STDIO_SOURCE}
        APPEND PROPERTY COMPILE_OPTIONS /W4)
    if(HLCLIENT_WARNINGS_AS_ERRORS)
        set_property(
            SOURCE ${HLCLIENT_BZIP2_NO_STDIO_SOURCE}
            APPEND PROPERTY COMPILE_OPTIONS /WX)
    endif()
else()
    set_property(
        SOURCE ${HLCLIENT_BZIP2_NO_STDIO_SOURCE}
        APPEND PROPERTY COMPILE_OPTIONS
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    if(HLCLIENT_WARNINGS_AS_ERRORS)
        set_property(
            SOURCE ${HLCLIENT_BZIP2_NO_STDIO_SOURCE}
            APPEND PROPERTY COMPILE_OPTIONS -Werror)
    endif()
endif()
set_property(TARGET hlclient_bzip2 PROPERTY FOLDER "ThirdParty/BZip2")

if(BUILD_TESTING)
    # Catch2 3.15.3, pinned to the peeled release commit.
    set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG 8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb
        GIT_PROGRESS TRUE
        SYSTEM
    )
    FetchContent_MakeAvailable(Catch2)

    foreach(target Catch2 Catch2WithMain)
        if(TARGET ${target})
            set_property(TARGET ${target} PROPERTY FOLDER "ThirdParty/Catch2")
        endif()
    endforeach()

    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
