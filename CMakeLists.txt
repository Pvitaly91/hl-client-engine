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
include(CheckCXXCompilerFlag)
include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

option(
    HLCLIENT_ENABLE_ADDRESS_SANITIZER
    "Enable MSVC AddressSanitizer for movement/collision diagnostics"
    OFF
)

if(HLCLIENT_ENABLE_ADDRESS_SANITIZER)
    if(NOT MSVC)
        message(FATAL_ERROR
            "HLCLIENT_ENABLE_ADDRESS_SANITIZER currently requires MSVC")
    endif()

    set(_hlclient_saved_try_compile_configuration
        "${CMAKE_TRY_COMPILE_CONFIGURATION}")
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
    check_cxx_compiler_flag(
        "/fsanitize=address"
        HLCLIENT_MSVC_ADDRESS_SANITIZER_SUPPORTED)
    set(CMAKE_TRY_COMPILE_CONFIGURATION
        "${_hlclient_saved_try_compile_configuration}")
    unset(_hlclient_saved_try_compile_configuration)

    if(NOT HLCLIENT_MSVC_ADDRESS_SANITIZER_SUPPORTED)
        message(FATAL_ERROR
            "The selected MSVC toolchain does not support /fsanitize=address")
    endif()

    # MSVC AddressSanitizer is incompatible with the Debug /RTC family.  This
    # adjustment is scoped to the opt-in sanitizer build tree only.
    foreach(_hlclient_debug_flags CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
        string(REPLACE "/RTC1" ""
            ${_hlclient_debug_flags} "${${_hlclient_debug_flags}}")
        string(REPLACE "/RTCsu" ""
            ${_hlclient_debug_flags} "${${_hlclient_debug_flags}}")
    endforeach()
endif()

function(hlclient_enable_address_sanitizer target)
    if(HLCLIENT_ENABLE_ADDRESS_SANITIZER)
        target_compile_options(${target} PRIVATE /fsanitize=address)
        # The focused ASan targets intentionally link ordinary project
        # libraries from the same opt-in tree.  Disable MSVC STL's separate
        # container-annotation ABI so annotated and ordinary objects retain a
        # single link-compatible contract; ASan still instruments the focused
        # movement/collision code itself.
        target_compile_definitions(
            ${target}
            PRIVATE
                _DISABLE_STRING_ANNOTATION
                _DISABLE_VECTOR_ANNOTATION
        )
        get_target_property(_hlclient_target_type ${target} TYPE)
        if(_hlclient_target_type STREQUAL "EXECUTABLE")
            target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        endif()
    endif()
endfunction()

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
    hlclient_hash_sha256
    include/hlclient/hash/sha256.hpp
    src/hash/sha256.cpp
)
add_library(hlclient::hash_sha256 ALIAS hlclient_hash_sha256)
target_link_libraries(hlclient_hash_sha256 PUBLIC hlclient::core)
set_property(TARGET hlclient_hash_sha256 PROPERTY FOLDER "Engine/Core/Hash")

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
    hlclient_input_api
    include/hlclient/input/input_event.hpp
    include/hlclient/input/input_snapshot.hpp
    include/hlclient/input/input_source.hpp
    include/hlclient/input/input_state_tracker.hpp
    src/input/input_snapshot.cpp
    src/input/input_source.cpp
    src/input/input_state_tracker.cpp
)
add_library(hlclient::input_api ALIAS hlclient_input_api)
target_link_libraries(hlclient_input_api PUBLIC hlclient::core)
set_property(TARGET hlclient_input_api PROPERTY FOLDER "Engine/Platform/Input")

hlclient_add_library(
    hlclient_platform_sdl_input
    src/platform/sdl_platform_event_translator.hpp
    src/platform/sdl_platform_event_translator.cpp
)
add_library(hlclient::platform_sdl_input ALIAS hlclient_platform_sdl_input)
target_link_libraries(
    hlclient_platform_sdl_input
    PUBLIC hlclient::input_api
    PRIVATE SDL3::SDL3-shared)
set_property(
    TARGET hlclient_platform_sdl_input PROPERTY FOLDER "Engine/Platform/Input")

hlclient_add_library(
    hlclient_platform
    include/hlclient/platform/platform_event.hpp
    include/hlclient/platform/sdl_runtime.hpp
    include/hlclient/platform/sdl_window.hpp
    src/platform/sdl_runtime.cpp
    src/platform/sdl_window.cpp
)
add_library(hlclient::platform ALIAS hlclient_platform)
target_link_libraries(
    hlclient_platform
    PUBLIC hlclient::input_api
    PRIVATE hlclient::core hlclient::platform_sdl_input SDL3::SDL3-shared)
set_property(TARGET hlclient_platform PROPERTY FOLDER "Engine/Platform")

hlclient_add_library(
    hlclient_gameplay_input
    include/hlclient/gameplay_input/gameplay_input_bindings.hpp
    include/hlclient/gameplay_input/gameplay_input_intent.hpp
    include/hlclient/gameplay_input/gameplay_input_limits.hpp
    src/gameplay_input/gameplay_input_bindings.cpp
    src/gameplay_input/gameplay_input_intent.cpp
    src/gameplay_input/gameplay_input_limits.cpp
)
add_library(hlclient::gameplay_input ALIAS hlclient_gameplay_input)
target_link_libraries(
    hlclient_gameplay_input PUBLIC hlclient::input_api hlclient::core)
set_property(
    TARGET hlclient_gameplay_input PROPERTY FOLDER "Engine/Client/Input")

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
    include/hlclient/assets/sprite_asset_types.hpp
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
    hlclient_collision_api
    include/hlclient/collision/collision_contents.hpp
    include/hlclient/collision/collision_world_package.hpp
    include/hlclient/collision/collision_world_query.hpp
    src/collision/collision_contents.cpp
    src/collision/collision_world_package.cpp
    src/collision/collision_world_query.cpp
)
add_library(hlclient::collision_api ALIAS hlclient_collision_api)
target_link_libraries(hlclient_collision_api PUBLIC hlclient::asset_api)
set_property(TARGET hlclient_collision_api PROPERTY FOLDER "Engine/Collision")

hlclient_add_library(
    hlclient_movement_api
    include/hlclient/movement/local_player_movement_state.hpp
    src/movement/local_player_movement_state.cpp
)
add_library(hlclient::movement_api ALIAS hlclient_movement_api)
target_link_libraries(
    hlclient_movement_api PUBLIC hlclient::asset_api PRIVATE hlclient::core)
set_property(TARGET hlclient_movement_api PROPERTY FOLDER "Engine/Simulation")

