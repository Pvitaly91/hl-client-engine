cmake_minimum_required(VERSION 3.25)

project(
    hl-client-engine
    VERSION 0.0.1
    DESCRIPTION "Independent clean-room GoldSrc-compatible client engine"
    LANGUAGES C CXX
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

include(CTest)
include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

find_package(OpenGL REQUIRED)
add_subdirectory(third_party/glad)

add_library(hlclient_hlsdk_headers INTERFACE)
add_library(hlclient::hlsdk_headers ALIAS hlclient_hlsdk_headers)
target_include_directories(
    hlclient_hlsdk_headers
    SYSTEM INTERFACE
        "${PROJECT_SOURCE_DIR}/third_party/halflife-sdk/common"
        "${PROJECT_SOURCE_DIR}/third_party/halflife-sdk/engine"
        "${PROJECT_SOURCE_DIR}/third_party/halflife-sdk/pm_shared"
        "${PROJECT_SOURCE_DIR}/third_party/halflife-sdk/cl_dll"
        "${PROJECT_SOURCE_DIR}/third_party/halflife-sdk/public"
)

add_custom_target(
    halflife-sdk-reference
    SOURCES
        third_party/halflife-sdk/LICENSE
        third_party/halflife-sdk/README.md
)
set_property(TARGET halflife-sdk-reference PROPERTY FOLDER "ThirdParty/HalfLifeSDK")

function(hlclient_add_library target)
    add_library(${target} STATIC ${ARGN})
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/include")
    hlclient_enable_warnings(${target})
    set_property(TARGET ${target} PROPERTY FOLDER "Engine")
endfunction()

hlclient_add_library(
    hlclient_core
    include/hlclient/core/command_line.hpp
    include/hlclient/core/log.hpp
    include/hlclient/core/version.hpp
    src/core/command_line.cpp
    src/core/log.cpp
)
add_library(hlclient::core ALIAS hlclient_core)

hlclient_add_library(
    hlclient_platform
    include/hlclient/platform/sdl_runtime.hpp
    include/hlclient/platform/sdl_window.hpp
    src/platform/sdl_runtime.cpp
    src/platform/sdl_window.cpp
)
add_library(hlclient::platform ALIAS hlclient_platform)
target_link_libraries(hlclient_platform PRIVATE hlclient::core SDL3::SDL3-shared)

hlclient_add_library(
    hlclient_filesystem
    include/hlclient/filesystem/game_paths.hpp
    src/filesystem/game_paths.cpp
)
add_library(hlclient::filesystem ALIAS hlclient_filesystem)
target_link_libraries(hlclient_filesystem PRIVATE hlclient::core)

hlclient_add_library(
    hlclient_network
    include/hlclient/network/network_address.hpp
    include/hlclient/network/network_runtime.hpp
    include/hlclient/network/udp_socket.hpp
    src/network/network_address.cpp
    src/network/network_runtime.cpp
    src/network/udp_socket.cpp
)
add_library(hlclient::network ALIAS hlclient_network)
target_link_libraries(hlclient_network PRIVATE hlclient::core)
if(WIN32)
    target_compile_definitions(hlclient_network PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(hlclient_network PRIVATE ws2_32)
endif()

hlclient_add_library(
    hlclient_goldsrc
    include/hlclient/goldsrc/byte_reader.hpp
    include/hlclient/goldsrc/byte_writer.hpp
    include/hlclient/goldsrc/compatibility.hpp
    src/goldsrc/byte_reader.cpp
    src/goldsrc/byte_writer.cpp
    src/goldsrc/compatibility.cpp
)
add_library(hlclient::goldsrc ALIAS hlclient_goldsrc)
target_link_libraries(
    hlclient_goldsrc
    PUBLIC hlclient::network
    PRIVATE hlclient::core hlclient::hlsdk_headers
)

hlclient_add_library(
    hlclient_client
    include/hlclient/client/client_world_state.hpp
    src/client/client_world_state.cpp
)
add_library(hlclient::client ALIAS hlclient_client)
target_link_libraries(hlclient_client PRIVATE hlclient::core hlclient::goldsrc)

hlclient_add_library(
    hlclient_renderer_api
    include/hlclient/renderer/render_scene.hpp
    include/hlclient/renderer/renderer.hpp
    src/renderer/renderer.cpp
)
add_library(hlclient::renderer_api ALIAS hlclient_renderer_api)
target_link_libraries(hlclient_renderer_api PRIVATE hlclient::core)

hlclient_add_library(
    hlclient_renderer_opengl
    include/hlclient/renderer/opengl/opengl_renderer.hpp
    src/renderer/opengl/opengl_renderer.cpp
)
add_library(hlclient::renderer_opengl ALIAS hlclient_renderer_opengl)
target_link_libraries(
    hlclient_renderer_opengl
    PUBLIC hlclient::renderer_api
    PRIVATE hlclient::platform hlclient::glad OpenGL::GL
)

add_executable(hlclient apps/hlclient/main.cpp)
hlclient_enable_warnings(hlclient)
target_link_libraries(
    hlclient
    PRIVATE
        hlclient::core
        hlclient::platform
        hlclient::filesystem
        hlclient::network
        hlclient::goldsrc
        hlclient::client
        hlclient::renderer_api
        hlclient::renderer_opengl
)
set_target_properties(
    hlclient
    PROPERTIES
        FOLDER "Apps"
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

add_custom_command(
    TARGET hlclient
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3-shared>"
            "$<TARGET_FILE_DIR:hlclient>"
    COMMENT "Copying the SDL3 runtime next to hlclient"
    VERBATIM
)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT hlclient)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
