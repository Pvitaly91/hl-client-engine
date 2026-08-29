add_executable(hlclient_tests
    test_auth_material_file.cpp
    test_authentication_provider.cpp
    test_asset_manager.cpp
    test_asset_importer_dispatch.cpp
    test_asset_registry.cpp
    test_authoritative_player_state.cpp
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
    test_collision_contents.cpp
    test_collision_brush_model_trace.cpp
    test_collision_hull_trace.cpp
    test_collision_point_contents.cpp
    test_collision_scene_composition.cpp
    test_collision_world_package.cpp
    test_input_events.cpp
    test_input_state_tracker.cpp
    test_sdl_input_translation.cpp
    test_sdl_single_event_pump.cpp
    test_gameplay_input_bindings.cpp
    test_gameplay_input_intent.cpp
    test_first_person_camera.cpp
    test_goldsrc_client_move_message.cpp
    test_goldsrc_move_checksum.cpp
    test_goldsrc_usercmd_delta_codec.cpp
    test_goldsrc_usercmd_duration.cpp
    test_goldsrc_usercmd_fake_hlds_integration.cpp
    test_goldsrc_usercmd_history.cpp
    test_goldsrc_usercmd_input_adapter.cpp
    test_goldsrc_usercmd_packet_planner.cpp
    test_goldsrc_usercmd_scheduler.cpp
    test_goldsrc_usercmd_schema_binding.cpp
    test_goldsrc_usercmd_state.cpp
    test_goldsrc_usercmd_transmission_stage.cpp
    test_entity_first_person_camera.cpp
    test_interactive_entity_visibility.cpp
    test_interactive_preview_controller.cpp
    test_gameplay_input_camera_campaigns.cpp
    test_opengl_interactive_camera.cpp
    test_opengl_player_wall_contact.cpp
    test_connectionless_packet.cpp
    test_bit_reader.cpp
    test_delta_description.cpp
    test_delta_description_stream.cpp
    test_delta_description_stage.cpp
    test_delta_schema_registry.cpp
    test_goldsrc_delta_value_decoder.cpp
    test_entity_baseline_registry.cpp
    test_entity_full_snapshot.cpp
    test_entity_delta_snapshot.cpp
    test_entity_snapshot_history.cpp
    test_post_resource_signon_stream.cpp
    test_entity_snapshot_signon_stage.cpp
    test_entity_snapshot_fake_hlds_integration.cpp
    test_entity_visual_projection.cpp
    test_entity_visual_binding.cpp
    test_entity_visual_asset_library.cpp
    test_entity_visual_asset_stage.cpp
    test_entity_snapshot_interpolation.cpp
    test_entity_interpolation_stage.cpp
    test_studio_pose_evaluator.cpp
    test_studio_model_render_asset.cpp
    test_entity_render_sprite_asset.cpp
    test_sprite_frame_selector.cpp
    test_sprite_billboard_basis.cpp
    test_entity_scene_render_package.cpp
    test_entity_scene_package_stage.cpp
    test_entity_render_frame_composer.cpp
    test_opengl_studio_entity_renderer.cpp
    test_opengl_sprite_entity_renderer.cpp
    test_runtime_entity_playback_integration.cpp
    test_game_paths.cpp
    test_goldsrc_bsp_header.cpp
    test_goldsrc_bsp_collision_source.cpp
    test_goldsrc_bsp_records.cpp
    test_goldsrc_bsp_textures.cpp
    test_goldsrc_bsp_face_loop.cpp
    test_goldsrc_bsp_face_orientation.cpp
    test_goldsrc_bsp_world_geometry.cpp
    test_goldsrc_bsp_models.cpp
    test_goldsrc_brush_submodels.cpp
    test_goldsrc_bsp_spatial_structure.cpp
    test_goldsrc_entity_document.cpp
    test_goldsrc_brush_transform.cpp
    test_goldsrc_brush_rigid_transform.cpp
    test_goldsrc_collision_world_builder.cpp
    test_goldsrc_brush_render_library.cpp
    test_goldsrc_spawn_camera.cpp
    test_goldsrc_world_scene_builder.cpp
    test_brush_submodel_instances.cpp
    test_goldsrc_pvs_decoder.cpp
    test_world_spatial_package.cpp
    test_world_spatial_query.cpp
    test_goldsrc_lightmap_extents.cpp
    test_goldsrc_lightmap_import.cpp
    test_goldsrc_bsp_importer.cpp
    test_goldsrc_bsp_mutation_corpus.cpp
    test_goldsrc_bsp_embedded_textures.cpp
    test_goldsrc_sprite_header.cpp
    test_goldsrc_sprite_palette.cpp
    test_goldsrc_sprite_frames.cpp
    test_goldsrc_sprite_groups.cpp
    test_goldsrc_sprite_formats.cpp
    test_goldsrc_visual_importers.cpp
    test_goldsrc_studio_header.cpp
    test_goldsrc_studio_records.cpp
    test_goldsrc_studio_skeleton.cpp
    test_goldsrc_studio_geometry.cpp
    test_goldsrc_studio_bodyparts.cpp
    test_goldsrc_studio_textures.cpp
    test_goldsrc_studio_animation.cpp
    test_goldsrc_studio_dependencies.cpp
    test_goldsrc_visual_asset_operation.cpp
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
    test_sha256.cpp
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
    test_world_lightmap_atlas.cpp
    test_world_render_package.cpp
    test_world_render_package_stage.cpp
    test_world_spatial_scene_stage.cpp
    test_render_camera_math.cpp
    test_world_view_frustum.cpp
    test_world_visibility_resolver.cpp
    test_world_visible_draw_list.cpp
    test_world_scene_render_package.cpp
    test_world_preview_scene.cpp
    test_precache_slot_tables.cpp
    test_precache_manifest.cpp
    test_precache_asset_dispatch_plan.cpp
    test_precache_asset_dispatch_stage.cpp
    test_precache_manifest_exit_policy.cpp
    test_precache_manifest_stage.cpp
    test_local_resource_consistency_provider.cpp
    test_move_vars.cpp
    test_movement_environment_stage.cpp
    test_local_player_movement_state.cpp
    test_goldsrc_movement_environment.cpp
    test_goldsrc_movement_math.cpp
    test_local_movement_collision.cpp
    test_goldsrc_local_movement_gravity.cpp
    test_goldsrc_local_movement_ground.cpp
    test_goldsrc_local_movement_jump.cpp
    test_goldsrc_local_movement_duck.cpp
    test_goldsrc_local_movement_slide.cpp
    test_goldsrc_local_movement_step.cpp
    test_local_player_movement_integration.cpp
    test_local_player_prediction_controller.cpp
    test_local_player_prepared_update.cpp
    test_local_prediction_history.cpp
    test_local_prediction_reconciliation.cpp
    test_player_walk_camera.cpp
    test_player_walk_failure_latch.cpp
    test_player_wall_contact_stability.cpp
    test_prediction_session_identity.cpp
    test_prediction_state_comparison.cpp
    test_prediction_visual_correction.cpp
    test_synthetic_authoritative_player.cpp
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
    test_netchan_unreliable_context.cpp
    test_opengl_world_renderer.cpp
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
        SDL3::SDL3-shared
        hlclient::bzip2
        hlclient_app_support
        hlclient_auth
        hlclient_asset_api
        hlclient_asset_dispatch
        hlclient_asset_manager
        hlclient_collision_api
        hlclient_core
        hlclient_filesystem
        hlclient_goldsrc
        hlclient_goldsrc_byte_io
        hlclient_goldsrc_bsp
        hlclient_goldsrc_bsp_collision
        hlclient_goldsrc_brush_transform
        hlclient_goldsrc_collision_scene
        hlclient_goldsrc_builtin_importers
        hlclient_goldsrc_brush_models
        hlclient_goldsrc_indexed_texture
        hlclient_goldsrc_lightmaps
        hlclient_goldsrc_spatial
        hlclient_goldsrc_studio_model
        hlclient_goldsrc_sprite
        hlclient_goldsrc_visual_asset_bundle
        hlclient_goldsrc_wad3
        hlclient_goldsrc_client
        hlclient_goldsrc_asset_dispatch
        hlclient_goldsrc_resource_readiness
        hlclient_goldsrc_local_resources
        hlclient_goldsrc_local_movement
        hlclient_goldsrc_move_vars_api
        hlclient_goldsrc_netchan
        hlclient_goldsrc_usercmd_api
        hlclient_goldsrc_usercmd_codec
        hlclient_goldsrc_usercmd_sampling
        hlclient_goldsrc_usercmd_session
        hlclient_goldsrc_delta_values
        hlclient_goldsrc_entity_snapshots
        hlclient_entity_visual_projection
        hlclient_entity_visual_assets
        hlclient_entity_interpolation
        hlclient_entity_interpolation_stage
        hlclient_studio_pose
        hlclient_entity_render_api
        hlclient_entity_scene_render
        hlclient_entity_scene_package_build
        hlclient_entity_frame_composition
        hlclient_goldsrc_post_resource_signon
        hlclient_goldsrc_signon
        hlclient_goldsrc_world_textures
        hlclient_goldsrc_world_render
        hlclient_hash_md5
        hlclient_hash_sha256
        hlclient_local_resources
        hlclient_local_asset_source
        hlclient_network
        hlclient_scene_api
        hlclient_renderer_null
        hlclient_renderer_opengl
        hlclient_platform
        hlclient_input_api
        hlclient_platform_sdl_input
        hlclient_gameplay_input
        hlclient_gameplay_camera
        hlclient_gameplay_camera_render_adapter
        hlclient_local_player_controller
        hlclient_local_prediction
        hlclient_prediction_api
        hlclient_prediction_visual
        hlclient_interactive_preview
        hlclient_glad
        hlclient_world_render
        hlclient_world_spatial
        hlclient_world_scene_renderer
        hlclient_world_visibility
        hlclient_world_preview
        hlclient_resource_consistency_api
        hlclient_resource_consistency_local
)

