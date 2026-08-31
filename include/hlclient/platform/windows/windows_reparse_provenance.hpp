#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::platform::windows {

class WindowsReparsePinnedTarget;

inline constexpr std::size_t kWindowsReparseDefaultMaximumPayloadBytes =
    16'384U;
inline constexpr std::size_t kWindowsReparseHardMaximumPayloadBytes =
    16'384U;
inline constexpr std::size_t
    kWindowsReparseDefaultMaximumTargetExpressionCharacters = 16'384U;
inline constexpr std::size_t
    kWindowsReparseHardMaximumTargetExpressionCharacters = 32'767U;
inline constexpr std::size_t kWindowsReparseDefaultMaximumFailureWitnesses =
    128U;
inline constexpr std::size_t kWindowsReparseHardMaximumFailureWitnesses =
    4'096U;
inline constexpr std::size_t kWindowsReparseDefaultMaximumNestedDepth = 32U;
inline constexpr std::size_t kWindowsReparseHardMaximumNestedDepth = 128U;
inline constexpr std::size_t kWindowsReparseDefaultMaximumDiagnosticTargets =
    256U;
inline constexpr std::size_t kWindowsReparseHardMaximumDiagnosticTargets =
    4'096U;

struct WindowsReparseProvenanceLimits final {
    std::size_t maximum_reparse_payload_bytes{
        kWindowsReparseDefaultMaximumPayloadBytes};
    std::size_t maximum_target_expression_characters{
        kWindowsReparseDefaultMaximumTargetExpressionCharacters};
    std::size_t maximum_failure_witnesses{
        kWindowsReparseDefaultMaximumFailureWitnesses};
    // Maximum additional documented name-surrogate hops followed after the
    // source link itself. Ordinary lexical path components do not consume it.
    std::size_t maximum_nested_reparse_depth{
        kWindowsReparseDefaultMaximumNestedDepth};
    std::size_t maximum_diagnostic_targets{
        kWindowsReparseDefaultMaximumDiagnosticTargets};
};

[[nodiscard]] bool valid_windows_reparse_provenance_limits(
    const WindowsReparseProvenanceLimits& limits) noexcept;

enum class WindowsReparseTagCategory {
    none,
    mount_point,
    symbolic_link,
    app_exec_link,
    cloud_placeholder,
    cloud_placeholder_variant,
    wci,
    wci_tombstone,
    wof,
    dedup,
    hsm,
    dfs,
    sis,
    projected_file_system,
    microsoft_name_surrogate_other,
    microsoft_non_name_surrogate_other,
    third_party_name_surrogate,
    third_party_other,
    malformed_or_unreadable,
};

[[nodiscard]] std::string_view to_string(
    WindowsReparseTagCategory value) noexcept;

enum class WindowsReparseTargetExpressionKind {
    none,
    relative_path,
    drive_absolute_path,
    volume_guid_path,
    nt_object_manager_path,
    unc_path,
    device_path,
    app_execution_alias,
    opaque_non_path_payload,
    malformed,
};

[[nodiscard]] std::string_view to_string(
    WindowsReparseTargetExpressionKind value) noexcept;

enum class WindowsReparseTargetReachability {
    reachable,
    target_path_not_found,
    target_component_not_found,
    target_volume_not_found,
    target_access_denied,
    target_not_directory,
    target_remote_or_device,
    target_cycle,
    target_depth_exceeded,
    target_changed,
    target_open_failed_other,
    not_applicable,
};

[[nodiscard]] std::string_view to_string(
    WindowsReparseTargetReachability value) noexcept;

enum class WindowsReparseNativeErrorCategory {
    none,
    file_not_found,
    path_not_found,
    invalid_name,
    bad_pathname,
    bad_network_path,
    bad_network_name,
    volume_not_ready,
    device_not_exist,
    device_not_connected,
    access_denied,
    cannot_access_file,
    not_directory,
    reparse_tag_invalid,
    reparse_tag_mismatch,
    reparse_point_encountered,
    circular_dependency,
    too_many_links,
    other,
};

[[nodiscard]] std::string_view to_string(
    WindowsReparseNativeErrorCategory value) noexcept;

[[nodiscard]] WindowsReparseNativeErrorCategory
classify_windows_reparse_native_error(std::uint32_t native_error) noexcept;

[[nodiscard]] WindowsReparseTargetReachability
classify_windows_reparse_target_reachability(
    std::uint32_t native_error) noexcept;

enum class WindowsReparsePayloadStatus {
    path_contract_decoded,
    opaque_non_path_payload,
    malformed,
};