add_library(hlclient_model_asset_api INTERFACE)
add_library(hlclient::model_asset_api ALIAS hlclient_model_asset_api)
target_sources(
    hlclient_model_asset_api
    INTERFACE include/hlclient/assets/model_asset_types.hpp)
target_include_directories(
    hlclient_model_asset_api INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(
    hlclient_model_asset_api INTERFACE hlclient::asset_api hlclient::core)
set_property(
    TARGET hlclient_model_asset_api PROPERTY FOLDER "Engine/Assets/Models")

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
    hlclient_goldsrc_studio_model
    include/hlclient/goldsrc/studio/goldsrc_studio_animation.hpp
    include/hlclient/goldsrc/studio/goldsrc_studio_format.hpp
    include/hlclient/goldsrc/studio/goldsrc_studio_geometry.hpp
    include/hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp
    include/hlclient/goldsrc/studio/goldsrc_studio_parser.hpp
    src/goldsrc/studio/goldsrc_studio_animation.cpp
    src/goldsrc/studio/goldsrc_studio_format.cpp
    src/goldsrc/studio/goldsrc_studio_geometry.cpp
    src/goldsrc/studio/goldsrc_studio_model_importer.cpp
    src/goldsrc/studio/goldsrc_studio_parser.cpp
)
add_library(
    hlclient::goldsrc_studio_model ALIAS hlclient_goldsrc_studio_model)
target_link_libraries(
    hlclient_goldsrc_studio_model
    PUBLIC hlclient::model_asset_api hlclient::asset_api hlclient::core)
set_property(
    TARGET hlclient_goldsrc_studio_model
    PROPERTY FOLDER "Engine/Assets/Formats/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_sprite
    include/hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp
    include/hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp
    include/hlclient/goldsrc/sprite/goldsrc_sprite_playback.hpp
    src/goldsrc/sprite/goldsrc_sprite_importer.cpp
    src/goldsrc/sprite/goldsrc_sprite_parser.cpp
    src/goldsrc/sprite/goldsrc_sprite_playback.cpp
)
add_library(hlclient::goldsrc_sprite ALIAS hlclient_goldsrc_sprite)
target_link_libraries(
    hlclient_goldsrc_sprite PUBLIC hlclient::asset_api hlclient::core)
set_property(
    TARGET hlclient_goldsrc_sprite
    PROPERTY FOLDER "Engine/Assets/Formats/GoldSrc")

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
    include/hlclient/goldsrc/bsp/goldsrc_bsp_collision_source.hpp
    include/hlclient/goldsrc/bsp/goldsrc_face_geometry_builder.hpp
    include/hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp
    include/hlclient/goldsrc/bsp/goldsrc_bsp_texture_source_parser.hpp
    include/hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp
    include/hlclient/goldsrc/bsp/goldsrc_entity_document.hpp
    include/hlclient/goldsrc/bsp/goldsrc_entity_transform.hpp
    include/hlclient/goldsrc/bsp/goldsrc_worldspawn_wad_references.hpp
    src/goldsrc/bsp/goldsrc_face_geometry_builder.cpp
    src/goldsrc/bsp/goldsrc_bsp_parser.cpp
    src/goldsrc/bsp/goldsrc_bsp_texture_source_parser.cpp
    src/goldsrc/bsp/goldsrc_bsp_world_importer.cpp
    src/goldsrc/bsp/goldsrc_entity_document.cpp
    src/goldsrc/bsp/goldsrc_entity_transform.cpp
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
    hlclient_goldsrc_bsp_collision
    include/hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp
    src/goldsrc/collision/goldsrc_collision_world_builder.cpp
)
add_library(
    hlclient::goldsrc_bsp_collision ALIAS hlclient_goldsrc_bsp_collision)
target_link_libraries(
    hlclient_goldsrc_bsp_collision
    PUBLIC hlclient::collision_api hlclient::goldsrc_bsp)
set_property(
    TARGET hlclient_goldsrc_bsp_collision
    PROPERTY FOLDER "Engine/Collision/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_builtin_importers
    include/hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp
    src/goldsrc/goldsrc_builtin_asset_importers.cpp
)
add_library(
    hlclient::goldsrc_builtin_importers ALIAS
    hlclient_goldsrc_builtin_importers)
target_link_libraries(
    hlclient_goldsrc_builtin_importers
    PUBLIC hlclient::goldsrc_bsp
    PRIVATE
        hlclient::goldsrc_studio_model
        hlclient::goldsrc_sprite)
set_property(
    TARGET hlclient_goldsrc_builtin_importers
    PROPERTY FOLDER "Engine/Resources/GoldSrc")

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

add_library(hlclient_goldsrc_approved_asset_source_api INTERFACE)
add_library(
    hlclient::goldsrc_approved_asset_source_api ALIAS
    hlclient_goldsrc_approved_asset_source_api)
target_sources(
    hlclient_goldsrc_approved_asset_source_api
    INTERFACE include/hlclient/goldsrc/approved_asset_source.hpp)
target_include_directories(
    hlclient_goldsrc_approved_asset_source_api
    INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(
    hlclient_goldsrc_approved_asset_source_api
    INTERFACE
        hlclient::asset_api
        hlclient::asset_dispatch
        hlclient::local_asset_source
        hlclient::local_resources)
set_property(
    TARGET hlclient_goldsrc_approved_asset_source_api
    PROPERTY FOLDER "Engine/Resources/GoldSrc")

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
    hlclient_goldsrc_byte_io
    include/hlclient/goldsrc/byte_reader.hpp
    include/hlclient/goldsrc/byte_writer.hpp
    src/goldsrc/byte_reader.cpp
    src/goldsrc/byte_writer.cpp
)
add_library(hlclient::goldsrc_byte_io ALIAS hlclient_goldsrc_byte_io)
target_link_libraries(hlclient_goldsrc_byte_io PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_byte_io
    PROPERTY FOLDER "Engine/Protocol/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc
    include/hlclient/goldsrc/challenge_protocol.hpp
    include/hlclient/goldsrc/connect_request.hpp
    include/hlclient/goldsrc/connect_response.hpp
    include/hlclient/goldsrc/connectionless_packet.hpp
    include/hlclient/goldsrc/info_string.hpp
    src/goldsrc/challenge_protocol.cpp
    src/goldsrc/connect_request.cpp
    src/goldsrc/connect_response.cpp
    src/goldsrc/connectionless_packet.cpp
    src/goldsrc/info_string.cpp
)
add_library(hlclient::goldsrc ALIAS hlclient_goldsrc)
target_link_libraries(
    hlclient_goldsrc
    PUBLIC
        hlclient::goldsrc_byte_io
        hlclient::network
    PRIVATE hlclient::core)
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
    hlclient_goldsrc_delta_schema
    include/hlclient/goldsrc/bit_reader.hpp
    include/hlclient/goldsrc/delta_description.hpp
    src/goldsrc/bit_reader.cpp
    src/goldsrc/delta_description.cpp
    src/goldsrc/post_delta_boundary.cpp
)
add_library(
    hlclient::goldsrc_delta_schema ALIAS hlclient_goldsrc_delta_schema)
