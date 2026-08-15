add_executable(hlclient_tests
    test_auth_material_file.cpp
    test_authentication_provider.cpp
    test_asset_manager.cpp
    test_asset_registry.cpp
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
    test_game_paths.cpp
    test_handshake_connect_response.cpp
    test_handshake_netchan.cpp
    test_info_string.cpp
    test_network_address.cpp
    test_netchan_bootstrap.cpp
    test_netchan_fragments.cpp
    test_netchan_packet.cpp
    test_netchan_reliable.cpp
    test_netchan_sequence.cpp
    test_netchan_udp_integration.cpp
    test_scene_source.cpp
    test_udp_socket.cpp
)

target_link_libraries(hlclient_tests
    PRIVATE
        Catch2::Catch2WithMain
        hlclient_app_support
        hlclient_auth
        hlclient_asset_api
        hlclient_asset_manager
        hlclient_core
        hlclient_filesystem
        hlclient_goldsrc
        hlclient_goldsrc_client
        hlclient_goldsrc_netchan
        hlclient_network
        hlclient_scene_api
        hlclient_renderer_null
)

target_compile_features(hlclient_tests PRIVATE cxx_std_20)
hlclient_enable_warnings(hlclient_tests)

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
