#include <hlclient/app/precache_manifest_exit_policy.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Precache manifest CLI exit policy accepts only a complete manifest",
          "[core][command-line][precache]")
{
    using hlclient::app::precache_manifest_exit_code;
    using Status = hlclient::goldsrc::PrecacheManifestCompleteness;

    CHECK(precache_manifest_exit_code(
              Status::complete_for_supported_local_profile) == 0);
    CHECK(precache_manifest_exit_code(Status::world_ready_but_incomplete) != 0);
    CHECK(precache_manifest_exit_code(Status::incomplete_missing_resources) != 0);
    CHECK(precache_manifest_exit_code(Status::blocked_unsafe_resources) != 0);
    CHECK(precache_manifest_exit_code(Status::unsupported_profile) != 0);
    CHECK(precache_manifest_exit_code(Status::local_io_failure) != 0);
    CHECK(precache_manifest_exit_code(
              Status::invalid_server_resource_correlation) != 0);
}