target_link_libraries(
    hlclient_goldsrc_delta_schema PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_delta_schema
    PROPERTY FOLDER "Engine/Network/GoldSrc/Delta")

hlclient_add_library(
    hlclient_goldsrc_move_vars_api
    include/hlclient/goldsrc/move_vars.hpp
    src/goldsrc/move_vars.cpp
)
add_library(
    hlclient::goldsrc_move_vars_api ALIAS hlclient_goldsrc_move_vars_api)
target_link_libraries(
    hlclient_goldsrc_move_vars_api
    PUBLIC
        hlclient::goldsrc_byte_io
        hlclient::goldsrc_delta_schema
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_move_vars_api
    PROPERTY FOLDER "Engine/Protocol/GoldSrc/Movement")

hlclient_add_library(
    hlclient_goldsrc_signon
    include/hlclient/goldsrc/client_message.hpp
    include/hlclient/goldsrc/delta_description_stage.hpp
    include/hlclient/goldsrc/initial_signon_stage.hpp
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
    src/goldsrc/client_message.cpp
    src/goldsrc/delta_description_stage.cpp
    src/goldsrc/delta_description_stream.cpp
    src/goldsrc/initial_signon_stage.cpp
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
        hlclient::goldsrc_delta_schema
        hlclient::goldsrc_move_vars_api
        hlclient::goldsrc_netchan
        hlclient::network
        hlclient::resource_consistency_api
    PRIVATE
        hlclient::bzip2
        hlclient::core
)
set_property(TARGET hlclient_goldsrc_signon PROPERTY FOLDER "Engine/Network/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_delta_values
    include/hlclient/goldsrc/delta_value_decoder.hpp
    src/goldsrc/delta_value_decoder.cpp
)
add_library(
    hlclient::goldsrc_delta_values ALIAS hlclient_goldsrc_delta_values)
target_link_libraries(
    hlclient_goldsrc_delta_values
    PUBLIC hlclient::goldsrc_delta_schema
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_delta_values
    PROPERTY FOLDER "Engine/Network/GoldSrc/Delta")

hlclient_add_library(
    hlclient_goldsrc_usercmd_api
    include/hlclient/goldsrc/usercmd_duration.hpp
    include/hlclient/goldsrc/usercmd_state.hpp
    src/goldsrc/usercmd_duration.cpp
    src/goldsrc/usercmd_state.cpp
)
add_library(
    hlclient::goldsrc_usercmd_api ALIAS hlclient_goldsrc_usercmd_api)
target_link_libraries(
    hlclient_goldsrc_usercmd_api PUBLIC hlclient::core)
set_property(
    TARGET hlclient_goldsrc_usercmd_api
    PROPERTY FOLDER "Engine/Network/GoldSrc/UserCmd")

hlclient_add_library(
    hlclient_prediction_api
    include/hlclient/prediction/authoritative_player_state.hpp
    include/hlclient/prediction/prediction_history.hpp
    include/hlclient/prediction/prediction_state_comparison.hpp
    include/hlclient/prediction/prediction_types.hpp
    src/prediction/authoritative_player_state.cpp
    src/prediction/prediction_history.cpp
    src/prediction/prediction_state_comparison.cpp
    src/prediction/prediction_types.cpp
)
add_library(hlclient::prediction_api ALIAS hlclient_prediction_api)
target_link_libraries(
    hlclient_prediction_api
    PUBLIC
        hlclient::goldsrc_usercmd_api
        hlclient::movement_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_prediction_api
    PROPERTY FOLDER "Engine/Simulation/Prediction")

hlclient_add_library(
    hlclient_goldsrc_usercmd_codec
    include/hlclient/goldsrc/bit_writer.hpp
    include/hlclient/goldsrc/client_move_message.hpp
    include/hlclient/goldsrc/move_checksum.hpp
    include/hlclient/goldsrc/usercmd_delta_codec.hpp
    include/hlclient/goldsrc/usercmd_schema_binding.hpp
    src/goldsrc/bit_writer.cpp
    src/goldsrc/client_move_message.cpp
    src/goldsrc/move_checksum.cpp
    src/goldsrc/usercmd_delta_codec.cpp
    src/goldsrc/usercmd_schema_binding.cpp
)
add_library(
    hlclient::goldsrc_usercmd_codec ALIAS hlclient_goldsrc_usercmd_codec)
target_link_libraries(
    hlclient_goldsrc_usercmd_codec
    PUBLIC
        hlclient::goldsrc_usercmd_api
        hlclient::goldsrc_delta_schema
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_usercmd_codec
    PROPERTY FOLDER "Engine/Network/GoldSrc/Client Move")

hlclient_add_library(
    hlclient_goldsrc_entity_snapshots
    include/hlclient/goldsrc/entity_snapshot.hpp
    src/goldsrc/entity_snapshot.cpp
)
add_library(
    hlclient::goldsrc_entity_snapshots ALIAS
    hlclient_goldsrc_entity_snapshots)
target_link_libraries(
    hlclient_goldsrc_entity_snapshots
    PUBLIC
        hlclient::goldsrc_delta_values
        hlclient::goldsrc_signon
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_entity_snapshots
    PROPERTY FOLDER "Engine/Network/GoldSrc/Entities")

hlclient_add_library(
    hlclient_goldsrc_post_resource_signon
    include/hlclient/goldsrc/post_resource_entity_snapshot_stage.hpp
    include/hlclient/goldsrc/post_resource_signon.hpp
    src/goldsrc/post_resource_entity_snapshot_stage.cpp
    src/goldsrc/post_resource_signon.cpp
)
add_library(
    hlclient::goldsrc_post_resource_signon ALIAS
    hlclient_goldsrc_post_resource_signon)
target_link_libraries(
    hlclient_goldsrc_post_resource_signon
    PUBLIC
        hlclient::goldsrc_entity_snapshots
        hlclient::goldsrc_signon
        hlclient::goldsrc_netchan
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_post_resource_signon
    PROPERTY FOLDER "Engine/Network/GoldSrc/Signon")