[[nodiscard]] std::string_view to_string(
    WindowsReparsePayloadStatus value) noexcept;

enum class WindowsReparsePayloadErrorCode {
    none,
    truncated_header,
    invalid_data_length,
    payload_limit_exceeded,
    truncated_path_header,
    truncated_guid_header,
    odd_utf16_offset_or_length,
    path_range_overflow,
    path_ranges_overlap,
    invalid_symbolic_link_flags,
    target_expression_limit_exceeded,
    target_expression_malformed,
};

[[nodiscard]] std::string_view to_string(
    WindowsReparsePayloadErrorCode value) noexcept;

enum class WindowsReparseDiagnosticClassification {
    none,
    reachable_name_surrogate,
    dangling_directory_junction,
    dangling_directory_symlink,
    missing_volume_mount,
    inaccessible_target,
    remote_or_device_target,
    cyclic_target,
    target_depth_exceeded,
    changed_during_observation,
    unsupported_tag_without_path_contract,
    malformed_reparse_payload,
    target_open_failed_other,
};

[[nodiscard]] std::string_view to_string(
    WindowsReparseDiagnosticClassification value) noexcept;

enum class StockExternalTopologyFailurePhase {
    source_link_open,
    reparse_payload_read,
    reparse_payload_decode,
    target_expression_parse,
    target_open,
    target_identity,
    target_inventory,
    nested_entry_open,
    nested_reparse_decode,
    post_inventory_revalidation,
};

[[nodiscard]] std::string_view to_string(
    StockExternalTopologyFailurePhase value) noexcept;

struct WindowsReparseTagProperties final {
    std::uint32_t raw_tag{0U};
    bool microsoft{false};
    bool name_surrogate{false};
    bool directory{false};
    WindowsReparseTagCategory category{WindowsReparseTagCategory::none};
    std::size_t payload_byte_count{0U};
};

[[nodiscard]] WindowsReparseTagCategory classify_windows_reparse_tag(
    std::uint32_t raw_tag) noexcept;

struct WindowsReparseTargetExpression final {
    WindowsReparseTargetExpressionKind kind{
        WindowsReparseTargetExpressionKind::none};
    std::wstring private_expression;
    std::wstring private_normalized_expression;
    bool relative{false};
};

[[nodiscard]] WindowsReparseTargetExpression
classify_windows_reparse_target_expression(
    std::wstring_view expression,
    bool relative,
    std::size_t maximum_characters =
        kWindowsReparseDefaultMaximumTargetExpressionCharacters) noexcept;

struct WindowsReparseProvenance final {
    WindowsReparseTagProperties tag;
    WindowsReparsePayloadStatus payload_status{
        WindowsReparsePayloadStatus::malformed};
    WindowsReparsePayloadErrorCode payload_error{
        WindowsReparsePayloadErrorCode::none};

    // These fields are private evidence. Callers must never include them in a
    // public summary or command-line diagnostic.
    std::string private_payload_sha256;
    std::wstring private_substitute_name;
    std::wstring private_print_name;
    WindowsReparseTargetExpression target_expression;

    std::uint32_t symbolic_link_flags{0U};
    bool symbolic_link_relative{false};
};

struct WindowsReparseTargetIdentity final {
    std::uint64_t volume_serial{0U};
    std::array<std::byte, 16U> file_id{};

    // A handle-derived path retained only for private identity binding. It is
    // not a public diagnostic string and lexical equality is not identity.
    std::filesystem::path private_final_handle_path;
    bool directory{false};
};

// Private resolver-chain context for a failure encountered after the outer
// source link has redirected traversal through another name surrogate.  The
// path exists only so the review boundary can bind its private witness digest;
// it must never be emitted by a public diagnostic or artifact.
struct WindowsReparseNestedFailure final {
    std::size_t traversal_depth{0U};
    std::size_t nested_ordinal{0U};
    WindowsReparseTagCategory reparse_tag_category{
        WindowsReparseTagCategory::none};
    WindowsReparseTargetExpressionKind expression_kind{
        WindowsReparseTargetExpressionKind::none};
    WindowsReparseTargetReachability reachability{
        WindowsReparseTargetReachability::not_applicable};
    StockExternalTopologyFailurePhase failure_phase{
        StockExternalTopologyFailurePhase::nested_entry_open};
    bool directory{false};
    WindowsReparseNativeErrorCategory native_error_category{
        WindowsReparseNativeErrorCategory::none};
    std::uint32_t native_error{0U};
    std::filesystem::path private_link_path;
};

