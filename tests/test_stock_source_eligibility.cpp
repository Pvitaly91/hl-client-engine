#include <hlclient/platform/windows/stock_source_eligibility.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

namespace fs = std::filesystem;
namespace windows = hlclient::platform::windows;

class ExactTemporaryDirectory final {
public:
    ExactTemporaryDirectory()
    {
        // Hosted TEMP can be expressed through a short-name or reparse
        // ancestor.  That is correctly rejected by the production exact-root
        // gate, so keep this profile fixture below the canonical test binary
        // directory instead of weakening the gate or skipping the assertion.
        std::array<wchar_t, 32'768U> module{};
        const DWORD length = ::GetModuleFileNameW(
            nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (length == 0U || length >= module.size()) return;
        std::error_code canonical_error;
        parent_ = fs::canonical(
            fs::path{std::wstring_view{module.data(), length}}.parent_path(),
            canonical_error);
        if (canonical_error) {
            parent_.clear();
            return;
        }
        static std::atomic_uint32_t ordinal{0U};
        for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
            const auto name = L"hlclient-source-eligibility-test-" +
                std::to_wstring(::GetCurrentProcessId()) + L"-" +
                std::to_wstring(::GetTickCount64()) + L"-" +
                std::to_wstring(ordinal.fetch_add(1U));
            const auto candidate = (parent_ / name).lexically_normal();
            if (::CreateDirectoryW(candidate.c_str(), nullptr) != FALSE) {
                path_ = candidate;
                break;
            }
        }
    }

    ~ExactTemporaryDirectory()
    {
        if (!path_.empty() && path_.is_absolute() &&
            path_.parent_path() == parent_ &&
            path_.filename().wstring().starts_with(
                L"hlclient-source-eligibility-test-")) {
            std::error_code error;
            static_cast<void>(fs::remove_all(path_, error));
        }
    }

    ExactTemporaryDirectory(const ExactTemporaryDirectory&) = delete;
    ExactTemporaryDirectory& operator=(const ExactTemporaryDirectory&) = delete;

    [[nodiscard]] bool valid() const noexcept { return !path_.empty(); }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path parent_;
    fs::path path_;
};

[[nodiscard]] bool write_text(
    const fs::path& path, const std::string_view text)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    return static_cast<bool>(output);
}

[[nodiscard]] windows::StockResearchTopologySummary safe_topology()
{
    windows::StockResearchTopologySummary topology;
    topology.categories = {
        windows::StockResearchTopologyCategory::ordinary_tree};
    topology.inspection_complete = true;
    topology.safe_to_materialize = true;
    topology.entry_count = 4U;
    topology.byte_count = 1024U;
    return topology;
}

[[nodiscard]] windows::StockSourceEligibilityAssessmentInput eligible_input()
{
    windows::StockSourceEligibilityAssessmentInput input;
    input.topology = safe_topology();
    input.exact_root = true;
    input.reparse_diagnostics_complete = true;
    input.client_profile =
        windows::StockSourceComponentProfileStatus::valid;
    input.server_profile =
        windows::StockSourceComponentProfileStatus::valid;
    input.app_profile = windows::StockSourceComponentProfileStatus::valid;
    return input;
}

} // namespace