hlclient_add_library(
    hlclient_goldsrc_stock_runtime
    include/hlclient/goldsrc/stock_authoritative_movement.hpp
    include/hlclient/goldsrc/stock_command_ack_evidence.hpp
    include/hlclient/goldsrc/stock_local_player_identity.hpp
    include/hlclient/goldsrc/stock_runtime_frame.hpp
    include/hlclient/goldsrc/stock_runtime_message_catalog.hpp
    include/hlclient/goldsrc/stock_server_time.hpp
    src/goldsrc/stock_authoritative_movement.cpp
    src/goldsrc/stock_command_ack_evidence.cpp
    src/goldsrc/stock_local_player_identity.cpp
    src/goldsrc/stock_runtime_frame.cpp
    src/goldsrc/stock_runtime_message_catalog.cpp
    src/goldsrc/stock_server_time.cpp
)
add_library(
    hlclient::goldsrc_stock_runtime ALIAS
    hlclient_goldsrc_stock_runtime)
target_link_libraries(
    hlclient_goldsrc_stock_runtime
    PUBLIC
        hlclient::goldsrc_entity_snapshots
        hlclient::movement_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_stock_runtime
    PROPERTY FOLDER "Engine/Network/GoldSrc/Runtime Evidence")

hlclient_add_library(
    hlclient_goldsrc_stock_runtime_capture
    include/hlclient/goldsrc/stock_runtime_capture.hpp
    src/goldsrc/stock_runtime_capture.cpp
)
add_library(
    hlclient::goldsrc_stock_runtime_capture ALIAS
    hlclient_goldsrc_stock_runtime_capture)
target_link_libraries(
    hlclient_goldsrc_stock_runtime_capture PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_stock_runtime_capture
    PROPERTY FOLDER "Tools/Research/GoldSrc")

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
        hlclient::goldsrc_approved_asset_source_api
        hlclient::goldsrc_resource_readiness
        hlclient::local_asset_source
        hlclient::asset_dispatch
)
set_property(
    TARGET hlclient_goldsrc_asset_dispatch
    PROPERTY FOLDER "Engine/Resources/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_visual_asset_bundle
    include/hlclient/goldsrc/visual_assets/goldsrc_studio_companion_names.hpp
    include/hlclient/goldsrc/visual_assets/goldsrc_studio_model_source_bundle.hpp
    include/hlclient/goldsrc/visual_assets/goldsrc_studio_model_bundle_import.hpp
    include/hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp
    src/goldsrc/visual_assets/goldsrc_studio_companion_names.cpp
    src/goldsrc/visual_assets/goldsrc_studio_model_source_bundle.cpp
    src/goldsrc/visual_assets/goldsrc_studio_model_bundle_import.cpp
    src/goldsrc/visual_assets/goldsrc_visual_asset_import.cpp
)
add_library(
    hlclient::goldsrc_visual_asset_bundle ALIAS
    hlclient_goldsrc_visual_asset_bundle)
target_link_libraries(
    hlclient_goldsrc_visual_asset_bundle
    PUBLIC
        hlclient::goldsrc_studio_model
        hlclient::goldsrc_sprite
        hlclient::goldsrc_approved_asset_source_api
        hlclient::local_asset_source
        hlclient::local_resources
        hlclient::asset_dispatch)
set_property(
    TARGET hlclient_goldsrc_visual_asset_bundle
    PROPERTY FOLDER "Engine/Resources/GoldSrc")

hlclient_add_library(
    hlclient_entity_visual_projection
    include/hlclient/entity_visual/entity_visual_projection.hpp
    src/entity_visual/entity_visual_projection.cpp
)
add_library(
    hlclient::entity_visual_projection ALIAS
    hlclient_entity_visual_projection)
target_link_libraries(
    hlclient_entity_visual_projection
    PUBLIC hlclient::goldsrc_entity_snapshots hlclient::asset_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_entity_visual_projection
    PROPERTY FOLDER "Engine/Client/Entities")

hlclient_add_library(
    hlclient_entity_visual_assets
    include/hlclient/entity_visual/entity_pipeline_stage_types.hpp
    include/hlclient/entity_visual/entity_visual_binding.hpp
    include/hlclient/entity_visual/entity_visual_asset_library.hpp
    include/hlclient/entity_visual/entity_visual_asset_stage.hpp
    src/entity_visual/entity_visual_binding.cpp
    src/entity_visual/entity_visual_asset_library.cpp
    src/entity_visual/entity_visual_asset_stage.cpp
)
add_library(
    hlclient::entity_visual_assets ALIAS hlclient_entity_visual_assets)
target_link_libraries(
    hlclient_entity_visual_assets
    PUBLIC
        hlclient::entity_visual_projection
        hlclient::goldsrc_resource_readiness
        hlclient::goldsrc_visual_asset_bundle
        hlclient::model_asset_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_entity_visual_assets
    PROPERTY FOLDER "Engine/Client/Entities")

hlclient_add_library(
    hlclient_entity_interpolation
    include/hlclient/goldsrc/entity_snapshot_interpolation.hpp
    src/goldsrc/entity_snapshot_interpolation.cpp
)
add_library(
    hlclient::entity_interpolation ALIAS hlclient_entity_interpolation)
target_link_libraries(
    hlclient_entity_interpolation
    PUBLIC hlclient::entity_visual_projection
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_entity_interpolation
    PROPERTY FOLDER "Engine/Client/Entities")

hlclient_add_library(
    hlclient_entity_interpolation_stage
    include/hlclient/entity_visual/entity_interpolation_stage.hpp
    src/entity_visual/entity_interpolation_stage.cpp
)
add_library(
    hlclient::entity_interpolation_stage ALIAS
    hlclient_entity_interpolation_stage)
target_link_libraries(
    hlclient_entity_interpolation_stage
    PUBLIC
        hlclient::entity_interpolation
        hlclient::entity_visual_assets
    PRIVATE
        hlclient::core
        hlclient::entity_scene_render)
set_property(
    TARGET hlclient_entity_interpolation_stage
    PROPERTY FOLDER "Engine/Client/Entities")

hlclient_add_library(
    hlclient_studio_pose
    include/hlclient/goldsrc/studio/goldsrc_studio_pose.hpp
    src/goldsrc/studio/goldsrc_studio_pose.cpp
)
add_library(hlclient::studio_pose ALIAS hlclient_studio_pose)
target_link_libraries(
    hlclient_studio_pose
    PUBLIC hlclient::model_asset_api hlclient::asset_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_studio_pose PROPERTY FOLDER "Engine/Assets/Models")

