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
    hlclient_hash_md5
    include/hlclient/hash/md5.hpp
    src/hash/md5.cpp
)
add_library(hlclient::hash_md5 ALIAS hlclient_hash_md5)
target_link_libraries(hlclient_hash_md5 PUBLIC hlclient::core)
set_property(TARGET hlclient_hash_md5 PROPERTY FOLDER "Engine/Core/Hash")

hlclient_add_library(
    hlclient_local_resources
    include/hlclient/local_resources/local_resource_identity.hpp
    include/hlclient/local_resources/local_resource_search_roots.hpp
    include/hlclient/local_resources/local_virtual_resource_name.hpp
    include/hlclient/local_resources/local_read_only_file.hpp
    include/hlclient/local_resources/local_resource_locator.hpp
    include/hlclient/local_resources/local_resource_environment.hpp
    include/hlclient/local_resources/local_resource_resolver.hpp
    src/local_resources/win32_local_resource_detail.hpp
    src/local_resources/local_resource_search_roots.cpp
    src/local_resources/local_virtual_resource_name.cpp
    src/local_resources/local_read_only_file.cpp
    src/local_resources/local_resource_locator.cpp
    src/local_resources/local_resource_environment.cpp
    src/local_resources/local_resource_resolver.cpp
)
add_library(hlclient::local_resources ALIAS hlclient_local_resources)
target_link_libraries(hlclient_local_resources PUBLIC hlclient::core)
set_property(
    TARGET hlclient_local_resources PROPERTY FOLDER "Engine/Resources/Local")
if(WIN32)
    target_compile_definitions(
        hlclient_local_resources PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

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
    include/hlclient/assets/world_lightmap_types.hpp
    include/hlclient/assets/world_texture_types.hpp
    src/assets/asset_source.cpp
    src/assets/world_lightmap_types.cpp
    src/assets/world_texture_types.cpp
)
add_library(hlclient::asset_api ALIAS hlclient_asset_api)
target_link_libraries(hlclient_asset_api PRIVATE hlclient::core)
set_property(TARGET hlclient_asset_api PROPERTY FOLDER "Engine/Assets")

hlclient_add_library(
    hlclient_world_spatial
    include/hlclient/world_spatial/world_spatial_types.hpp
    include/hlclient/world_spatial/world_spatial_query.hpp
    src/world_spatial/world_spatial_types.cpp
    src/world_spatial/world_spatial_query.cpp
)
add_library(hlclient::world_spatial ALIAS hlclient_world_spatial)
target_link_libraries(hlclient_world_spatial PUBLIC hlclient::asset_api)
set_property(
    TARGET hlclient_world_spatial
    PROPERTY FOLDER "Engine/Renderer/World/Visibility")

hlclient_add_library(
    hlclient_goldsrc_spatial
    include/hlclient/goldsrc/spatial/goldsrc_pvs_decoder.hpp
    include/hlclient/goldsrc/spatial/goldsrc_spatial_package_builder.hpp
    src/goldsrc/spatial/goldsrc_pvs_decoder.cpp
    src/goldsrc/spatial/goldsrc_spatial_package_builder.cpp
)
add_library(hlclient::goldsrc_spatial ALIAS hlclient_goldsrc_spatial)
target_link_libraries(
    hlclient_goldsrc_spatial
    PUBLIC hlclient::asset_api hlclient::world_spatial)
set_property(
    TARGET hlclient_goldsrc_spatial
    PROPERTY FOLDER "Engine/Assets/Formats/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_indexed_texture
    include/hlclient/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.hpp
    src/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.cpp
)
add_library(
    hlclient::goldsrc_indexed_texture ALIAS
    hlclient_goldsrc_indexed_texture)
target_link_libraries(
    hlclient_goldsrc_indexed_texture PUBLIC hlclient::asset_api)
