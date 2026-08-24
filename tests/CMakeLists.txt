add_executable(hlclient_tests
    test_auth_material_file.cpp
    test_authentication_provider.cpp
    test_asset_manager.cpp
    test_asset_importer_dispatch.cpp
    test_asset_registry.cpp
    test_approved_asset_source.cpp
    test_byte_io.cpp
    test_challenge_exchange.cpp
    test_challenge_protocol.cpp
    test_challenge_udp_integration.cpp
    test_connect_request.cpp
    test_connect_request_stage.cpp
    test_connect_response.cpp
    test_connect_response_wait.cpp
    test_connect_response_udp_integration.cpp
    test_connect_udp_integration.cpp
    test_command_line.cpp
    test_connectionless_packet.cpp
    test_bit_reader.cpp
    test_delta_description.cpp
    test_delta_description_stream.cpp
    test_delta_description_stage.cpp
    test_delta_schema_registry.cpp
    test_game_paths.cpp
    test_goldsrc_bsp_header.cpp
    test_goldsrc_bsp_records.cpp
    test_goldsrc_bsp_textures.cpp
    test_goldsrc_bsp_face_loop.cpp
    test_goldsrc_bsp_world_geometry.cpp
    test_goldsrc_bsp_models.cpp
    test_goldsrc_bsp_spatial_structure.cpp
    test_goldsrc_bsp_importer.cpp
    test_goldsrc_bsp_mutation_corpus.cpp
    test_goldsrc_bsp_embedded_textures.cpp
    test_goldsrc_indexed_texture.cpp
    test_goldsrc_wad3_catalog.cpp
    test_goldsrc_wad3_texture.cpp
    test_goldsrc_worldspawn_wad_references.cpp
    test_handshake_connect_response.cpp
    test_handshake_netchan.cpp
    test_info_string.cpp
    test_initial_signon_request.cpp
    test_initial_signon_stage.cpp
    test_initial_signon_udp_integration.cpp
    test_md5.cpp
    test_local_resource_paths.cpp
    test_local_resource_search_roots.cpp
    test_local_read_only_file.cpp
    test_local_resource_locator.cpp
    test_local_resource_md5.cpp
    test_goldsrc_local_resource_mapping.cpp
    test_local_resource_inventory.cpp
    test_local_resource_readiness.cpp
    test_local_resource_correlation.cpp
    test_world_resource_selection.cpp
    test_world_texture_resolution.cpp
    test_world_texture_set.cpp
    test_world_texture_import_stage.cpp
    test_precache_slot_tables.cpp
    test_precache_manifest.cpp
    test_precache_asset_dispatch_plan.cpp
    test_precache_asset_dispatch_stage.cpp
    test_precache_manifest_exit_policy.cpp
    test_precache_manifest_stage.cpp
    test_local_resource_consistency_provider.cpp
    test_move_vars.cpp
    test_movement_environment_stage.cpp
    test_post_movevars_stream.cpp
    test_network_address.cpp
    test_netchan_bootstrap.cpp
    test_netchan_driver.cpp
    test_netchan_fragment_packet.cpp
    test_netchan_fragment_udp_integration.cpp
    test_netchan_outgoing_fragments.cpp
    test_netchan_packet.cpp
    test_netchan_reassembly.cpp
    test_netchan_reliable.cpp
    test_netchan_reliable_udp_integration.cpp
    test_netchan_sequence.cpp
    test_netchan_session.cpp
    test_netchan_udp_integration.cpp
    test_scene_source.cpp
    test_service_message_stream.cpp
    test_pre_resource_service_stream.cpp
    test_pre_resource_signon_stage.cpp
    test_pre_resource_udp_integration.cpp
    test_post_resource_list_stream.cpp
    test_resource_list.cpp
    test_resource_list_stage.cpp
    test_resource_client_response.cpp
    test_resource_client_response_stage.cpp
    test_resource_response_carrier.cpp
    test_resource_consistency_provider.cpp
    test_resource_transition_control.cpp
    test_resource_transition_request.cpp
    test_resource_transition_stage.cpp
    test_resource_transition_udp_integration.cpp
    test_server_info.cpp
    test_service_payload_envelope.cpp
    test_udp_socket.cpp
    test_user_info_stream.cpp
    test_user_info_update.cpp
)

target_link_libraries(hlclient_tests
    PRIVATE
        Catch2::Catch2WithMain
        hlclient::bzip2
        hlclient_app_support
        hlclient_auth
        hlclient_asset_api
        hlclient_asset_dispatch
        hlclient_asset_manager
        hlclient_core
        hlclient_filesystem
        hlclient_goldsrc
        hlclient_goldsrc_bsp
        hlclient_goldsrc_indexed_texture
        hlclient_goldsrc_wad3
        hlclient_goldsrc_client
        hlclient_goldsrc_asset_dispatch
        hlclient_goldsrc_resource_readiness
        hlclient_goldsrc_local_resources
        hlclient_goldsrc_netchan
        hlclient_goldsrc_signon
        hlclient_goldsrc_world_textures
        hlclient_hash_md5
        hlclient_local_resources
        hlclient_local_asset_source
        hlclient_network
        hlclient_scene_api
        hlclient_renderer_null
        hlclient_resource_consistency_api
        hlclient_resource_consistency_local
)

target_compile_features(hlclient_tests PRIVATE cxx_std_20)
hlclient_enable_warnings(hlclient_tests)
add_dependencies(hlclient_tests hlclient)

set_target_properties(hlclient_tests PROPERTIES FOLDER "Tests")

if(DEFINED catch2_SOURCE_DIR AND EXISTS "${catch2_SOURCE_DIR}/extras/Catch.cmake")
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch OPTIONAL)
if(COMMAND catch_discover_tests)
    catch_discover_tests(
        hlclient_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 30
    )
else()
    add_test(NAME hlclient_tests COMMAND hlclient_tests)
    set_tests_properties(hlclient_tests PROPERTIES TIMEOUT 30)
endif()

add_test(
    NAME hlclient_null_renderer_smoke
    COMMAND $<TARGET_FILE:hlclient> --renderer null
)
set_tests_properties(
    hlclient_null_renderer_smoke
    PROPERTIES
        ENVIRONMENT "HLCLIENT_SMOKE_TEST_FRAMES=2"
        TIMEOUT 30
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