hlclient_add_library(
    hlclient_entity_render_api
    include/hlclient/entity_render/entity_render_types.hpp
    include/hlclient/entity_render/studio_model_render_asset.hpp
    include/hlclient/entity_render/sprite_render_asset.hpp
    include/hlclient/entity_render/sprite_frame_selector.hpp
    include/hlclient/entity_render/sprite_billboard_basis.hpp
    src/entity_render/entity_render_types.cpp
    src/entity_render/studio_model_render_asset.cpp
    src/entity_render/sprite_render_asset.cpp
    src/entity_render/sprite_frame_selector.cpp
    src/entity_render/sprite_billboard_basis.cpp
)
add_library(hlclient::entity_render_api ALIAS hlclient_entity_render_api)
target_link_libraries(
    hlclient_entity_render_api
    PUBLIC hlclient::model_asset_api hlclient::asset_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_entity_render_api
    PROPERTY FOLDER "Engine/Renderer/Entities")

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
        hlclient::goldsrc_post_resource_signon
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
target_link_libraries(hlclient_client PRIVATE hlclient::renderer_api)

hlclient_add_library(
    hlclient_gameplay_camera
    include/hlclient/gameplay_camera/first_person_camera.hpp
    include/hlclient/gameplay_camera/entity_first_person_camera.hpp
    src/gameplay_camera/first_person_camera.cpp
    src/gameplay_camera/entity_first_person_camera.cpp
)
add_library(hlclient::gameplay_camera ALIAS hlclient_gameplay_camera)
target_link_libraries(
    hlclient_gameplay_camera
    PUBLIC
        hlclient::asset_api
        hlclient::gameplay_input
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_gameplay_camera PROPERTY FOLDER "Engine/Client/Camera")

hlclient_add_library(
    hlclient_gameplay_camera_render_adapter
    include/hlclient/gameplay_camera/render_camera_adapter.hpp
    src/gameplay_camera/render_camera_adapter.cpp
)
add_library(
    hlclient::gameplay_camera_render_adapter ALIAS
    hlclient_gameplay_camera_render_adapter)
target_link_libraries(
    hlclient_gameplay_camera_render_adapter
    PUBLIC
        hlclient::gameplay_camera
        hlclient::renderer_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_gameplay_camera_render_adapter
    PROPERTY FOLDER "Engine/Client/Camera")

hlclient_add_library(
    hlclient_goldsrc_usercmd_sampling
    include/hlclient/goldsrc/usercmd_input_adapter.hpp
    include/hlclient/goldsrc/usercmd_scheduler.hpp
    src/goldsrc/usercmd_input_adapter.cpp
    src/goldsrc/usercmd_scheduler.cpp
)
add_library(
    hlclient::goldsrc_usercmd_sampling ALIAS hlclient_goldsrc_usercmd_sampling)
target_link_libraries(
    hlclient_goldsrc_usercmd_sampling
    PUBLIC
        hlclient::goldsrc_usercmd_api
        hlclient::gameplay_input
        hlclient::gameplay_camera
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_usercmd_sampling
    PROPERTY FOLDER "Engine/Client/GoldSrc/UserCmd")

hlclient_add_library(
    hlclient_goldsrc_usercmd_session
    include/hlclient/goldsrc/usercmd_history.hpp
    include/hlclient/goldsrc/usercmd_packet_planner.hpp
    include/hlclient/goldsrc/usercmd_transmission_stage.hpp
    src/goldsrc/usercmd_history.cpp
    src/goldsrc/usercmd_packet_planner.cpp
    src/goldsrc/usercmd_transmission_stage.cpp
)
add_library(
    hlclient::goldsrc_usercmd_session ALIAS hlclient_goldsrc_usercmd_session)
target_link_libraries(
    hlclient_goldsrc_usercmd_session
    PUBLIC
        hlclient::goldsrc_usercmd_codec
        hlclient::goldsrc_netchan
        hlclient::goldsrc_usercmd_sampling
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_usercmd_session
    PROPERTY FOLDER "Engine/Network/GoldSrc/UserCmd")

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
    hlclient_goldsrc_brush_transform
    include/hlclient/goldsrc/brush_models/goldsrc_brush_rigid_transform.hpp
    src/goldsrc/brush_models/goldsrc_brush_rigid_transform.cpp
)
add_library(
    hlclient::goldsrc_brush_transform ALIAS hlclient_goldsrc_brush_transform)
target_link_libraries(
    hlclient_goldsrc_brush_transform PUBLIC hlclient::asset_api)
set_property(
    TARGET hlclient_goldsrc_brush_transform
    PROPERTY FOLDER "Engine/Collision/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_collision_scene
    include/hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp
    src/goldsrc/collision/goldsrc_brush_collision_scene.cpp
)
add_library(
    hlclient::goldsrc_collision_scene ALIAS
    hlclient_goldsrc_collision_scene)
target_link_libraries(
    hlclient_goldsrc_collision_scene
    PUBLIC
        hlclient::collision_api
        hlclient::goldsrc_bsp_collision
        hlclient::goldsrc_brush_transform)
set_property(
    TARGET hlclient_goldsrc_collision_scene
    PROPERTY FOLDER "Engine/Collision/GoldSrc")

hlclient_add_library(
    hlclient_goldsrc_local_movement
    include/hlclient/goldsrc/movement/goldsrc_local_movement.hpp
    include/hlclient/goldsrc/movement/goldsrc_movement_environment.hpp
    include/hlclient/goldsrc/movement/goldsrc_movement_math.hpp
    include/hlclient/goldsrc/movement/local_movement_collision.hpp
    include/hlclient/goldsrc/movement/player_wall_contact_diagnostics.hpp
    src/goldsrc/movement/goldsrc_local_movement.cpp
    src/goldsrc/movement/goldsrc_movement_environment.cpp
    src/goldsrc/movement/goldsrc_movement_math.cpp
    src/goldsrc/movement/local_movement_collision.cpp
)
add_library(
    hlclient::goldsrc_local_movement ALIAS
    hlclient_goldsrc_local_movement)
target_link_libraries(
    hlclient_goldsrc_local_movement
    PUBLIC
        hlclient::collision_api
        hlclient::goldsrc_collision_scene
        hlclient::goldsrc_move_vars_api
        hlclient::goldsrc_usercmd_api
        hlclient::movement_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_goldsrc_local_movement
    PROPERTY FOLDER "Engine/Simulation/GoldSrc")

hlclient_add_library(
    hlclient_local_prediction
    include/hlclient/prediction/local_prediction.hpp
    include/hlclient/prediction/prediction_reconciliation.hpp
    include/hlclient/prediction/synthetic_authoritative_player.hpp
    src/prediction/local_prediction.cpp
    src/prediction/prediction_reconciliation.cpp
    src/prediction/synthetic_authoritative_player.cpp
)
add_library(hlclient::local_prediction ALIAS hlclient_local_prediction)
target_link_libraries(
    hlclient_local_prediction
    PUBLIC
        hlclient::collision_api
        hlclient::goldsrc_local_movement
        hlclient::prediction_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_local_prediction
    PROPERTY FOLDER "Engine/Simulation/Prediction")