set_property(
    TARGET hlclient_goldsrc_indexed_texture
    PROPERTY FOLDER "Engine/Assets/Formats/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_bsp
    include/hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp
    include/hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp
    include/hlclient/goldsrc/bsp/goldsrc_bsp_texture_source_parser.hpp
    include/hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp
    include/hlclient/goldsrc/bsp/goldsrc_entity_document.hpp
    include/hlclient/goldsrc/bsp/goldsrc_worldspawn_wad_references.hpp
    src/goldsrc/bsp/goldsrc_bsp_parser.cpp
    src/goldsrc/bsp/goldsrc_bsp_texture_source_parser.cpp
    src/goldsrc/bsp/goldsrc_bsp_world_importer.cpp
    src/goldsrc/bsp/goldsrc_entity_document.cpp
    src/goldsrc/bsp/goldsrc_worldspawn_wad_references.cpp
)
add_library(hlclient::goldsrc_bsp ALIAS hlclient_goldsrc_bsp)
target_link_libraries(
    hlclient_goldsrc_bsp
    PUBLIC
        hlclient::asset_api
        hlclient::goldsrc_indexed_texture
        hlclient::goldsrc_spatial)
set_property(
    TARGET hlclient_goldsrc_bsp
    PROPERTY FOLDER "Engine/Assets/Formats/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_lightmaps
    include/hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp
    src/goldsrc/lightmaps/goldsrc_world_lightmap_import.cpp
)
add_library(hlclient::goldsrc_lightmaps ALIAS hlclient_goldsrc_lightmaps)
target_link_libraries(
    hlclient_goldsrc_lightmaps
    PUBLIC hlclient::asset_api)
set_property(
    TARGET hlclient_goldsrc_lightmaps
    PROPERTY FOLDER "Engine/Assets/Formats/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_wad3
    include/hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp
    include/hlclient/goldsrc/wad3/goldsrc_wad3_format.hpp
    include/hlclient/goldsrc/wad3/goldsrc_wad3_texture.hpp
    src/goldsrc/wad3/goldsrc_wad3_catalog.cpp
    src/goldsrc/wad3/goldsrc_wad3_texture.cpp
)
add_library(hlclient::goldsrc_wad3 ALIAS hlclient_goldsrc_wad3)
target_link_libraries(
    hlclient_goldsrc_wad3
    PUBLIC hlclient::asset_api hlclient::goldsrc_indexed_texture)
set_property(
    TARGET hlclient_goldsrc_wad3
    PROPERTY FOLDER "Engine/Assets/Formats/GoldSrc")

hlclient_add_library(
    hlclient_asset_dispatch
    include/hlclient/assets/asset_importer_dispatcher.hpp
    src/assets/asset_importer_dispatcher.cpp
)
add_library(hlclient::asset_dispatch ALIAS hlclient_asset_dispatch)
target_link_libraries(hlclient_asset_dispatch PUBLIC hlclient::asset_api)
set_property(
    TARGET hlclient_asset_dispatch PROPERTY FOLDER "Engine/Assets/Dispatch")

hlclient_add_library(
    hlclient_local_asset_source
    include/hlclient/local_assets/local_asset_source.hpp
    src/local_assets/local_asset_source.cpp
)
add_library(hlclient::local_asset_source ALIAS hlclient_local_asset_source)
target_link_libraries(
    hlclient_local_asset_source
    PUBLIC hlclient::asset_api hlclient::local_resources
)
set_property(
    TARGET hlclient_local_asset_source PROPERTY FOLDER "Engine/Assets/Source")

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
    include/hlclient/goldsrc/netchan_driver.hpp
    include/hlclient/goldsrc/netchan_packet.hpp
    include/hlclient/goldsrc/netchan_payload_transform.hpp
    include/hlclient/goldsrc/netchan_reassembly.hpp
    include/hlclient/goldsrc/netchan_sequence.hpp
    include/hlclient/goldsrc/netchan_session.hpp
    src/goldsrc/netchan_bootstrap_stage.cpp
    src/goldsrc/netchan_driver.cpp
    src/goldsrc/netchan_packet.cpp
    src/goldsrc/netchan_payload_transform.cpp
    src/goldsrc/netchan_reassembly.cpp
    src/goldsrc/netchan_sequence.cpp
    src/goldsrc/netchan_session.cpp
)
add_library(hlclient::goldsrc_netchan ALIAS hlclient_goldsrc_netchan)
target_link_libraries(
    hlclient_goldsrc_netchan
    PUBLIC hlclient::goldsrc hlclient::network
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_goldsrc_netchan PROPERTY FOLDER "Engine/Network/GoldSrc")