TEST_CASE(
    "Stock candidate source eligibility requires every independent gate",
    "[windows][stock-runtime][source-eligibility][policy]")
{
    SECTION("clean eligible tree") {
        const auto result = windows::assess_stock_source_eligibility(
            eligible_input());
        CHECK(result.topology_safe);
        CHECK(result.research_copy_eligible);
        CHECK(result.status == windows::StockSourceEligibilityStatus::success);
        CHECK(result.escaped_target_count == 0U);
        CHECK(result.dangling_target_count == 0U);
        CHECK(result.unsupported_tag_count == 0U);
        CHECK(result.alternate_data_stream_count == 0U);
    }

    SECTION("escaped dangling target") {
        auto input = eligible_input();
        input.topology.safe_to_materialize = false;
        input.topology.escaped_target_count = 1U;
        input.dangling_target_count = 1U;
        // A failed legacy topology preflight cannot prove an exact root, but
        // the independent no-publication diagnostic still reports the exact
        // target reason without making the source eligible.
        input.exact_root = false;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.topology_safe);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status ==
              windows::StockSourceEligibilityStatus::dangling_target);
        CHECK(result.dangling_target_count == 1U);
    }

    SECTION("unsupported tag") {
        auto input = eligible_input();
        input.topology.safe_to_materialize = false;
        input.unsupported_tag_count = 2U;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status == windows::StockSourceEligibilityStatus::
                                   unsupported_reparse_tag);
        CHECK(result.unsupported_tag_count == 2U);
    }

    SECTION("wrong client version") {
        auto input = eligible_input();
        input.client_profile =
            windows::StockSourceComponentProfileStatus::version_mismatch;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status == windows::StockSourceEligibilityStatus::
                                   client_profile_invalid);
    }

    SECTION("invalid client signature") {
        auto input = eligible_input();
        input.client_profile =
            windows::StockSourceComponentProfileStatus::signature_invalid;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status == windows::StockSourceEligibilityStatus::
                                   client_profile_invalid);
    }

    SECTION("wrong server profile") {
        auto input = eligible_input();
        input.server_profile =
            windows::StockSourceComponentProfileStatus::machine_mismatch;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status == windows::StockSourceEligibilityStatus::
                                   server_profile_invalid);
    }

    SECTION("wrong app id") {
        auto input = eligible_input();
        input.app_profile =
            windows::StockSourceComponentProfileStatus::app_id_mismatch;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status ==
              windows::StockSourceEligibilityStatus::app_profile_invalid);
    }

    SECTION("wrong app build") {
        auto input = eligible_input();
        input.app_profile =
            windows::StockSourceComponentProfileStatus::build_id_mismatch;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status ==
              windows::StockSourceEligibilityStatus::app_profile_invalid);
    }

    SECTION("alternate data stream") {
        auto input = eligible_input();
        input.topology.safe_to_materialize = false;
        input.topology.alternate_data_stream_count = 1U;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status == windows::StockSourceEligibilityStatus::
                                   alternate_data_stream);
    }

    SECTION("already prepared") {
        auto input = eligible_input();
        input.source_already_prepared = true;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status == windows::StockSourceEligibilityStatus::
                                   source_already_prepared);
    }

    SECTION("remote or subst root") {
        auto input = eligible_input();
        input.exact_root = false;
        input.topology.categories = {
            windows::StockResearchTopologyCategory::source_subst_drive};
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.topology_safe);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status ==
              windows::StockSourceEligibilityStatus::topology_unsafe);
    }

    SECTION("incomplete diagnostics do not turn unavailable counts into proof") {
        auto input = eligible_input();
        input.reparse_diagnostics_complete = false;
        const auto result =
            windows::assess_stock_source_eligibility(input);
        CHECK_FALSE(result.topology_safe);
        CHECK_FALSE(result.research_copy_eligible);
        CHECK(result.status ==
              windows::StockSourceEligibilityStatus::topology_incomplete);
    }
}