target_compile_features(hlclient_tests PRIVATE cxx_std_20)
hlclient_enable_warnings(hlclient_tests)
hlclient_enable_address_sanitizer(hlclient_tests)
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

add_executable(
    hlclient_collision_fixture_writer
    write_synthetic_collision_fixture.cpp
)
target_compile_features(hlclient_collision_fixture_writer PRIVATE cxx_std_20)
hlclient_enable_warnings(hlclient_collision_fixture_writer)
set_target_properties(
    hlclient_collision_fixture_writer
    PROPERTIES FOLDER "Tests/Fixtures"
)

set(
    HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/collision-checker-fixture"
)
add_test(
    NAME hlclient_collision_check_synthetic_fixture_setup
    COMMAND
        $<TARGET_FILE:hlclient_collision_fixture_writer>
        "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
)
set_tests_properties(
    hlclient_collision_check_synthetic_fixture_setup
    PROPERTIES
        FIXTURES_SETUP hlclient_collision_checker_fixture
        TIMEOUT 30
)

add_test(
    NAME hlclient_collision_check_synthetic_summary
    COMMAND
        $<TARGET_FILE:hlclient_collision_check>
        --basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
        --game valve
        --map maps/test_collision.bsp
        --scenario summary
)
add_test(
    NAME hlclient_collision_check_synthetic_deterministic_probes
    COMMAND
        $<TARGET_FILE:hlclient_collision_check>
        --basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
        --game valve
        --map maps/test_collision.bsp
        --scenario deterministic-probes
)
add_test(
    NAME hlclient_movement_check_synthetic_deterministic_route
    COMMAND
        $<TARGET_FILE:hlclient_movement_check>
        --basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
        --game valve
        --map maps/test_movement.bsp
        --scenario deterministic-route
)
foreach(
    wall_scenario
    IN ITEMS
        wall-contact-stress
        wall-glance-stress
        corner-contact-stress
        jump-wall-stress
        duck-wall-stress
)
    string(REPLACE "-" "_" wall_scenario_name "${wall_scenario}")
    add_test(
        NAME hlclient_movement_check_synthetic_${wall_scenario_name}
        COMMAND
            $<TARGET_FILE:hlclient_movement_check>
            --basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
            --game valve
            --map maps/test_movement.bsp
            --scenario ${wall_scenario}
    )
    set_tests_properties(
        hlclient_movement_check_synthetic_${wall_scenario_name}
        PROPERTIES
            FIXTURES_REQUIRED hlclient_collision_checker_fixture
            TIMEOUT 180
    )