hlclient_add_library(
    hlclient_resource_consistency_api
    include/hlclient/resource_consistency/provider.hpp
    src/resource_consistency/provider.cpp
)
add_library(
    hlclient::resource_consistency_api ALIAS hlclient_resource_consistency_api)
set_property(
    TARGET hlclient_resource_consistency_api
    PROPERTY FOLDER "Engine/Resource Consistency")

hlclient_add_library(
    hlclient_resource_consistency_local
    include/hlclient/local_resources/local_resource_file_inspection.hpp
    include/hlclient/resource_consistency/prepared_local_resource_consistency_provider.hpp
    src/local_resources/local_resource_file_inspection.cpp
    src/resource_consistency/prepared_local_resource_consistency_provider.cpp
)
add_library(
    hlclient::resource_consistency_local ALIAS
    hlclient_resource_consistency_local)
target_link_libraries(
    hlclient_resource_consistency_local
    PUBLIC
        hlclient::resource_consistency_api
        hlclient::local_resources
        hlclient::hash_md5
)
set_property(
    TARGET hlclient_resource_consistency_local
    PROPERTY FOLDER "Engine/Resource Consistency")

hlclient_add_library(
    hlclient_goldsrc_signon
    include/hlclient/goldsrc/bit_reader.hpp
    include/hlclient/goldsrc/client_message.hpp
    include/hlclient/goldsrc/delta_description.hpp
    include/hlclient/goldsrc/delta_description_stage.hpp
    include/hlclient/goldsrc/initial_signon_stage.hpp
    include/hlclient/goldsrc/move_vars.hpp
    include/hlclient/goldsrc/movement_environment_stage.hpp
    include/hlclient/goldsrc/pre_resource_signon_stage.hpp
    include/hlclient/goldsrc/resource_list.hpp
    include/hlclient/goldsrc/resource_list_stage.hpp
    include/hlclient/goldsrc/resource_client_response.hpp
    include/hlclient/goldsrc/resource_client_response_stage.hpp
    include/hlclient/goldsrc/resource_transition_control.hpp
    include/hlclient/goldsrc/resource_transition_request.hpp
    include/hlclient/goldsrc/resource_transition_stage.hpp
    include/hlclient/goldsrc/server_info.hpp
    include/hlclient/goldsrc/service_message_stream.hpp
    include/hlclient/goldsrc/service_payload_envelope.hpp
    include/hlclient/goldsrc/user_info_signon_stage.hpp
    include/hlclient/goldsrc/user_info_update.hpp
    src/goldsrc/bit_reader.cpp
    src/goldsrc/client_message.cpp
    src/goldsrc/delta_description.cpp
    src/goldsrc/delta_description_stage.cpp
    src/goldsrc/initial_signon_stage.cpp
    src/goldsrc/move_vars.cpp
    src/goldsrc/movement_environment_stage.cpp
    src/goldsrc/pre_resource_signon_stage.cpp
    src/goldsrc/resource_list.cpp
    src/goldsrc/resource_list_stage.cpp
    src/goldsrc/resource_client_response.cpp
    src/goldsrc/resource_client_response_stage.cpp
    src/goldsrc/resource_transition_control.cpp
    src/goldsrc/resource_transition_request.cpp
    src/goldsrc/resource_transition_stage.cpp
    src/goldsrc/server_info.cpp
    src/goldsrc/service_message_stream.cpp
    src/goldsrc/service_payload_envelope.cpp
    src/goldsrc/user_info_signon_stage.cpp
    src/goldsrc/user_info_update.cpp
)
add_library(hlclient::goldsrc_signon ALIAS hlclient_goldsrc_signon)
target_link_libraries(
    hlclient_goldsrc_signon
    PUBLIC
        hlclient::goldsrc
        hlclient::goldsrc_netchan
        hlclient::network
        hlclient::resource_consistency_api
    PRIVATE
        hlclient::bzip2
        hlclient::core
)
set_property(TARGET hlclient_goldsrc_signon PROPERTY FOLDER "Engine/Network/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_local_resources
    include/hlclient/goldsrc/local_resource_mapping.hpp
    include/hlclient/goldsrc/local_resource_inventory.hpp
    src/goldsrc/local_resource_mapping.cpp
    src/goldsrc/local_resource_inventory.cpp
)
add_library(
    hlclient::goldsrc_local_resources ALIAS hlclient_goldsrc_local_resources)
