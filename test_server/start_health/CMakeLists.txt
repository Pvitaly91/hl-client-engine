cmake_minimum_required(VERSION 3.25)
project(hlclient_test_start_health LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The owned GoldSrc test addon requires Win32")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# This standalone test-server build is never added to engine/client targets.
include(FetchContent)
FetchContent_Declare(metamod_api
  URL https://codeload.github.com/Bots-United/metamod-p/tar.gz/dc4f6d8d6271658268a19e22cbc88706f9c1c78e
  URL_HASH SHA256=10d5fcf73ed946d8a4b036214122b265a02a174233f71b57287c31d431a41c6c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(metamod_api)
add_library(hlclient_test_start_health SHARED plugin.cpp exports.def)
# The pinned C ABI predates const string parameters. Scope its compatibility
# switch to this test DLL, never engine/client compilation.
target_compile_options(hlclient_test_start_health PRIVATE /Zc:strictStrings-)
target_include_directories(hlclient_test_start_health SYSTEM PRIVATE
  ${metamod_api_SOURCE_DIR}/metamod ${metamod_api_SOURCE_DIR}/hlsdk/dlls
  ${metamod_api_SOURCE_DIR}/hlsdk/engine ${metamod_api_SOURCE_DIR}/hlsdk/common
  ${metamod_api_SOURCE_DIR}/hlsdk/pm_shared)
set_property(TARGET hlclient_test_start_health PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_executable(start_health_offline_test offline_test.cpp)
target_compile_options(start_health_offline_test PRIVATE /Zc:strictStrings-)
target_include_directories(start_health_offline_test SYSTEM PRIVATE
  ${metamod_api_SOURCE_DIR}/metamod ${metamod_api_SOURCE_DIR}/hlsdk/dlls
  ${metamod_api_SOURCE_DIR}/hlsdk/engine ${metamod_api_SOURCE_DIR}/hlsdk/common
  ${metamod_api_SOURCE_DIR}/hlsdk/pm_shared)
enable_testing()
add_test(NAME start_health_fake_engine COMMAND start_health_offline_test $<TARGET_FILE:hlclient_test_start_health>)
