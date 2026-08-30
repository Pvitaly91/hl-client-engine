#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::platform::windows {

inline constexpr std::uint64_t kMaximumObservedExecutableBytes =
    512U * 1'024U * 1'024U;
inline constexpr std::uint64_t kMaximumSteamAppManifestBytes = 1U * 1'024U * 1'024U;

struct WindowsFileIdentity final {
    std::uint64_t volume_serial_number{0U};
    std::array<std::byte, 16U> file_id{};

    friend bool operator==(const WindowsFileIdentity&,
                           const WindowsFileIdentity&) noexcept = default;
};

struct WindowsFileSnapshot final {
    WindowsFileIdentity identity{};
    std::uint64_t size{0U};
    std::int64_t creation_time{0};
    std::int64_t last_write_time{0};
    std::int64_t change_time{0};
    std::uint32_t attributes{0U};

    friend bool operator==(const WindowsFileSnapshot&,
                           const WindowsFileSnapshot&) noexcept = default;
};

struct WindowsFileVersion final {
    std::uint16_t major{0U};
    std::uint16_t minor{0U};
    std::uint16_t patch{0U};
    std::uint16_t build{0U};

    friend bool operator==(const WindowsFileVersion&,
                           const WindowsFileVersion&) noexcept = default;
};

[[nodiscard]] std::string to_string(const WindowsFileVersion& version);

enum class WindowsPeMachine {
    unknown,
    x86,
    x64,
    arm64,
};

[[nodiscard]] std::string_view to_string(WindowsPeMachine machine) noexcept;

enum class WindowsBinaryIdentityErrorCode {
    none,
    empty_path,
    path_not_absolute,
    alternate_data_stream,
    reparse_point,
    open_failed,
    not_regular_file,
    hardlink_rejected,
    identity_query_failed,
    canonical_path_failed,
    file_too_large,
    read_failed,
    file_changed,
    digest_failed,
    version_missing,
    malformed_pe,
    unsupported_machine,
    authenticode_invalid,
    process_image_query_failed,
    process_image_mismatch,
};

[[nodiscard]] std::string_view to_string(
    WindowsBinaryIdentityErrorCode code) noexcept;

struct WindowsBinaryIdentity final {
    // This path remains private process-local metadata and must not be emitted
    // into committed evidence.
    std::filesystem::path canonical_path;
    WindowsFileSnapshot snapshot{};
    std::array<std::byte, 32U> sha256{};
    std::optional<WindowsFileVersion> file_version;
    WindowsPeMachine pe_machine{WindowsPeMachine::unknown};
    bool authenticode_valid{false};
    std::string anonymized_profile_fingerprint;

    friend bool operator==(const WindowsBinaryIdentity&,
                           const WindowsBinaryIdentity&) noexcept = default;
};

enum class AuthenticodePolicy {
    required,
    not_required_for_project_owned_binary,
};

struct WindowsBinaryObservationPolicy final {
    AuthenticodePolicy authenticode{AuthenticodePolicy::required};
    bool file_version_required{true};
};

struct WindowsBinaryIdentityResult final {
    std::optional<WindowsBinaryIdentity> identity;
    WindowsBinaryIdentityErrorCode code{WindowsBinaryIdentityErrorCode::none};
    std::uint32_t native_error{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return identity.has_value();
    }
};

// Observes a bounded, stable, non-reparse, single-link executable and performs
// an offline Authenticode verification. No path or digest is logged.
[[nodiscard]] WindowsBinaryIdentityResult observe_windows_binary_identity(
    const std::filesystem::path& path,
    std::uint64_t maximum_file_bytes = kMaximumObservedExecutableBytes,
    WindowsBinaryObservationPolicy policy = {}) noexcept;