TEST_CASE(
    "Stock candidate source validation is read-only and rejects unsigned files",
    "[windows][stock-runtime][source-eligibility][filesystem][read-only]")
{
    ExactTemporaryDirectory temporary;
    REQUIRE(temporary.valid());
    const auto steamapps = temporary.path() / L"steamapps";
    const auto source = steamapps / L"common" / L"Half-Life";
    REQUIRE(fs::create_directories(source));
    REQUIRE(fs::create_directory(source / L"valve"));
    REQUIRE(write_text(source / L"hl.exe", "unsigned-client"));
    REQUIRE(write_text(source / L"hlds.exe", "unsigned-server"));
    REQUIRE(write_text(source / L"valve" / L"asset.bin", "asset"));
    const auto manifest = steamapps / L"appmanifest_70.acf";
    REQUIRE(write_text(
        manifest,
        "\"AppState\"\n{\n\"appid\" \"70\"\n"
        "\"buildid\" \"15961492\"\n}\n"));

    const auto before_client_size = fs::file_size(source / L"hl.exe");
    const auto before_server_size = fs::file_size(source / L"hlds.exe");
    const auto result = windows::validate_stock_runtime_candidate_source(
        source, manifest);
    INFO("status=" << windows::to_string(result.status));
    INFO("topology-error=" << windows::to_string(result.topology_error));
    INFO("external-diagnostic-error="
         << windows::to_string(result.external_diagnostic_error));
    INFO("native-error=" << result.native_error);
    REQUIRE(result);
    CHECK_FALSE(result.summary->research_copy_eligible);
    CHECK(result.summary->topology_safe);
    CHECK(result.summary->client_profile !=
          windows::StockSourceComponentProfileStatus::valid);
    CHECK(result.status ==
          windows::StockSourceEligibilityStatus::client_profile_invalid);
    CHECK(fs::file_size(source / L"hl.exe") == before_client_size);
    CHECK(fs::file_size(source / L"hlds.exe") == before_server_size);
    CHECK_FALSE(fs::exists(source / L"research-copy"));
    CHECK_FALSE(fs::exists(source / L".hlclient-research-isolated"));

    const auto unrelated_manifest =
        temporary.path() / L"other-steamapps" / L"appmanifest_70.acf";
    REQUIRE(fs::create_directory(unrelated_manifest.parent_path()));
    REQUIRE(write_text(
        unrelated_manifest,
        "\"AppState\"\n{\n\"appid\" \"70\"\n"
        "\"buildid\" \"15961492\"\n}\n"));
    const auto unrelated = windows::validate_stock_runtime_candidate_source(
        source, unrelated_manifest);
    CHECK_FALSE(unrelated);
    CHECK(unrelated.status ==
          windows::StockSourceEligibilityStatus::app_profile_invalid);
    CHECK(unrelated.app_manifest_error ==
          windows::SteamAppManifestErrorCode::unsafe_path);
    const auto unc_manifest =
        fs::path{L"\\\\hlclient-invalid.invalid\\share\\appmanifest_70.acf"};
    const auto remote_manifest =
        windows::validate_stock_runtime_candidate_source(
            source, unc_manifest);
    CHECK_FALSE(remote_manifest);
    CHECK(remote_manifest.status ==
          windows::StockSourceEligibilityStatus::app_profile_invalid);
    CHECK(remote_manifest.app_manifest_error ==
          windows::SteamAppManifestErrorCode::unsafe_path);

    const auto streamed_asset =
        (source / L"valve" / L"asset.bin").wstring() + L":candidate-test";
    const HANDLE stream = ::CreateFileW(
        streamed_asset.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stream != INVALID_HANDLE_VALUE) {
        const std::byte marker_byte{0x2a};
        DWORD written = 0U;
        REQUIRE(::WriteFile(
                    stream, &marker_byte, 1U, &written, nullptr) != FALSE);
        REQUIRE(written == 1U);
        REQUIRE(::CloseHandle(stream) != FALSE);
        const auto ads = windows::validate_stock_runtime_candidate_source(
            source, manifest);
        REQUIRE(ads);
        CHECK_FALSE(ads.summary->research_copy_eligible);
        CHECK(ads.summary->alternate_data_stream_count == 1U);
        CHECK(ads.status == windows::StockSourceEligibilityStatus::
                                alternate_data_stream);
        REQUIRE(::DeleteFileW(streamed_asset.c_str()) != FALSE);
    } else {
        WARN("Alternate-data-stream capability unavailable");
    }

    REQUIRE(write_text(
        source / L".hlclient-research-isolated",
        "HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1"));
    const auto prepared = windows::validate_stock_runtime_candidate_source(
        source, manifest);
    REQUIRE(prepared);
    CHECK_FALSE(prepared.summary->research_copy_eligible);
    CHECK(prepared.status ==
          windows::StockSourceEligibilityStatus::source_already_prepared);
}

TEST_CASE(
    "Stock candidate source rejects UNC and device roots before diagnostics",
    "[windows][stock-runtime][source-eligibility][root-gate]")
{
    const auto manifest = fs::path{L"C:\\steamapps\\appmanifest_70.acf"};
    for (const auto& unsafe_source : {
             fs::path{L"\\\\hlclient-invalid.invalid\\share\\steamapps\\common\\Half-Life"},
             fs::path{L"\\\\?\\C:\\steamapps\\common\\Half-Life"}}) {
        const auto result = windows::validate_stock_runtime_candidate_source(
            unsafe_source, manifest);
        CHECK_FALSE(result);
        CHECK(result.status ==
              windows::StockSourceEligibilityStatus::topology_unsafe);
        CHECK_FALSE(result.summary.has_value());
    }
}

TEST_CASE(
    "Stock candidate source validator rejects invalid API arguments",
    "[windows][stock-runtime][source-eligibility][arguments]")
{
    CHECK_FALSE(windows::validate_stock_runtime_candidate_source(
        {}, fs::path{L"C:\\appmanifest_70.acf"}));

    windows::StockSourceEligibilityOptions options;
    options.expected_app_build = 1U;
    const auto unsupported_build =
        windows::validate_stock_runtime_candidate_source(
            fs::path{L"C:\\Half-Life"},
            fs::path{L"C:\\appmanifest_70.acf"}, options);
    CHECK_FALSE(unsupported_build);
    CHECK(unsupported_build.status ==
          windows::StockSourceEligibilityStatus::invalid_argument);
}