target_link_libraries(
    hlclient_goldsrc_local_resources
    PUBLIC
        hlclient::goldsrc_signon
        hlclient::local_resources
)
set_property(
    TARGET hlclient_goldsrc_local_resources
    PROPERTY FOLDER "Engine/Resources/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_resource_readiness
    include/hlclient/goldsrc/local_resource_readiness.hpp
    include/hlclient/goldsrc/precache_manifest.hpp
    include/hlclient/goldsrc/precache_manifest_stage.hpp
    src/goldsrc/local_resource_readiness.cpp
    src/goldsrc/precache_manifest.cpp
    src/goldsrc/precache_manifest_stage.cpp
)
add_library(
    hlclient::goldsrc_resource_readiness ALIAS
    hlclient_goldsrc_resource_readiness)
target_link_libraries(
    hlclient_goldsrc_resource_readiness
    PUBLIC
        hlclient::goldsrc_local_resources
        hlclient::local_resources
)
set_property(
    TARGET hlclient_goldsrc_resource_readiness
    PROPERTY FOLDER "Engine/Resources/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_asset_dispatch
    include/hlclient/goldsrc/precache_asset_dispatch.hpp
    include/hlclient/goldsrc/precache_asset_dispatch_stage.hpp
    src/goldsrc/precache_asset_dispatch.cpp
    src/goldsrc/precache_asset_dispatch_stage.cpp
)
add_library(
    hlclient::goldsrc_asset_dispatch ALIAS hlclient_goldsrc_asset_dispatch)
target_link_libraries(
    hlclient_goldsrc_asset_dispatch
    PUBLIC
        hlclient::goldsrc_resource_readiness
        hlclient::local_asset_source
        hlclient::asset_dispatch
)
set_property(
    TARGET hlclient_goldsrc_asset_dispatch
    PROPERTY FOLDER "Engine/Resources/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_world_texture_import
    include/hlclient/goldsrc/world_textures/world_texture_import.hpp
    src/goldsrc/world_textures/world_texture_import.cpp
)
add_library(
    hlclient::goldsrc_world_texture_import ALIAS
    hlclient_goldsrc_world_texture_import)
target_link_libraries(
    hlclient_goldsrc_world_texture_import
    PUBLIC
        hlclient::goldsrc_bsp
        hlclient::goldsrc_wad3
        hlclient::local_asset_source
)
set_property(
    TARGET hlclient_goldsrc_world_texture_import
    PROPERTY FOLDER "Engine/Assets/World Textures")

