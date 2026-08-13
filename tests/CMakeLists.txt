add_executable(hlclient_tests
    test_byte_io.cpp
    test_command_line.cpp
    test_game_paths.cpp
    test_network_address.cpp
    test_udp_socket.cpp
)

target_link_libraries(hlclient_tests
    PRIVATE
        Catch2::Catch2WithMain
        hlclient_core
        hlclient_filesystem
        hlclient_goldsrc
        hlclient_network
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
