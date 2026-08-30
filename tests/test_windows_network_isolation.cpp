#include <hlclient/platform/windows/network_isolation.hpp>
#include <hlclient/platform/windows/process_orchestrator.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

namespace {

namespace windows = hlclient::platform::windows;

[[nodiscard]] windows::NetworkIsolationApplication synthetic_application(
    const std::wstring_view path,
    const std::byte discriminator = std::byte{1U})
{
    windows::WindowsBinaryIdentity identity;
    identity.canonical_path = std::filesystem::path{path};
    identity.snapshot.size = 1U;
    identity.snapshot.identity.volume_serial_number = 7U;
    identity.snapshot.identity.file_id[0U] = discriminator;
    identity.sha256[0U] = discriminator;
    identity.pe_machine = windows::WindowsPeMachine::x86;
    identity.anonymized_profile_fingerprint = std::string(64U, 'a');
    return windows::NetworkIsolationApplication{
        std::move(identity), {discriminator, std::byte{0U}}};
}

[[nodiscard]] std::filesystem::path sibling_probe()
{
    std::wstring module(32'768U, L'\0');
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    REQUIRE(size > 0U);
    REQUIRE(size < module.size());
    module.resize(size);
    return std::filesystem::path{module}.parent_path() /
        L"hlclient_network_isolation_probe.exe";
}

} // namespace

TEST_CASE("Windows isolation policy is exact and fail closed",
          "[platform][windows][stock-runtime][isolation]")
{
    windows::NetworkIsolationPolicy policy;
    CHECK_FALSE(windows::validate_network_isolation_policy(policy));

    policy.applications.push_back(
        synthetic_application(LR"(C:\research\probe.exe)"));
    REQUIRE(windows::validate_network_isolation_policy(policy));

    SECTION("every weakening is rejected") {
        policy.allow_ipv4_loopback = false;
        CHECK(windows::validate_network_isolation_policy(policy).code ==
              windows::NetworkIsolationErrorCode::policy_not_fail_closed);
        policy.allow_ipv4_loopback = true;
        policy.allow_ipv6_loopback = false;
        CHECK_FALSE(windows::validate_network_isolation_policy(policy));
        policy.allow_ipv6_loopback = true;
        policy.block_non_loopback_outbound = false;
        CHECK_FALSE(windows::validate_network_isolation_policy(policy));
        policy.block_non_loopback_outbound = true;
        policy.block_non_loopback_inbound_accept = false;
        CHECK_FALSE(windows::validate_network_isolation_policy(policy));
        policy.block_non_loopback_inbound_accept = true;
        policy.dynamic_session_required = false;
        CHECK_FALSE(windows::validate_network_isolation_policy(policy));
        policy.dynamic_session_required = true;
        policy.persistent_filters_allowed = true;
        CHECK_FALSE(windows::validate_network_isolation_policy(policy));
    }

    SECTION("empty application IDs are rejected") {
        policy.applications.front().wfp_application_id.clear();
        CHECK(windows::validate_network_isolation_policy(policy).code ==
              windows::NetworkIsolationErrorCode::invalid_application_id);
    }

    SECTION("duplicate path, file ID, or WFP app ID is rejected") {
        policy.applications.push_back(policy.applications.front());
        CHECK(windows::validate_network_isolation_policy(policy).code ==
              windows::NetworkIsolationErrorCode::duplicate_application);
    }
}

TEST_CASE("Windows isolation filter plan has exact loopback ranges",
          "[platform][windows][stock-runtime][isolation][plan]")
{
    windows::NetworkIsolationPolicy policy;
    policy.applications.push_back(
        synthetic_application(LR"(C:\research\one.exe)", std::byte{1U}));
    policy.applications.push_back(
        synthetic_application(LR"(C:\research\two.exe)", std::byte{2U}));
    const auto plan = windows::build_network_isolation_filter_plan(policy);
    REQUIRE(plan);
    CHECK(plan->ipv4_loopback_network_host_order == 0x7f000000U);
    CHECK(plan->ipv4_loopback_mask_host_order == 0xff000000U);
    CHECK(plan->ipv6_loopback_address[15U] == std::byte{1U});
    for (std::size_t index = 0U; index < 15U; ++index) {
        CHECK(plan->ipv6_loopback_address[index] == std::byte{0U});
    }
    CHECK(plan->ipv6_loopback_prefix_length == 128U);
    CHECK(plan->permit_filter_count == 8U);
    CHECK(plan->block_filter_count == 8U);
    CHECK(plan->dynamic_only);
    CHECK(plan->persistent_filter_count == 0U);
}

TEST_CASE("Windows isolation application and count bounds are explicit",
          "[platform][windows][stock-runtime][isolation][bounds]")
{
    windows::NetworkIsolationPolicy policy;
    for (std::size_t index = 0U;
         index < windows::kMaximumIsolatedApplications; ++index) {
        auto application = synthetic_application(
            L"C:\\research\\app" + std::to_wstring(index) + L".exe",
            static_cast<std::byte>(index + 1U));
        application.wfp_application_id[1U] =
            static_cast<std::byte>(index + 1U);
        policy.applications.push_back(std::move(application));
    }
    REQUIRE(windows::validate_network_isolation_policy(policy));
    policy.applications.push_back(synthetic_application(
        LR"(C:\research\overflow.exe)", std::byte{42U}));
    CHECK(windows::validate_network_isolation_policy(policy).code ==
          windows::NetworkIsolationErrorCode::too_many_applications);
}

TEST_CASE("Isolation heartbeat accepts only EOF and broken pipe as clean closure",
          "[platform][windows][stock-runtime][isolation][heartbeat]")
{
    using Disposition = windows::NetworkIsolationHeartbeatReadDisposition;
    CHECK(windows::classify_network_isolation_heartbeat_read(
              true, 1U, ERROR_SUCCESS) == Disposition::continue_waiting);
    CHECK(windows::classify_network_isolation_heartbeat_read(
              true, 0U, ERROR_SUCCESS) == Disposition::clean_eof);
    CHECK(windows::classify_network_isolation_heartbeat_read(
              false, 0U, ERROR_BROKEN_PIPE) == Disposition::clean_eof);
    CHECK(windows::classify_network_isolation_heartbeat_read(
              false, 0U, ERROR_OPERATION_ABORTED) == Disposition::error);
    CHECK(windows::classify_network_isolation_heartbeat_read(
              false, 0U, ERROR_INVALID_HANDLE) == Disposition::error);
}

TEST_CASE("Actual dynamic WFP canary is explicitly capability gated",
          "[platform][windows][stock-runtime][isolation][capability]")
{
    std::array<char, 8U> enabled{};
    const DWORD enabled_size = ::GetEnvironmentVariableA(
        "HLCLIENT_RUN_WFP_CAPABILITY_TEST", enabled.data(),
        static_cast<DWORD>(enabled.size()));
    if (enabled_size != 1U || std::string_view{enabled.data(), enabled_size} != "1") {
        SKIP("dynamic WFP capability test requires explicit local opt-in");
    }
    if (!windows::windows_process_is_elevated()) {
        SKIP("dynamic WFP capability test requires an elevated process");
    }
    const auto probe = sibling_probe();
    windows::WindowsBinaryIdentityErrorCode binary_error{};
    windows::NetworkIsolationErrorCode isolation_error{};
    const auto application = windows::observe_network_isolation_application(
        probe, binary_error, isolation_error);
    REQUIRE(application);
    windows::NetworkIsolationPolicy policy;
    policy.applications.push_back(*application);
    const auto result = windows::run_network_isolation_canary(probe, policy);
    REQUIRE(result);
    CHECK(result.ipv4_loopback_allowed);
    CHECK(result.non_loopback_os_denied);
}

TEST_CASE("Active-guard canary uses the supplied campaign job and exact count",
          "[platform][windows][stock-runtime][isolation][capability][campaign-job]")
{
    std::array<char, 8U> enabled{};
    const DWORD enabled_size = ::GetEnvironmentVariableA(
        "HLCLIENT_RUN_WFP_CAPABILITY_TEST", enabled.data(),
        static_cast<DWORD>(enabled.size()));
    if (enabled_size != 1U ||
        std::string_view{enabled.data(), enabled_size} != "1") {
        SKIP("dynamic WFP capability test requires explicit local opt-in");
    }
    if (!windows::windows_process_is_elevated()) {
        SKIP("dynamic WFP capability test requires an elevated process");
    }

    const auto probe = sibling_probe();
    windows::WindowsBinaryIdentityErrorCode binary_error{};
    windows::NetworkIsolationErrorCode isolation_error{};
    const auto application = windows::observe_network_isolation_application(
        probe, binary_error, isolation_error);
    REQUIRE(application);
    windows::NetworkIsolationPolicy policy;
    policy.applications.push_back(*application);
    auto [session, session_result] =
        windows::DynamicNetworkIsolationSession::start(policy);
    REQUIRE(session_result);

    auto [job, job_result] =
        windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(job_result);
    const auto result =
        windows::run_network_isolation_canary_under_existing_guard(job, probe);
    REQUIRE(result);
    CHECK(result.processes_started ==
          (result.ipv6_loopback_allowed ? 3U : 2U));
    CHECK(job.active_process_count() == 0U);
    session.close();
}

TEST_CASE("Redundant WFP owner remains effective after primary guard loss",
          "[platform][windows][stock-runtime][isolation][capability]"
          "[redundant-session]")
{
    std::array<char, 8U> enabled{};
    const DWORD enabled_size = ::GetEnvironmentVariableA(
        "HLCLIENT_RUN_WFP_CAPABILITY_TEST", enabled.data(),
        static_cast<DWORD>(enabled.size()));
    if (enabled_size != 1U ||
        std::string_view{enabled.data(), enabled_size} != "1") {
        SKIP("dynamic WFP capability test requires explicit local opt-in");
    }
    if (!windows::windows_process_is_elevated()) {
        SKIP("dynamic WFP capability test requires an elevated process");
    }

    const auto probe = sibling_probe();
    windows::WindowsBinaryIdentityErrorCode binary_error{};
    windows::NetworkIsolationErrorCode isolation_error{};
    const auto application = windows::observe_network_isolation_application(
        probe, binary_error, isolation_error);
    REQUIRE(application);
    windows::NetworkIsolationPolicy policy;
    policy.applications.push_back(*application);
    auto [backup, backup_result] =
        windows::DynamicNetworkIsolationSession::start(policy);
    REQUIRE(backup_result);
    REQUIRE(backup.active());
    auto [primary, primary_result] =
        windows::DynamicNetworkIsolationSession::start(policy);
    REQUIRE(primary_result);
    primary.close();
    REQUIRE_FALSE(primary.active());

    auto [campaign_job, job_result] =
        windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(job_result);
    const auto canary =
        windows::run_network_isolation_canary_under_existing_guard(
            campaign_job, probe);
    REQUIRE(canary);
    CHECK(canary.non_loopback_os_denied);
    CHECK(campaign_job.active_process_count() == 0U);
    CHECK(backup.active());
    backup.close();
}
