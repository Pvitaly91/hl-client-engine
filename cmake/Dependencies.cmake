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