endforeach()
add_test(
    NAME hlclient_movement_verify_synthetic_deterministic_route
    COMMAND
        "${HLCLIENT_TEST_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -ExecutionPolicy Bypass
        -File "${PROJECT_SOURCE_DIR}/scripts/verify_local_player_movement.ps1"
        -ToolPath $<TARGET_FILE:hlclient_movement_check>
        -Basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
        -Game valve
        -Maps maps/test_movement.bsp
        -Scenarios deterministic-route
)
set_tests_properties(
    hlclient_collision_check_synthetic_summary
    hlclient_collision_check_synthetic_deterministic_probes
    PROPERTIES
        FIXTURES_REQUIRED hlclient_collision_checker_fixture
        PASS_REGULAR_EXPRESSION "\\[collision\\] result=success"
        TIMEOUT 30
)
set_tests_properties(
    hlclient_movement_check_synthetic_deterministic_route
    PROPERTIES
        FIXTURES_REQUIRED hlclient_collision_checker_fixture
        PASS_REGULAR_EXPRESSION "\\[movement\\] grounded=true"
        TIMEOUT 30
)
set_tests_properties(
    hlclient_movement_verify_synthetic_deterministic_route
    PROPERTIES
        FIXTURES_REQUIRED hlclient_collision_checker_fixture
        PASS_REGULAR_EXPRESSION "\\[movement-verify\\] result=success"
        TIMEOUT 30
)

