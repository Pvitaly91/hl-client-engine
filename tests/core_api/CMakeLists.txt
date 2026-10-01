# This executable never links or includes the concrete game. The same host
# and engine objects are used by the ordinary client.
add_executable(hlclient_core_api_tests
    ../test_audio_api.cpp
    ../test_game_client_module.cpp
    ../test_game_camera_intent.cpp
    ../test_remote_player_visibility_trace.cpp
    ../test_packet_entity_decoder.cpp
    ../test_entity_snapshot_interpolation.cpp
    ../test_studio_pose_evaluator.cpp
    ../test_entity_render_frame_composer.cpp
    ../test_byte_io.cpp
    ../test_bit_reader.cpp
    ../test_input_state_tracker.cpp
    ../test_gameplay_input_intent.cpp
    ../test_goldsrc_usercmd_scheduler.cpp
    ../test_goldsrc_usercmd_history.cpp
    ../test_goldsrc_movement_environment.cpp
    ../test_goldsrc_local_movement_ground.cpp
    ../test_goldsrc_local_movement_jump.cpp
    ../test_goldsrc_local_movement_duck.cpp
    ../test_goldsrc_lightmap_import.cpp
    ../test_world_texture_resolution.cpp
    ../test_world_texture_set.cpp
    ../test_world_render_package.cpp
    ../test_render_camera_math.cpp)
target_link_libraries(hlclient_core_api_tests PRIVATE
    Catch2::Catch2WithMain
    hlclient::goldsrc_audio
    hlclient::game_api hlclient::game_client_host hlclient::game_policy
    hlclient::goldsrc_client_message
    hlclient::goldsrc_runtime_replay hlclient::client
    hlclient::goldsrc_usercmd_session hlclient::goldsrc_local_movement
    hlclient::gameplay_input hlclient::input_api hlclient::renderer_api
    hlclient::live_visual_control)
target_link_libraries(hlclient_core_api_tests PRIVATE
    hlclient::entity_interpolation hlclient::studio_pose
    hlclient::entity_frame_composition hlclient::entity_scene_render
    hlclient::entity_scene_package_build hlclient::entity_visual_assets)
target_link_libraries(hlclient_core_api_tests PRIVATE
    hlclient_goldsrc_bsp hlclient_goldsrc_world_texture_import
    hlclient_goldsrc_lightmaps hlclient_world_render_package)
target_compile_features(hlclient_core_api_tests PRIVATE cxx_std_20)
target_compile_definitions(hlclient_core_api_tests PRIVATE HLCLIENT_HAS_GAME_HALFLIFE=0)
hlclient_enable_warnings(hlclient_core_api_tests)
hlclient_enable_address_sanitizer(hlclient_core_api_tests)
set_property(TARGET hlclient_core_api_tests PROPERTY FOLDER "Tests/Core API")
add_test(NAME hlclient_core_api_offline COMMAND hlclient_core_api_tests --reporter compact)
set_tests_properties(hlclient_core_api_offline PROPERTIES LABELS "offline;core-api" TIMEOUT 120)

add_test(NAME hlclient_game_boundary_guard
    COMMAND "${CMAKE_COMMAND}"
        "-DHLCLIENT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DHLCLIENT_TEST_BINARY=${CMAKE_CURRENT_BINARY_DIR}/guard-fixtures"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/test_game_boundary_guard.cmake")
set_tests_properties(hlclient_game_boundary_guard PROPERTIES LABELS "offline;architecture" TIMEOUT 60)