hlclient_add_library(
    hlclient_stock_authoritative_adapter
    include/hlclient/prediction/stock_authoritative_player_state_adapter.hpp
    src/prediction/stock_authoritative_player_state_adapter.cpp
)
add_library(
    hlclient::stock_authoritative_adapter ALIAS
    hlclient_stock_authoritative_adapter)
target_link_libraries(
    hlclient_stock_authoritative_adapter
    PUBLIC
        hlclient::goldsrc_stock_runtime
        hlclient::local_prediction
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_stock_authoritative_adapter
    PROPERTY FOLDER "Engine/Simulation/Prediction")

hlclient_add_library(
    hlclient_prediction_visual
    include/hlclient/prediction/prediction_visual_correction.hpp
    src/prediction/prediction_visual_correction.cpp
)
add_library(hlclient::prediction_visual ALIAS hlclient_prediction_visual)
target_link_libraries(
    hlclient_prediction_visual
    PUBLIC
        hlclient::collision_api
        hlclient::gameplay_camera
        hlclient::prediction_api
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_prediction_visual
    PROPERTY FOLDER "Engine/Renderer/Prediction")

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
        hlclient::goldsrc_brush_transform
        hlclient::renderer_api
        hlclient::world_spatial)
set_property(
    TARGET hlclient_goldsrc_brush_models
    PROPERTY FOLDER "Engine/Renderer/World/Brush Models")

hlclient_add_library(
    hlclient_local_player_controller
    include/hlclient/local_player/local_player_movement_controller.hpp
    include/hlclient/local_player/local_player_prediction_controller.hpp
    include/hlclient/local_player/local_player_spawn_selector.hpp
    include/hlclient/local_player/player_walk_failure_latch.hpp
    src/local_player/local_player_movement_controller.cpp
    src/local_player/local_player_prediction_controller.cpp
    src/local_player/local_player_spawn_selector.cpp
    src/local_player/player_walk_failure_latch.cpp
)
add_library(
    hlclient::local_player_controller ALIAS hlclient_local_player_controller)
target_link_libraries(
    hlclient_local_player_controller
    PUBLIC
        hlclient::gameplay_camera
        hlclient::gameplay_input
        hlclient::goldsrc_local_movement
        hlclient::goldsrc_usercmd_sampling
        hlclient::local_prediction
        hlclient::prediction_visual
    PRIVATE
        hlclient::core
        hlclient::goldsrc_bsp)
set_property(
    TARGET hlclient_local_player_controller
    PROPERTY FOLDER "Engine/Client/Movement")

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

hlclient_add_library(
    hlclient_entity_scene_render
    include/hlclient/entity_render/entity_scene_render.hpp
    src/entity_render/entity_scene_render.cpp
)
add_library(
    hlclient::entity_scene_render ALIAS hlclient_entity_scene_render)
target_link_libraries(
    hlclient_entity_scene_render
    PUBLIC
        hlclient::entity_render_api
        hlclient::world_spatial
        hlclient::world_visibility
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_entity_scene_render
    PROPERTY FOLDER "Engine/Renderer/Entities")

hlclient_add_library(
    hlclient_entity_scene_package_build
    include/hlclient/entity_render/entity_scene_package_stage.hpp
    src/entity_render/entity_scene_package_build.cpp
    src/entity_render/entity_scene_package_stage.cpp
)
add_library(
    hlclient::entity_scene_package_build ALIAS
    hlclient_entity_scene_package_build)
target_link_libraries(
    hlclient_entity_scene_package_build
    PUBLIC
        hlclient::entity_scene_render
        hlclient::entity_visual_assets
    PRIVATE
        hlclient::core
        hlclient::world_scene_render)
set_property(
    TARGET hlclient_entity_scene_package_build
    PROPERTY FOLDER "Engine/Client/Entities")

hlclient_add_library(
    hlclient_entity_frame_composition
    include/hlclient/entity_render/entity_render_frame_composer.hpp
    src/entity_render/entity_render_frame_composer.cpp
)
add_library(
    hlclient::entity_frame_composition ALIAS
    hlclient_entity_frame_composition)
target_link_libraries(
    hlclient_entity_frame_composition
    PUBLIC
        hlclient::entity_scene_render
        hlclient::entity_interpolation
        hlclient::studio_pose
        hlclient::goldsrc_sprite
    PRIVATE hlclient::core)
set_property(
    TARGET hlclient_entity_frame_composition
    PROPERTY FOLDER "Engine/Client/Entities")

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
    PUBLIC
        hlclient_world_scene_renderer
        hlclient_world_visibility
        hlclient_entity_scene_render)

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
        hlclient::entity_scene_render
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_scene_api PROPERTY FOLDER "Engine/Client")

hlclient_add_library(
    hlclient_interactive_preview
    include/hlclient/interactive_preview/interactive_entity_visibility.hpp
    include/hlclient/interactive_preview/interactive_preview_controller.hpp
    src/interactive_preview/interactive_entity_visibility.cpp
    src/interactive_preview/interactive_preview_controller.cpp
)
add_library(hlclient::interactive_preview ALIAS hlclient_interactive_preview)
target_link_libraries(
    hlclient_interactive_preview
    PUBLIC
        hlclient::client
        hlclient::entity_scene_render
        hlclient::gameplay_camera
        hlclient::gameplay_input
        hlclient::input_api
    PRIVATE
        hlclient::gameplay_camera_render_adapter
        hlclient::renderer_api)
set_property(
    TARGET hlclient_interactive_preview PROPERTY FOLDER "Engine/Client/Camera")

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
        hlclient::entity_scene_render
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
        hlclient::entity_scene_render
    PRIVATE hlclient::core
)
set_property(TARGET hlclient_renderer_null PROPERTY FOLDER "Engine/Renderer")

hlclient_add_library(
    hlclient_renderer_opengl
    include/hlclient/renderer/opengl/opengl_renderer.hpp
    src/renderer/opengl/opengl_renderer.cpp
    src/renderer/opengl/opengl_entity_renderer.hpp
    src/renderer/opengl/opengl_entity_renderer.cpp
)
add_library(hlclient::renderer_opengl ALIAS hlclient_renderer_opengl)
target_link_libraries(
    hlclient_renderer_opengl
    PUBLIC hlclient::renderer_api
    PRIVATE
        hlclient::world_render_api
        hlclient::world_scene_render
        hlclient::world_visibility
        hlclient::entity_scene_render
        hlclient::entity_render_api
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
        hlclient::goldsrc_bsp_collision
        hlclient::goldsrc_builtin_importers
        hlclient::network
        hlclient::goldsrc
        hlclient::goldsrc_client
        hlclient::goldsrc_resource_readiness
        hlclient::goldsrc_local_resources
        hlclient::goldsrc_usercmd_session
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
        hlclient::interactive_preview
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
    hlclient_bsp_compat_check
    apps/hlclient_bsp_compat_check/main.cpp
)
hlclient_enable_warnings(hlclient_bsp_compat_check)
target_link_libraries(
    hlclient_bsp_compat_check
    PRIVATE
        hlclient::goldsrc_bsp
        hlclient::goldsrc_brush_models
        hlclient::goldsrc_world_texture_import
        hlclient::goldsrc_lightmaps
        hlclient::world_render_package
        hlclient::local_asset_source
        hlclient::local_resources
)
set_property(
    TARGET hlclient_bsp_compat_check PROPERTY FOLDER "Tools/Assets")