// Converts retained nested-chain metadata into the same path-free diagnostic
// classification published for the outer target. Unsupported tag contracts,
// including AppExecLink aliases, remain opaque and are never followed.
[[nodiscard]] WindowsReparseDiagnosticClassification
classify_windows_reparse_nested_failure_diagnostic(
    const WindowsReparseNestedFailure& failure) noexcept;

enum class WindowsReparseProvenanceErrorCode {
    none,
    invalid_argument,
    invalid_limits,
    source_link_open_failed,
    source_not_reparse_point,
    reparse_payload_read_failed,
    reparse_payload_limit_exceeded,
    target_identity_failed,
    source_link_changed,
};

[[nodiscard]] std::string_view to_string(
    WindowsReparseProvenanceErrorCode value) noexcept;

template <typename T>
struct WindowsReparseProvenanceResult final {
    std::optional<T> value;
    WindowsReparseProvenanceErrorCode code{
        WindowsReparseProvenanceErrorCode::none};
    std::uint32_t native_error{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value() &&
               code == WindowsReparseProvenanceErrorCode::none;
    }
};

struct WindowsReparseTargetObservation final {
    WindowsReparseProvenance provenance;
    WindowsReparseTargetReachability reachability{
        WindowsReparseTargetReachability::not_applicable};
    WindowsReparseNativeErrorCategory native_error_category{
        WindowsReparseNativeErrorCategory::none};
    std::uint32_t native_error{0U};
    std::optional<WindowsReparseTargetIdentity> target_identity;
    std::optional<WindowsReparseNestedFailure> nested_failure;

    // Private shared ownership of the exact second, root-relative no-follow
    // target chain used to establish target_identity. Keeping this pin alive
    // prevents accepted path components from being renamed or replaced before
    // a caller has completed its handle-rooted inspection. The pointee is
    // intentionally opaque outside this implementation boundary.
    std::shared_ptr<const WindowsReparsePinnedTarget> private_pinned_target;
    WindowsReparseDiagnosticClassification diagnostic_classification{
        WindowsReparseDiagnosticClassification::none};
    StockExternalTopologyFailurePhase failure_phase{
        StockExternalTopologyFailurePhase::source_link_open};
    bool observation_complete{false};
};

// Returns the final native HANDLE owned by private_pinned_target, or nullptr
// when the observation is not reachable. The handle is non-owning: callers
// must retain the observation (or its private pin) for the entire use and must
// never close the returned value.
[[nodiscard]] void* windows_reparse_target_native_final_handle(
    const WindowsReparseTargetObservation& observation) noexcept;

struct StockExternalTopologyFailureWitness final {
    std::size_t target_ordinal{0U};
    std::size_t traversal_depth{0U};
    WindowsReparseTagCategory reparse_tag_category{
        WindowsReparseTagCategory::none};
    WindowsReparseTargetExpressionKind expression_kind{
        WindowsReparseTargetExpressionKind::none};
    WindowsReparseTargetReachability reachability{
        WindowsReparseTargetReachability::not_applicable};
    StockExternalTopologyFailurePhase failure_phase{
        StockExternalTopologyFailurePhase::source_link_open};
    bool directory{false};
    std::size_t nested_ordinal{0U};
    WindowsReparseNativeErrorCategory native_error_category{
        WindowsReparseNativeErrorCategory::none};
    std::string private_witness_sha256;
};

// Decodes an exact byte sequence returned by FSCTL_GET_REPARSE_POINT. The
// input includes the 8-byte common header. Malformed payloads return a value
// with payload_status=malformed so a caller can publish a complete, path-free
// ineligibility diagnostic.
[[nodiscard]] WindowsReparseProvenanceResult<WindowsReparseProvenance>
decode_windows_reparse_payload(
    std::span<const std::byte> bytes,
    bool directory,
    const WindowsReparseProvenanceLimits& limits = {}) noexcept;

// Opens the entry without following it and obtains an exact bounded payload.
[[nodiscard]] WindowsReparseProvenanceResult<WindowsReparseProvenance>
read_windows_reparse_provenance(
    const std::filesystem::path& source_link,
    const WindowsReparseProvenanceLimits& limits = {}) noexcept;

// Resolves only the documented mount-point and symbolic-link path contracts.
// The followed handle, not the target string, establishes target identity.
// Opaque, remote and device expressions are never followed.
[[nodiscard]] WindowsReparseProvenanceResult<WindowsReparseTargetObservation>
observe_windows_reparse_target(
    const std::filesystem::path& source_link,
    const WindowsReparseProvenanceLimits& limits = {}) noexcept;

} // namespace hlclient::platform::windows
