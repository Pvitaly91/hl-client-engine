#include <hlclient/platform/windows/stock_source_eligibility.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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

namespace hlclient::platform::windows {
namespace {

constexpr WindowsFileVersion kExpectedClientVersion{1U, 1U, 1U, 1U};
constexpr WindowsFileVersion kExpectedServerVersion{4U, 1U, 1U, 1U};
constexpr std::wstring_view kPreparedMarker =
    L".hlclient-research-isolated";

[[nodiscard]] bool has_category(
    const StockResearchTopologySummary& summary,
    const StockResearchTopologyCategory category) noexcept
{
    return std::ranges::find(summary.categories, category) !=
           summary.categories.end();
}

[[nodiscard]] bool summary_equal(
    const StockResearchTopologySummary& left,
    const StockResearchTopologySummary& right) noexcept
{
    return left.categories == right.categories &&
           left.inspection_complete == right.inspection_complete &&
           left.safe_to_materialize == right.safe_to_materialize &&
           left.root_reparse == right.root_reparse &&
           left.internal_reparse_count == right.internal_reparse_count &&
           left.hardlink_count == right.hardlink_count &&
           left.alternate_data_stream_count ==
               right.alternate_data_stream_count &&
           left.contained_target_count == right.contained_target_count &&
           left.escaped_target_count == right.escaped_target_count &&
           left.reviewed_external_target_count ==
               right.reviewed_external_target_count &&
           left.entry_count == right.entry_count &&
           left.byte_count == right.byte_count;
}

[[nodiscard]] bool topology_uses_exact_local_root(
    const StockResearchTopologySummary& summary) noexcept
{
    return !summary.root_reparse &&
           !has_category(
               summary,
               StockResearchTopologyCategory::source_path_ancestor_reparse) &&
           !has_category(
               summary,
               StockResearchTopologyCategory::source_root_reparse) &&
           !has_category(
               summary,
               StockResearchTopologyCategory::source_subst_drive) &&
           !has_category(
               summary, StockResearchTopologyCategory::source_unc_path) &&
           !has_category(
               summary, StockResearchTopologyCategory::source_remote_volume);
}

[[nodiscard]] std::wstring normalized_path_key(
    const std::filesystem::path& path)
{
    auto value = path.lexically_normal().native();
    std::ranges::replace(value, L'/', L'\\');
    while (value.size() > 3U && value.back() == L'\\') {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] bool ordinal_path_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    const std::size_t maximum_characters)
{
    const auto left_key = normalized_path_key(left);
    const auto right_key = normalized_path_key(right);
    if (maximum_characters == 0U ||
        left_key.size() > maximum_characters ||
        right_key.size() > maximum_characters) {
        return false;
    }
    return ::CompareStringOrdinal(
               left_key.data(), static_cast<int>(left_key.size()),
               right_key.data(), static_cast<int>(right_key.size()), TRUE) ==
           CSTR_EQUAL;
}

[[nodiscard]] bool ordinal_component_equal(
    const std::filesystem::path& value,
    const std::wstring_view expected)
{
    const auto native = value.native();
    if (native.size() != expected.size()) {
        return false;
    }
    return ::CompareStringOrdinal(
               native.data(), static_cast<int>(native.size()),
               expected.data(), static_cast<int>(expected.size()), TRUE) ==
           CSTR_EQUAL;
}

// This is deliberately lexical and is evaluated only after the source root's
// no-alias local fixed-volume proof.  It prevents the candidate gate from
// opening a UNC/device/unrelated manifest while leaving physical identity and
// reparse/ADS/single-link validation to observe_steam_app_manifest_70().
[[nodiscard]] bool app_manifest_belongs_to_source_installation(
    const std::filesystem::path& source_root,
    const std::filesystem::path& app_manifest,
    const std::size_t maximum_path_characters)
{
    const auto normalized_source = source_root.lexically_normal();
    if (!ordinal_component_equal(
            normalized_source.filename(), L"Half-Life") ||
        !ordinal_component_equal(
            normalized_source.parent_path().filename(), L"common") ||
        !ordinal_component_equal(
            normalized_source.parent_path().parent_path().filename(),
            L"steamapps")) {
        return false;
    }
    const auto expected_manifest =
        normalized_source.parent_path().parent_path() /
        L"appmanifest_70.acf";
    return ordinal_path_equal(
        expected_manifest, app_manifest, maximum_path_characters);
}

[[nodiscard]] bool is_dangling_target(
    const StockExternalTargetReview& target) noexcept
{
    if (!target.reparse_observation) return false;
    switch (target.reparse_observation->diagnostic_classification) {
    case WindowsReparseDiagnosticClassification::dangling_directory_junction:
    case WindowsReparseDiagnosticClassification::dangling_directory_symlink:
        return true;
    // A missing volume is a more specific reachability diagnosis, but it is
    // also an unresolved/dangling target for the candidate gate. The public
    // surface has one dangling counter and must not silently report zero.
    case WindowsReparseDiagnosticClassification::missing_volume_mount:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_unsupported_target(
    const StockExternalTargetReview& target) noexcept
{
    if (!target.reparse_observation) return false;
    switch (target.reparse_observation->diagnostic_classification) {
    case WindowsReparseDiagnosticClassification::
        unsupported_tag_without_path_contract:
    case WindowsReparseDiagnosticClassification::malformed_reparse_payload:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] StockSourceComponentProfileStatus binary_profile_status(
    const WindowsBinaryIdentityResult& result,
    const WindowsFileVersion expected) noexcept
{
    if (!result) {
        switch (result.code) {
        case WindowsBinaryIdentityErrorCode::open_failed:
        case WindowsBinaryIdentityErrorCode::not_regular_file:
            return StockSourceComponentProfileStatus::missing;
        case WindowsBinaryIdentityErrorCode::authenticode_invalid:
            return StockSourceComponentProfileStatus::signature_invalid;
        default:
            return StockSourceComponentProfileStatus::identity_invalid;
        }
    }
    if (result.identity->file_version != expected) {
        return StockSourceComponentProfileStatus::version_mismatch;
    }
    if (result.identity->pe_machine != WindowsPeMachine::x86) {
        return StockSourceComponentProfileStatus::machine_mismatch;
    }
    if (!result.identity->authenticode_valid) {
        return StockSourceComponentProfileStatus::signature_invalid;
    }
    return StockSourceComponentProfileStatus::valid;
}

struct MarkerObservation final {
    std::optional<bool> present;
    std::uint32_t native_error{ERROR_SUCCESS};
};

[[nodiscard]] MarkerObservation observe_prepared_marker(
    const std::filesystem::path& source_root) noexcept
{
    try {
        const auto marker = source_root / kPreparedMarker;
        const HANDLE handle = ::CreateFileW(
            marker.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(handle));
            return {true, ERROR_SUCCESS};
        }
        const DWORD error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return {false, ERROR_SUCCESS};
        }
        return {std::nullopt, error};
    } catch (...) {
        return {std::nullopt, ERROR_NOT_ENOUGH_MEMORY};
    }
}

[[nodiscard]] StockSourceEligibilityResult hard_failure(
    const StockSourceEligibilityStatus status,
    const StockResearchCopyErrorCode topology_error =
        StockResearchCopyErrorCode::none,
    const std::uint32_t native_error = ERROR_SUCCESS,
    const StockExternalReviewErrorCode external_diagnostic_error =
        StockExternalReviewErrorCode::none) noexcept
{
    StockSourceEligibilityResult result;
    result.status = status;
    result.topology_error = topology_error;
    result.external_diagnostic_error = external_diagnostic_error;
    result.native_error = native_error;
    return result;
}

} // namespace

std::string_view to_string(
    const StockSourceComponentProfileStatus status) noexcept
{
    switch (status) {
    case StockSourceComponentProfileStatus::not_observed:
        return "not_observed";
    case StockSourceComponentProfileStatus::valid: return "valid";
    case StockSourceComponentProfileStatus::missing: return "missing";
    case StockSourceComponentProfileStatus::identity_invalid:
        return "identity_invalid";
    case StockSourceComponentProfileStatus::version_mismatch:
        return "version_mismatch";
    case StockSourceComponentProfileStatus::machine_mismatch:
        return "machine_mismatch";
    case StockSourceComponentProfileStatus::signature_invalid:
        return "signature_invalid";
    case StockSourceComponentProfileStatus::app_id_mismatch:
        return "app_id_mismatch";
    case StockSourceComponentProfileStatus::build_id_mismatch:
        return "build_id_mismatch";
    }
    return "not_observed";
}

std::string_view to_string(const StockSourceEligibilityStatus status) noexcept
{
    switch (status) {
    case StockSourceEligibilityStatus::success: return "success";
    case StockSourceEligibilityStatus::invalid_argument:
        return "invalid_argument";
    case StockSourceEligibilityStatus::topology_observation_failed:
        return "topology_observation_failed";
    case StockSourceEligibilityStatus::topology_incomplete:
        return "topology_incomplete";
    case StockSourceEligibilityStatus::topology_unsafe:
        return "topology_unsafe";
    case StockSourceEligibilityStatus::escaped_target:
        return "escaped_target";
    case StockSourceEligibilityStatus::dangling_target:
        return "dangling_target";
    case StockSourceEligibilityStatus::unsupported_reparse_tag:
        return "unsupported_reparse_tag";
    case StockSourceEligibilityStatus::alternate_data_stream:
        return "alternate_data_stream";
    case StockSourceEligibilityStatus::client_profile_invalid:
        return "client_profile_invalid";
    case StockSourceEligibilityStatus::server_profile_invalid:
        return "server_profile_invalid";
    case StockSourceEligibilityStatus::app_profile_invalid:
        return "app_profile_invalid";
    case StockSourceEligibilityStatus::source_already_prepared:
        return "source_already_prepared";
    case StockSourceEligibilityStatus::source_changed:
        return "source_changed";
    }
    return "topology_observation_failed";
}

StockSourceEligibilitySummary assess_stock_source_eligibility(
    const StockSourceEligibilityAssessmentInput& input) noexcept
{
    StockSourceEligibilitySummary result;
    result.escaped_target_count = input.topology.escaped_target_count;
    result.dangling_target_count = input.dangling_target_count;
    result.unsupported_tag_count = input.unsupported_tag_count;
    result.alternate_data_stream_count =
        input.topology.alternate_data_stream_count;
    result.inventory_entry_count = input.topology.entry_count;
    result.inventory_byte_count = input.topology.byte_count;
    result.client_profile = input.client_profile;
    result.server_profile = input.server_profile;
    result.app_profile = input.app_profile;
    result.source_already_prepared = input.source_already_prepared;

    if (!input.topology.inspection_complete ||
        !input.reparse_diagnostics_complete) {
        result.status = StockSourceEligibilityStatus::topology_incomplete;
    } else if (input.dangling_target_count != 0U) {
        result.status = StockSourceEligibilityStatus::dangling_target;
    } else if (input.unsupported_tag_count != 0U) {
        result.status =
            StockSourceEligibilityStatus::unsupported_reparse_tag;
    } else if (!input.exact_root) {
        result.status = StockSourceEligibilityStatus::topology_unsafe;
    } else if (input.topology.escaped_target_count != 0U) {
        result.status = StockSourceEligibilityStatus::escaped_target;
    } else if (input.topology.alternate_data_stream_count != 0U) {
        result.status = StockSourceEligibilityStatus::alternate_data_stream;
    } else if (!input.topology.safe_to_materialize) {
        result.status = StockSourceEligibilityStatus::topology_unsafe;
    } else if (input.source_already_prepared) {
        result.status = StockSourceEligibilityStatus::source_already_prepared;
    } else if (input.client_profile !=
               StockSourceComponentProfileStatus::valid) {
        result.status = StockSourceEligibilityStatus::client_profile_invalid;
    } else if (input.server_profile !=
               StockSourceComponentProfileStatus::valid) {
        result.status = StockSourceEligibilityStatus::server_profile_invalid;
    } else if (input.app_profile != StockSourceComponentProfileStatus::valid) {
        result.status = StockSourceEligibilityStatus::app_profile_invalid;
    } else {
        result.status = StockSourceEligibilityStatus::success;
        result.research_copy_eligible = true;
    }
    result.topology_safe =
        input.topology.inspection_complete &&
        input.reparse_diagnostics_complete && input.exact_root &&
        input.dangling_target_count == 0U &&
        input.unsupported_tag_count == 0U &&
        input.topology.escaped_target_count == 0U &&
        input.topology.alternate_data_stream_count == 0U &&
        input.topology.safe_to_materialize;
    return result;
}

StockSourceEligibilityResult validate_stock_runtime_candidate_source(
    const std::filesystem::path& source_half_life_root,
    const std::filesystem::path& app_manifest_path,
    const StockSourceEligibilityOptions& options) noexcept
{
    try {
        if (source_half_life_root.empty() ||
            !source_half_life_root.is_absolute() ||
            app_manifest_path.empty() || !app_manifest_path.is_absolute() ||
            options.expected_app_build != kDefaultExpectedStockSteamBuild ||
            options.inventory_limits.maximum_entries == 0U ||
            options.inventory_limits.maximum_total_bytes == 0U ||
            options.inventory_limits.maximum_file_bytes == 0U ||
            options.inventory_limits.maximum_reparse_depth == 0U ||
            options.inventory_limits.maximum_path_characters == 0U ||
            options.inventory_limits.maximum_streams_per_file == 0U) {
            return hard_failure(StockSourceEligibilityStatus::invalid_argument);
        }

        const auto topology_before = inspect_stock_research_topology(
            source_half_life_root, options.inventory_limits);
        if (!topology_before) {
            return hard_failure(
                StockSourceEligibilityStatus::topology_observation_failed,
                topology_before.code, topology_before.native_error);
        }
        const bool exact_root =
            topology_uses_exact_local_root(*topology_before.summary);
        if (!exact_root) {
            return hard_failure(
                StockSourceEligibilityStatus::topology_unsafe,
                topology_before.code, topology_before.native_error);
        }
        if (!app_manifest_belongs_to_source_installation(
                source_half_life_root, app_manifest_path,
                options.inventory_limits.maximum_path_characters)) {
            auto failure = hard_failure(
                StockSourceEligibilityStatus::app_profile_invalid);
            failure.app_manifest_error =
                SteamAppManifestErrorCode::unsafe_path;
            return failure;
        }
        // This API is deliberately no-publication: it performs the exact same
        // bounded, path-private scan used by V2 review without creating a
        // review directory or artifact.
        const auto diagnostic_before = diagnose_stock_external_targets(
            source_half_life_root, options.inventory_limits);
        if (!diagnostic_before) {
            return hard_failure(
                StockSourceEligibilityStatus::topology_observation_failed,
                topology_before.code, diagnostic_before.native_error,
                diagnostic_before.code);
        }
        if (!diagnostic_before.value->source_inventory_complete ||
            !diagnostic_before.value->all_targets_diagnostic_complete) {
            return hard_failure(
                StockSourceEligibilityStatus::topology_incomplete,
                topology_before.code, ERROR_INVALID_DATA,
                StockExternalReviewErrorCode::topology_read_failed);
        }

        StockSourceEligibilityAssessmentInput input;
        input.topology = *topology_before.summary;
        input.exact_root = exact_root &&
                           !diagnostic_before.value->root_reparse;
        input.reparse_diagnostics_complete =
            diagnostic_before.value->source_inventory_complete &&
            diagnostic_before.value->all_targets_diagnostic_complete;
        input.dangling_target_count = static_cast<std::size_t>(
            std::ranges::count_if(
                diagnostic_before.value->targets, is_dangling_target));
        input.unsupported_tag_count = static_cast<std::size_t>(
            std::ranges::count_if(
                diagnostic_before.value->targets, is_unsupported_target));

        if (input.exact_root) {
            const auto marker = observe_prepared_marker(source_half_life_root);
            if (!marker.present) {
                return hard_failure(
                    StockSourceEligibilityStatus::source_changed,
                    StockResearchCopyErrorCode::none, marker.native_error);
            }
            input.source_already_prepared = *marker.present;
        }

        WindowsBinaryIdentityResult client;
        WindowsBinaryIdentityResult server;
        SteamAppManifestResult app_manifest;
        if (input.topology.inspection_complete && input.exact_root &&
            input.topology.safe_to_materialize &&
            input.topology.escaped_target_count == 0U &&
            input.unsupported_tag_count == 0U &&
            input.topology.alternate_data_stream_count == 0U) {
            client = observe_windows_binary_identity(
                source_half_life_root / L"hl.exe");
            server = observe_windows_binary_identity(
                source_half_life_root / L"hlds.exe");
            app_manifest = observe_steam_app_manifest_70(app_manifest_path);
            input.client_profile =
                binary_profile_status(client, kExpectedClientVersion);
            input.server_profile =
                binary_profile_status(server, kExpectedServerVersion);
            if (!app_manifest) {
                switch (app_manifest.code) {
                case SteamAppManifestErrorCode::unexpected_app_id:
                    input.app_profile =
                        StockSourceComponentProfileStatus::app_id_mismatch;
                    break;
                case SteamAppManifestErrorCode::unexpected_build_id:
                    input.app_profile =
                        StockSourceComponentProfileStatus::build_id_mismatch;
                    break;
                default:
                    input.app_profile =
                        StockSourceComponentProfileStatus::identity_invalid;
                    break;
                }
            } else {
                input.app_profile =
                    app_manifest.observation->app_id == 70U &&
                            app_manifest.observation->build_id ==
                                options.expected_app_build
                        ? StockSourceComponentProfileStatus::valid
                        : StockSourceComponentProfileStatus::build_id_mismatch;
            }
        }

        if (topology_before &&
            input.client_profile == StockSourceComponentProfileStatus::valid &&
            input.server_profile == StockSourceComponentProfileStatus::valid &&
            input.app_profile == StockSourceComponentProfileStatus::valid &&
            !input.source_already_prepared) {
            const auto topology_after = inspect_stock_research_topology(
                source_half_life_root, options.inventory_limits);
            const auto diagnostic_after = diagnose_stock_external_targets(
                source_half_life_root, options.inventory_limits);
            if (!topology_after || !diagnostic_after ||
                !summary_equal(
                    *topology_before.summary, *topology_after.summary) ||
                diagnostic_before.value->source_identity_sha256 !=
                    diagnostic_after.value->source_identity_sha256 ||
                diagnostic_before.value->source_inventory_sha256 !=
                    diagnostic_after.value->source_inventory_sha256 ||
                diagnostic_before.value->targets.size() !=
                    diagnostic_after.value->targets.size() ||
                diagnostic_before.value->alternate_data_stream_count !=
                    diagnostic_after.value->alternate_data_stream_count) {
                return hard_failure(
                    StockSourceEligibilityStatus::source_changed,
                    topology_after.code,
                    diagnostic_after
                        ? ERROR_FILE_INVALID
                        : diagnostic_after.native_error,
                    diagnostic_after.code);
            }
        }

        auto summary = assess_stock_source_eligibility(input);
        StockSourceEligibilityResult result;
        result.status = summary.status;
        result.summary = std::move(summary);
        result.client_binary_error = client.code;
        result.server_binary_error = server.code;
        result.app_manifest_error = app_manifest.code;
        return result;
    } catch (...) {
        return hard_failure(
            StockSourceEligibilityStatus::topology_observation_failed,
            StockResearchCopyErrorCode::enumeration_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    }
}

} // namespace hlclient::platform::windows