hlclient_add_library(
    hlclient_goldsrc_world_textures
    include/hlclient/goldsrc/world_textures/world_texture_import_stage.hpp
    src/goldsrc/world_textures/world_texture_import_stage.cpp
)
add_library(
    hlclient::goldsrc_world_textures ALIAS
    hlclient_goldsrc_world_textures)
target_link_libraries(
    hlclient_goldsrc_world_textures
    PUBLIC
        hlclient::goldsrc_asset_dispatch
        hlclient::goldsrc_world_texture_import
)
set_property(
    TARGET hlclient_goldsrc_world_textures
    PROPERTY FOLDER "Engine/Assets/World Textures")

hlclient_add_library(
    hlclient_world_render_api
    include/hlclient/world_render/world_render_types.hpp
    src/world_render/world_render_types.cpp
)
add_library(hlclient::world_render_api ALIAS hlclient_world_render_api)
target_link_libraries(hlclient_world_render_api PUBLIC hlclient::asset_api)
set_property(
    TARGET hlclient_world_render_api PROPERTY FOLDER "Engine/Renderer/World")

hlclient_add_library(
    hlclient_world_render_package
    include/hlclient/world_render/world_render_package_builder.hpp
    src/world_render/world_render_package_builder.cpp
)
add_library(
    hlclient::world_render_package ALIAS hlclient_world_render_package)
target_link_libraries(
    hlclient_world_render_package PUBLIC hlclient::world_render_api)
set_property(
    TARGET hlclient_world_render_package PROPERTY FOLDER "Engine/Renderer/World")

add_library(hlclient_world_render INTERFACE)
add_library(hlclient::world_render ALIAS hlclient_world_render)
target_link_libraries(
    hlclient_world_render
    INTERFACE hlclient::world_render_api hlclient::world_render_package)
set_property(
    TARGET hlclient_world_render PROPERTY FOLDER "Engine/Renderer/World")

hlclient_add_library(
    hlclient_goldsrc_world_render
    include/hlclient/goldsrc/world_render/world_render_package_stage.hpp
    src/goldsrc/world_render/world_render_package_stage.cpp
)
add_library(
    hlclient::goldsrc_world_render ALIAS hlclient_goldsrc_world_render)
target_link_libraries(
    hlclient_goldsrc_world_render
    PUBLIC
        hlclient::goldsrc_world_textures
        hlclient::goldsrc_lightmaps
        hlclient::world_render_package
)
set_property(
    TARGET hlclient_goldsrc_world_render
    PROPERTY FOLDER "Engine/Assets/World Render")

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
        hlclient::goldsrc_resource_readiness
        hlclient::goldsrc_asset_dispatch
        hlclient::goldsrc_world_textures
        hlclient::goldsrc_world_render
        hlclient::goldsrc_signon
        hlclient::network
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_goldsrc_client PROPERTY FOLDER "Engine/Network/GoldSrc")

hlclient_add_library(
    hlclient_app_support
    include/hlclient/app/authentication_material_file.hpp
    include/hlclient/app/explicit_file_authentication_provider.hpp
    include/hlclient/app/precache_manifest_exit_policy.hpp
    src/app/authentication_material_file.cpp
    src/app/explicit_file_authentication_provider.cpp
)
add_library(hlclient::app_support ALIAS hlclient_app_support)
target_link_libraries(
    hlclient_app_support
    PUBLIC hlclient::auth hlclient::goldsrc_resource_readiness)
set_property(TARGET hlclient_app_support PROPERTY FOLDER "Apps")

hlclient_add_library(
    hlclient_client
    include/hlclient/client/client_world_state.hpp
    src/client/client_world_state.cpp
)
add_library(hlclient::client ALIAS hlclient_client)
target_link_libraries(
    hlclient_client PRIVATE hlclient::core hlclient::world_render_api)
set_property(TARGET hlclient_client PROPERTY FOLDER "Engine/Client")

