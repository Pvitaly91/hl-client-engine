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
    include/hlclient/filesystem/file_system.hpp
    include/hlclient/filesystem/game_paths.hpp
    include/hlclient/filesystem/rooted_file_system.hpp
    src/filesystem/game_paths.cpp
    src/filesystem/rooted_file_system.cpp
)
add_library(hlclient::filesystem ALIAS hlclient_filesystem)
target_link_libraries(hlclient_filesystem PRIVATE hlclient::core)

hlclient_add_library(
    hlclient_asset_api
    include/hlclient/assets/asset_importer.hpp
    include/hlclient/assets/asset_importer_registry.hpp
    include/hlclient/assets/asset_source.hpp
    include/hlclient/assets/asset_types.hpp
    src/assets/asset_source.cpp
)
add_library(hlclient::asset_api ALIAS hlclient_asset_api)
target_link_libraries(hlclient_asset_api PRIVATE hlclient::core)
set_property(TARGET hlclient_asset_api PROPERTY FOLDER "Engine/Assets")

hlclient_add_library(
    hlclient_asset_manager
    include/hlclient/assets/asset_manager.hpp
    src/assets/asset_manager.cpp
)
add_library(hlclient::asset_manager ALIAS hlclient_asset_manager)
target_link_libraries(
    hlclient_asset_manager
    PUBLIC hlclient::asset_api hlclient::filesystem
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_asset_manager PROPERTY FOLDER "Engine/Assets")

hlclient_add_library(
    hlclient_network
    include/hlclient/network/datagram_transport.hpp
    include/hlclient/network/network_address.hpp
    include/hlclient/network/network_runtime.hpp
    include/hlclient/network/udp_socket.hpp
    src/network/udp_datagram_transport.cpp
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
    include/hlclient/goldsrc/challenge_protocol.hpp
    include/hlclient/goldsrc/connect_request.hpp
    include/hlclient/goldsrc/connect_response.hpp
    include/hlclient/goldsrc/connectionless_packet.hpp
    include/hlclient/goldsrc/info_string.hpp
    src/goldsrc/byte_reader.cpp
    src/goldsrc/byte_writer.cpp
    src/goldsrc/challenge_protocol.cpp
    src/goldsrc/connect_request.cpp
    src/goldsrc/connect_response.cpp
    src/goldsrc/connectionless_packet.cpp
    src/goldsrc/info_string.cpp
)
add_library(hlclient::goldsrc ALIAS hlclient_goldsrc)
target_link_libraries(hlclient_goldsrc PUBLIC hlclient::network PRIVATE hlclient::core)
set_property(TARGET hlclient_goldsrc PROPERTY FOLDER "Engine/Network/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_netchan
    include/hlclient/goldsrc/netchan_bootstrap_stage.hpp
    include/hlclient/goldsrc/netchan_channel.hpp
    include/hlclient/goldsrc/netchan_packet.hpp
    include/hlclient/goldsrc/netchan_payload_transform.hpp
    include/hlclient/goldsrc/netchan_reassembly.hpp
    include/hlclient/goldsrc/netchan_sequence.hpp
    src/goldsrc/netchan_bootstrap_stage.cpp
    src/goldsrc/netchan_channel.cpp
    src/goldsrc/netchan_packet.cpp
    src/goldsrc/netchan_payload_transform.cpp
    src/goldsrc/netchan_reassembly.cpp
    src/goldsrc/netchan_sequence.cpp
)
add_library(hlclient::goldsrc_netchan ALIAS hlclient_goldsrc_netchan)
target_link_libraries(
    hlclient_goldsrc_netchan
    PUBLIC hlclient::goldsrc hlclient::network
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_goldsrc_netchan PROPERTY FOLDER "Engine/Network/GoldSrc")

hlclient_add_library(
    hlclient_auth
    include/hlclient/auth/authentication_provider.hpp
    src/auth/authentication_provider.cpp
)
add_library(hlclient::auth ALIAS hlclient_auth)
target_link_libraries(hlclient_auth PUBLIC hlclient::goldsrc hlclient::network)
set_property(TARGET hlclient_auth PROPERTY FOLDER "Engine/Authentication")

hlclient_add_library(
    hlclient_goldsrc_client
    include/hlclient/goldsrc/challenge_exchange.hpp
    include/hlclient/goldsrc/connect_request_stage.hpp
    include/hlclient/goldsrc/connect_response_wait.hpp
    src/goldsrc/challenge_exchange.cpp
    src/goldsrc/connect_request_stage.cpp
    src/goldsrc/connect_response_wait.cpp
)
add_library(hlclient::goldsrc_client ALIAS hlclient_goldsrc_client)
target_link_libraries(
    hlclient_goldsrc_client
    PUBLIC
        hlclient::auth
        hlclient::goldsrc
        hlclient::goldsrc_netchan
        hlclient::network
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_goldsrc_client PROPERTY FOLDER "Engine/Network/GoldSrc")

hlclient_add_library(
    hlclient_app_support
    include/hlclient/app/authentication_material_file.hpp
    include/hlclient/app/explicit_file_authentication_provider.hpp
    src/app/authentication_material_file.cpp
    src/app/explicit_file_authentication_provider.cpp
)
add_library(hlclient::app_support ALIAS hlclient_app_support)
target_link_libraries(hlclient_app_support PUBLIC hlclient::auth)
set_property(TARGET hlclient_app_support PROPERTY FOLDER "Apps")

hlclient_add_library(
    hlclient_client
    include/hlclient/client/client_world_state.hpp
    src/client/client_world_state.cpp
)
add_library(hlclient::client ALIAS hlclient_client)
target_link_libraries(hlclient_client PRIVATE hlclient::core)
set_property(TARGET hlclient_client PROPERTY FOLDER "Engine/Client")

hlclient_add_library(
    hlclient_renderer_api
    include/hlclient/renderer/render_scene.hpp
    include/hlclient/renderer/renderer.hpp
    src/renderer/renderer.cpp
)
add_library(hlclient::renderer_api ALIAS hlclient_renderer_api)
target_link_libraries(hlclient_renderer_api PRIVATE hlclient::core)
set_property(TARGET hlclient_renderer_api PROPERTY FOLDER "Engine/Renderer")

hlclient_add_library(
    hlclient_scene_api
    include/hlclient/client/client_scene_source.hpp
    src/client/client_scene_source.cpp
)
add_library(hlclient::scene_api ALIAS hlclient_scene_api)
target_link_libraries(
    hlclient_scene_api
    PUBLIC hlclient::client hlclient::renderer_api
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_scene_api PROPERTY FOLDER "Engine/Client")

hlclient_add_library(
    hlclient_renderer_null
    include/hlclient/renderer/null/null_renderer.hpp
    src/renderer/null/null_renderer.cpp
)
add_library(hlclient::renderer_null ALIAS hlclient_renderer_null)
target_link_libraries(
    hlclient_renderer_null
    PUBLIC hlclient::renderer_api
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_renderer_null PROPERTY FOLDER "Engine/Renderer")

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
set_property(TARGET hlclient_renderer_opengl PROPERTY FOLDER "Engine/Renderer")

add_executable(hlclient apps/hlclient/main.cpp)
hlclient_enable_warnings(hlclient)
target_link_libraries(
    hlclient
    PRIVATE
        hlclient::core
        hlclient::app_support
        hlclient::platform
        hlclient::filesystem
        hlclient::asset_api
        hlclient::asset_manager
        hlclient::network
        hlclient::goldsrc
        hlclient::goldsrc_client
        hlclient::client
        hlclient::scene_api
        hlclient::renderer_api
        hlclient::renderer_null
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