foreach(
    prediction_scenario
    IN ITEMS
        exact-authority
        delayed-authority
        small-correction
        velocity-correction
        large-correction
        teleport
        stale-duplicate
        wall-replay
        jump-replay
        duck-replay
        history-backpressure
        hard-reset
        mixed
        deterministic-route
)
    string(REPLACE "-" "_" prediction_scenario_name
        "${prediction_scenario}")
    if(prediction_scenario STREQUAL "exact-authority")
        set(prediction_authority_delay 0)
    else()
        set(prediction_authority_delay 8)
    endif()
    add_test(
        NAME hlclient_prediction_check_synthetic_${prediction_scenario_name}
        COMMAND
            $<TARGET_FILE:hlclient_prediction_check>
            --basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
            --game valve
            --map maps/test_movement.bsp
            --scenario ${prediction_scenario}
            --authority-delay-commands ${prediction_authority_delay}
            --commands 1000
    )
    set_tests_properties(
        hlclient_prediction_check_synthetic_${prediction_scenario_name}
        PROPERTIES
            FIXTURES_REQUIRED hlclient_collision_checker_fixture
            PASS_REGULAR_EXPRESSION "\\[prediction\\] result=success"
            TIMEOUT 180
    )
endforeach()

add_test(
    NAME hlclient_prediction_check_rejects_raw_authority
    COMMAND
        "${HLCLIENT_TEST_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -ExecutionPolicy Bypass
        -File "${PROJECT_SOURCE_DIR}/scripts/test_prediction_check_cli.ps1"
        -ToolPath $<TARGET_FILE:hlclient_prediction_check>
        -Basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
        -Mode raw-authority
)
add_test(
    NAME hlclient_prediction_check_rejects_zero_commands
    COMMAND
        "${HLCLIENT_TEST_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -ExecutionPolicy Bypass
        -File "${PROJECT_SOURCE_DIR}/scripts/test_prediction_check_cli.ps1"
        -ToolPath $<TARGET_FILE:hlclient_prediction_check>
        -Basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
        -Mode zero-commands
)
add_test(
    NAME hlclient_prediction_check_rejects_excessive_delay
    COMMAND
        "${HLCLIENT_TEST_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -ExecutionPolicy Bypass
        -File "${PROJECT_SOURCE_DIR}/scripts/test_prediction_check_cli.ps1"
        -ToolPath $<TARGET_FILE:hlclient_prediction_check>
        -Basedir "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
        -Mode excessive-delay
)
set_tests_properties(
    hlclient_prediction_check_rejects_raw_authority
    hlclient_prediction_check_rejects_zero_commands
    hlclient_prediction_check_rejects_excessive_delay
    PROPERTIES
        FIXTURES_REQUIRED hlclient_collision_checker_fixture
        TIMEOUT 30
)

add_test(
    NAME hlclient_collision_check_synthetic_fixture_cleanup
    COMMAND
        "${CMAKE_COMMAND}" -E remove_directory
        "${HLCLIENT_COLLISION_CHECKER_FIXTURE_ROOT}"
)
set_tests_properties(
    hlclient_collision_check_synthetic_fixture_cleanup
    PROPERTIES
        FIXTURES_CLEANUP hlclient_collision_checker_fixture
        TIMEOUT 30
)