hlclient_add_library(
    hlclient_renderer_api
    include/hlclient/renderer/render_camera_math.hpp
    include/hlclient/renderer/render_scene.hpp
    include/hlclient/renderer/renderer.hpp
    src/renderer/render_camera_math.cpp
    src/renderer/renderer.cpp
)
add_library(hlclient::renderer_api ALIAS hlclient_renderer_api)
target_link_libraries(
    hlclient_renderer_api PRIVATE hlclient::core hlclient::world_render_api)
set_property(TARGET hlclient_renderer_api PROPERTY FOLDER "Engine/Renderer")

hlclient_add_library(
    hlclient_world_visibility
    include/hlclient/world_visibility/world_view_frustum.hpp
    include/hlclient/world_visibility/world_visibility_resolver.hpp
    include/hlclient/world_visibility/world_visibility_types.hpp
    include/hlclient/world_visibility/world_visible_draw_list.hpp
    src/world_visibility/world_view_frustum.cpp
    src/world_visibility/world_visibility_resolver.cpp
    src/world_visibility/world_visibility_types.cpp
    src/world_visibility/world_visible_draw_list.cpp
)
add_library(hlclient::world_visibility ALIAS hlclient_world_visibility)
target_link_libraries(
    hlclient_world_visibility
    PUBLIC
        hlclient::renderer_api
        hlclient::world_render_api
        hlclient::world_spatial)
set_property(
    TARGET hlclient_world_visibility
    PROPERTY FOLDER "Engine/Renderer/World/Visibility")

hlclient_add_library(
    hlclient_goldsrc_brush_models
    include/hlclient/goldsrc/brush_models/brush_submodel_instances.hpp
    include/hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp
    include/hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp
    include/hlclient/goldsrc/brush_models/goldsrc_spawn_camera.hpp
    include/hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp
    include/hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp
    src/goldsrc/brush_models/brush_submodel_instances.cpp
    src/goldsrc/brush_models/goldsrc_brush_render_library.cpp
    src/goldsrc/brush_models/goldsrc_brush_entity.cpp
    src/goldsrc/brush_models/goldsrc_spawn_camera.cpp
    src/goldsrc/brush_models/goldsrc_world_scene_builder.cpp
    src/goldsrc/brush_models/goldsrc_brush_transform.cpp
)
add_library(
    hlclient::goldsrc_brush_models ALIAS hlclient_goldsrc_brush_models)
target_link_libraries(
    hlclient_goldsrc_brush_models
    PUBLIC
        hlclient::goldsrc_bsp
        hlclient::renderer_api
        hlclient::world_spatial)
set_property(
    TARGET hlclient_goldsrc_brush_models
    PROPERTY FOLDER "Engine/Renderer/World/Brush Models")

hlclient_add_library(
    hlclient_world_scene_renderer
    include/hlclient/world_scene_render/world_scene_render_types.hpp
    src/world_scene_render/world_scene_render_types.cpp
)
add_library(
    hlclient::world_scene_render ALIAS hlclient_world_scene_renderer)
target_link_libraries(
    hlclient_world_scene_renderer
    PUBLIC
        hlclient::renderer_api
        hlclient::world_render_api
        hlclient::world_spatial
        hlclient::world_visibility)
set_property(
    TARGET hlclient_world_scene_renderer
    PROPERTY FOLDER "Engine/Renderer/World")
target_link_libraries(
    hlclient_goldsrc_brush_models
    PUBLIC
        hlclient_world_scene_renderer
        hlclient_goldsrc_lightmaps
        hlclient_goldsrc_world_texture_import
        hlclient_world_render_package)
target_link_libraries(
    hlclient_goldsrc_world_render
    PUBLIC hlclient_goldsrc_brush_models)
target_link_libraries(
    hlclient_client
    PUBLIC hlclient_world_scene_renderer hlclient_world_visibility)

