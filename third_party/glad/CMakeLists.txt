add_library(hlclient_glad STATIC src/gl.c)
add_library(hlclient::glad ALIAS hlclient_glad)

target_include_directories(
    hlclient_glad
    SYSTEM PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(hlclient_glad PRIVATE ${CMAKE_DL_LIBS})

set_target_properties(
    hlclient_glad
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        FOLDER "ThirdParty/glad2"
)
