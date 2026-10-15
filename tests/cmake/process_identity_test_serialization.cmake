# Loaded after PRE_TEST discovery, when the individual Catch tests exist.
# Do not weaken the no-client assertion or serialize all offline/GL tests.
cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW)
if("Fake HLDS profile variants stay server-only and bounded" IN_LIST hlclient_tests_TESTS)
    set_tests_properties("Fake HLDS profile variants stay server-only and bounded"
        PROPERTIES RUN_SERIAL TRUE)
endif()
cmake_policy(POP)