hlclient_add_library(
    hlclient_scene_api
    include/hlclient/client/client_scene_source.hpp
    src/client/client_scene_source.cpp
)
add_library(hlclient::scene_api ALIAS hlclient_scene_api)
target_link_libraries(
    hlclient_scene_api
    PUBLIC
        hlclient::client
        hlclient::renderer_api
        hlclient::world_visibility
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_scene_api PROPERTY FOLDER "Engine/Client")

hlclient_add_library(
    hlclient_world_preview
    include/hlclient/world_preview/world_preview_scene_source.hpp
    src/world_preview/world_preview_scene_source.cpp
)
add_library(hlclient::world_preview ALIAS hlclient_world_preview)
target_link_libraries(
    hlclient_world_preview
    PUBLIC
        hlclient::scene_api
        hlclient::world_render_api
        hlclient::world_scene_render
        hlclient::world_visibility
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_world_preview PROPERTY FOLDER "Engine/Renderer/World")

hlclient_add_library(
    hlclient_renderer_null
    include/hlclient/renderer/null/null_renderer.hpp
    src/renderer/null/null_renderer.cpp
)
add_library(hlclient::renderer_null ALIAS hlclient_renderer_null)
target_link_libraries(
    hlclient_renderer_null
    PUBLIC
        hlclient::renderer_api
        hlclient::world_scene_render
        hlclient::world_visibility
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
    PRIVATE
        hlclient::world_render_api
        hlclient::world_scene_render
        hlclient::world_visibility
        hlclient::glad
        OpenGL::GL
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
        hlclient::goldsrc_bsp
        hlclient::network
        hlclient::goldsrc
        hlclient::goldsrc_client
        hlclient::goldsrc_resource_readiness
        hlclient::goldsrc_local_resources
        hlclient::goldsrc_world_textures
        hlclient::goldsrc_world_render
        hlclient::resource_consistency_local
        hlclient::client
        hlclient::scene_api
        hlclient::renderer_api
        hlclient::renderer_null
        hlclient::renderer_opengl
        hlclient::world_render
        hlclient::world_preview
)

add_executable(
    hlclient_local_resource_check
    apps/hlclient_local_resource_check/main.cpp
)
hlclient_enable_warnings(hlclient_local_resource_check)
target_link_libraries(
    hlclient_local_resource_check
    PRIVATE hlclient::resource_consistency_local)
set_property(
    TARGET hlclient_local_resource_check PROPERTY FOLDER "Tools/Resources")

add_executable(
    hlclient_world_texture_check
    apps/hlclient_world_texture_check/main.cpp
)
hlclient_enable_warnings(hlclient_world_texture_check)
target_link_libraries(
    hlclient_world_texture_check
    PRIVATE
        hlclient::goldsrc_bsp
        hlclient::goldsrc_world_texture_import
        hlclient::local_asset_source
        hlclient::local_resources
)
set_property(
    TARGET hlclient_world_texture_check PROPERTY FOLDER "Tools/Resources")

add_executable(
    hlclient_world_viewer
    apps/hlclient_world_viewer/main.cpp
)
hlclient_enable_warnings(hlclient_world_viewer)
target_link_libraries(
    hlclient_world_viewer
    PRIVATE
        hlclient::goldsrc_bsp
        hlclient::goldsrc_brush_models
        hlclient::goldsrc_world_texture_import
        hlclient::goldsrc_lightmaps
        hlclient::world_render_package
        hlclient::world_preview
        hlclient::local_asset_source
        hlclient::local_resources
        hlclient::platform
        hlclient::renderer_opengl
)
set_property(TARGET hlclient_world_viewer PROPERTY FOLDER "Tools/World")
set_target_properties(
    hlclient_world_viewer
    PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
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

add_custom_command(
    TARGET hlclient_world_viewer
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3-shared>"
            "$<TARGET_FILE_DIR:hlclient_world_viewer>"
    COMMENT "Copying the SDL3 runtime next to hlclient_world_viewer"
    VERBATIM
)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT hlclient)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