add_executable(
    hlclient_collision_check
    apps/hlclient_collision_check/main.cpp
)
hlclient_enable_warnings(hlclient_collision_check)
target_link_libraries(
    hlclient_collision_check
    PRIVATE
        hlclient::collision_api
        hlclient::goldsrc_bsp
        hlclient::goldsrc_bsp_collision
        hlclient::hash_sha256
        hlclient::local_asset_source
        hlclient::local_resources
)
set_property(
    TARGET hlclient_collision_check PROPERTY FOLDER "Tools/Collision")

add_executable(
    hlclient_movement_check
    apps/hlclient_movement_check/main.cpp
)
hlclient_enable_warnings(hlclient_movement_check)
target_link_libraries(
    hlclient_movement_check
    PRIVATE
        hlclient::goldsrc_bsp
        hlclient::goldsrc_bsp_collision
        hlclient::hash_sha256
        hlclient::local_asset_source
        hlclient::local_player_controller
        hlclient::local_resources
)
set_property(
    TARGET hlclient_movement_check PROPERTY FOLDER "Tools/Movement")

add_executable(
    hlclient_prediction_check
    apps/hlclient_prediction_check/main.cpp
)
hlclient_enable_warnings(hlclient_prediction_check)
target_link_libraries(
    hlclient_prediction_check
    PRIVATE
        hlclient::goldsrc_bsp
        hlclient::goldsrc_bsp_collision
        hlclient::hash_sha256
        hlclient::local_asset_source
        hlclient::local_player_controller
        hlclient::local_prediction
        hlclient::local_resources
)
set_property(
    TARGET hlclient_prediction_check PROPERTY FOLDER "Tools/Prediction")

add_executable(
    hlclient_goldsrc_asset_check
    apps/hlclient_goldsrc_asset_check/main.cpp
)
hlclient_enable_warnings(hlclient_goldsrc_asset_check)
target_link_libraries(
    hlclient_goldsrc_asset_check
    PRIVATE
        hlclient::goldsrc_builtin_importers
        hlclient::goldsrc_visual_asset_bundle
        hlclient::local_asset_source
        hlclient::local_resources
)
set_property(
    TARGET hlclient_goldsrc_asset_check PROPERTY FOLDER "Tools/Assets")

add_executable(
    hlclient_usercmd_check
    apps/hlclient_usercmd_check/main.cpp
)
hlclient_enable_warnings(hlclient_usercmd_check)
target_link_libraries(
    hlclient_usercmd_check PRIVATE hlclient::goldsrc_usercmd_codec)
set_property(
    TARGET hlclient_usercmd_check PROPERTY FOLDER "Tools/Network")

add_executable(
    hlclient_stock_runtime_capture
    apps/hlclient_stock_runtime_capture/main.cpp
)
hlclient_enable_warnings(hlclient_stock_runtime_capture)
target_link_libraries(
    hlclient_stock_runtime_capture
    PRIVATE
        hlclient::goldsrc_stock_runtime_capture
        hlclient::network)
set_property(
    TARGET hlclient_stock_runtime_capture
    PROPERTY FOLDER "Tools/Research/GoldSrc")

add_executable(
    hlclient_stock_runtime_check
    apps/hlclient_stock_runtime_check/main.cpp
)
hlclient_enable_warnings(hlclient_stock_runtime_check)
target_link_libraries(
    hlclient_stock_runtime_check
    PRIVATE
        hlclient::goldsrc_stock_runtime_capture
        hlclient::hash_sha256)
set_property(
    TARGET hlclient_stock_runtime_check
    PROPERTY FOLDER "Tools/Research/GoldSrc")

if(BUILD_TESTING)
    find_program(
        HLCLIENT_STOCK_RUNTIME_POWERSHELL_EXECUTABLE
        NAMES pwsh powershell
        REQUIRED)

    add_test(
        NAME hlclient_stock_runtime_capture_validate_config
        COMMAND
            "${HLCLIENT_STOCK_RUNTIME_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File
                "${PROJECT_SOURCE_DIR}/scripts/test_stock_runtime_capture_validate_config.ps1"
            -CaptureToolPath $<TARGET_FILE:hlclient_stock_runtime_capture>)
    set_tests_properties(
        hlclient_stock_runtime_capture_validate_config
        PROPERTIES TIMEOUT 15)

    add_test(
        NAME hlclient_stock_runtime_restoration_guard
        COMMAND
            "${HLCLIENT_STOCK_RUNTIME_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File
                "${PROJECT_SOURCE_DIR}/scripts/capture_stock_runtime_state.ps1"
            -ValidateRestorationGuard)
    set_tests_properties(
        hlclient_stock_runtime_restoration_guard
        PROPERTIES TIMEOUT 30)

    add_test(
        NAME hlclient_stock_runtime_active_disabled
        COMMAND
            "${HLCLIENT_STOCK_RUNTIME_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File
                "${PROJECT_SOURCE_DIR}/scripts/test_stock_runtime_active_disabled.ps1")
    set_tests_properties(
        hlclient_stock_runtime_active_disabled
        PROPERTIES TIMEOUT 15)

    add_test(
        NAME hlclient_stock_runtime_evidence_pending
        COMMAND
            "${HLCLIENT_STOCK_RUNTIME_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File
                "${PROJECT_SOURCE_DIR}/scripts/test_stock_runtime_evidence_pending.ps1")
    set_tests_properties(
        hlclient_stock_runtime_evidence_pending
        PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING)
    foreach(
        usercmd_scenario
        IN ITEMS idle move look buttons batch loss-recovery)
        add_test(
            NAME hlclient_usercmd_check_${usercmd_scenario}
            COMMAND
                $<TARGET_FILE:hlclient_usercmd_check>
                --profile synthetic
                --scenario ${usercmd_scenario})
        set_tests_properties(
            hlclient_usercmd_check_${usercmd_scenario}
            PROPERTIES TIMEOUT 15)
    endforeach()
endif()