[[nodiscard]] WindowsBinaryIdentityResult verify_windows_process_image_identity(
    void* process_handle,
    const WindowsBinaryIdentity& expected,
    std::uint64_t maximum_file_bytes = kMaximumObservedExecutableBytes,
    WindowsBinaryObservationPolicy policy = {}) noexcept;

[[nodiscard]] bool same_windows_file_identity(
    const WindowsBinaryIdentity& left,
    const WindowsBinaryIdentity& right) noexcept;

struct SteamAppManifestObservation final {
    std::uint32_t app_id{0U};
    std::uint64_t build_id{0U};
    WindowsFileSnapshot snapshot{};
};

enum class SteamAppManifestErrorCode {
    none,
    unsafe_path,
    open_failed,
    too_large,
    read_failed,
    changed,
    malformed,
    duplicate_field,
    missing_field,
    unexpected_app_id,
    unexpected_build_id,
};

[[nodiscard]] std::string_view to_string(
    SteamAppManifestErrorCode code) noexcept;

struct SteamAppManifestResult final {
    std::optional<SteamAppManifestObservation> observation;
    SteamAppManifestErrorCode code{SteamAppManifestErrorCode::none};
    std::uint32_t native_error{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return observation.has_value();
    }
};

[[nodiscard]] SteamAppManifestResult observe_steam_app_manifest_70(
    const std::filesystem::path& path,
    std::uint64_t maximum_file_bytes = kMaximumSteamAppManifestBytes) noexcept;

enum class StockBinaryProfileEvidenceStatus {
    pending,
    observed,
};

// Metadata-only publication shape. Native paths and raw SHA-256 values are
// intentionally absent.
struct StockBinaryProfileObservation final {
    WindowsFileVersion client_file_version{};
    WindowsFileVersion server_launcher_version{};
    std::optional<WindowsFileVersion> server_engine_version;
    std::optional<std::uint32_t> protocol;
    std::optional<std::uint32_t> engine_build;
    std::uint32_t steam_app_id{0U};
    std::uint64_t steam_build_id{0U};
    WindowsPeMachine client_machine{WindowsPeMachine::unknown};
    WindowsPeMachine server_machine{WindowsPeMachine::unknown};
    bool client_signature_valid{false};
    bool server_signature_valid{false};
    std::string client_profile_fingerprint;
    std::string server_profile_fingerprint;
    StockBinaryProfileEvidenceStatus evidence_status{
        StockBinaryProfileEvidenceStatus::pending};
};

enum class StockBinaryProfileErrorCode {
    none,
    client_identity_invalid,
    client_version_mismatch,
    client_machine_mismatch,
    client_signature_invalid,
    server_identity_invalid,
    server_version_mismatch,
    server_machine_mismatch,
    server_signature_invalid,
    app_manifest_invalid,
};

[[nodiscard]] std::string_view to_string(
    StockBinaryProfileErrorCode code) noexcept;

struct StockBinaryProfileResult final {
    std::optional<StockBinaryProfileObservation> observation;
    StockBinaryProfileErrorCode code{StockBinaryProfileErrorCode::none};
    WindowsBinaryIdentityErrorCode binary_error{
        WindowsBinaryIdentityErrorCode::none};
    SteamAppManifestErrorCode manifest_error{SteamAppManifestErrorCode::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return observation.has_value();
    }
};

// Pure validation boundary used after every input has already been observed
// from a stable held file identity. It performs no filesystem or trust-store
// access and cannot weaken the stock observer's required-signature policy.
[[nodiscard]] StockBinaryProfileResult
validate_required_stock_binary_profile_observations(
    const WindowsBinaryIdentity& client,
    const WindowsBinaryIdentity& server_launcher,
    const SteamAppManifestObservation& app_manifest) noexcept;

[[nodiscard]] StockBinaryProfileResult observe_required_stock_binary_profile(
    const std::filesystem::path& client,
    const std::filesystem::path& server_launcher,
    const std::filesystem::path& app_manifest) noexcept;

} // namespace hlclient::platform::windows