if(BUILD_TESTING)
    find_program(
        HLCLIENT_TEST_POWERSHELL_EXECUTABLE
        NAMES pwsh powershell
        REQUIRED)
    set(
        HLCLIENT_GOLDSRC_ASSET_CHECK_CLI_TEST_SCRIPT
        "${PROJECT_SOURCE_DIR}/scripts/test_goldsrc_asset_check_cli.ps1")

    add_test(
        NAME hlclient_goldsrc_asset_check_sprite_smoke
        COMMAND
            "${HLCLIENT_TEST_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${HLCLIENT_GOLDSRC_ASSET_CHECK_CLI_TEST_SCRIPT}"
            -ToolPath $<TARGET_FILE:hlclient_goldsrc_asset_check>
            -Mode sprite-smoke)
    set_tests_properties(
        hlclient_goldsrc_asset_check_sprite_smoke
        PROPERTIES TIMEOUT 30)

    foreach(
        forbidden_asset_option
        IN ITEMS
            native-model-path
            native-sprite-path
            raw-mdl
            raw-spr
            texture-companion-path
            sequence-group-path
            force-model-format
            force-sprite-format
            ignore-model-dependency
            skip-animation-validation
            dump-textures
            dump-animation
            render-model
            render-sprite
    )
        add_test(
            NAME
                hlclient_goldsrc_asset_check_rejects_${forbidden_asset_option}
            COMMAND
                "${HLCLIENT_TEST_POWERSHELL_EXECUTABLE}"
                -NoLogo
                -NoProfile
                -NonInteractive
                -ExecutionPolicy Bypass
                -File "${HLCLIENT_GOLDSRC_ASSET_CHECK_CLI_TEST_SCRIPT}"
                -ToolPath $<TARGET_FILE:hlclient_goldsrc_asset_check>
                -Mode forbidden-option
                -ForbiddenOption ${forbidden_asset_option}
        )
        set_tests_properties(
            hlclient_goldsrc_asset_check_rejects_${forbidden_asset_option}
            PROPERTIES TIMEOUT 30)
    endforeach()
endif()

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
        hlclient::interactive_preview
        hlclient::local_player_controller
        hlclient::renderer_opengl
        hlclient::glad
)
set_property(TARGET hlclient_world_viewer PROPERTY FOLDER "Tools/World")
set_target_properties(
    hlclient_world_viewer
    PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_executable(
    hlclient_prediction_viewer
    apps/hlclient_world_viewer/main.cpp
)
target_compile_definitions(
    hlclient_prediction_viewer PRIVATE HLCLIENT_PREDICTION_VIEWER=1)
hlclient_enable_warnings(hlclient_prediction_viewer)
target_link_libraries(
    hlclient_prediction_viewer
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
        hlclient::interactive_preview
        hlclient::local_player_controller
        hlclient::local_prediction
        hlclient::renderer_opengl
        hlclient::glad
)
set_property(
    TARGET hlclient_prediction_viewer PROPERTY FOLDER "Tools/Prediction")
set_target_properties(
    hlclient_prediction_viewer
    PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_executable(
    hlclient_entity_viewer
    apps/hlclient_entity_viewer/main.cpp
)
hlclient_enable_warnings(hlclient_entity_viewer)
target_link_libraries(
    hlclient_entity_viewer
    PRIVATE
        hlclient::goldsrc_bsp
        hlclient::goldsrc_brush_models
        hlclient::goldsrc_builtin_importers
        hlclient::goldsrc_world_texture_import
        hlclient::goldsrc_lightmaps
        hlclient::goldsrc_visual_asset_bundle
        hlclient::entity_interpolation
        hlclient::entity_visual_assets
        hlclient::entity_scene_package_build
        hlclient::entity_frame_composition
        hlclient::world_render_package
        hlclient::world_preview
        hlclient::local_asset_source
        hlclient::local_resources
        hlclient::platform
        hlclient::interactive_preview
        hlclient::renderer_opengl
        hlclient::glad
)
set_property(TARGET hlclient_entity_viewer PROPERTY FOLDER "Tools/Entities")
set_target_properties(
    hlclient_entity_viewer
    PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

if(BUILD_TESTING)
    set(
        HLCLIENT_ENTITY_VIEWER_CLI_TEST_SCRIPT
        "${PROJECT_SOURCE_DIR}/scripts/test_entity_viewer_cli.ps1")
    foreach(
        forbidden_entity_viewer_option
        IN ITEMS
            raw-entity
            entity-state-file
            snapshot-file
            inject-modelindex
            force-model-slot
            native-model-path
            native-sprite-path
            execute-event
            live-entity-hack
            ignore-projection-evidence
            force-additive
            force-index-alpha
            send-usercmd
    )
        add_test(
            NAME
                hlclient_entity_viewer_rejects_${forbidden_entity_viewer_option}
            COMMAND
                "${HLCLIENT_TEST_POWERSHELL_EXECUTABLE}"
                -NoLogo
                -NoProfile
                -NonInteractive
                -ExecutionPolicy Bypass
                -File "${HLCLIENT_ENTITY_VIEWER_CLI_TEST_SCRIPT}"
                -ToolPath $<TARGET_FILE:hlclient_entity_viewer>
                -ForbiddenOption ${forbidden_entity_viewer_option}
        )
        set_tests_properties(
            hlclient_entity_viewer_rejects_${forbidden_entity_viewer_option}
            PROPERTIES TIMEOUT 15)
    endforeach()
endif()
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

add_custom_command(
    TARGET hlclient_prediction_viewer
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3-shared>"
            "$<TARGET_FILE_DIR:hlclient_prediction_viewer>"
    COMMENT "Copying the SDL3 runtime next to hlclient_prediction_viewer"
    VERBATIM
)

add_custom_command(
    TARGET hlclient_entity_viewer
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3-shared>"
            "$<TARGET_FILE_DIR:hlclient_entity_viewer>"
    COMMENT "Copying the SDL3 runtime next to hlclient_entity_viewer"
    VERBATIM
)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT hlclient)

foreach(
    _hlclient_sanitized_target
    IN ITEMS
        hlclient_collision_api
        hlclient_goldsrc_bsp_collision
        hlclient_goldsrc_collision_scene
        hlclient_movement_api
        hlclient_prediction_api
        hlclient_goldsrc_stock_runtime
        hlclient_goldsrc_stock_runtime_capture
        hlclient_stock_authoritative_adapter
        hlclient_goldsrc_local_movement
        hlclient_local_prediction
        hlclient_prediction_visual
        hlclient_local_player_controller
        hlclient_movement_check
        hlclient_prediction_check
        hlclient_prediction_viewer
        hlclient_stock_runtime_check
        hlclient_world_viewer
)
    hlclient_enable_address_sanitizer(${_hlclient_sanitized_target})
endforeach()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
