#include <hlclient/platform/windows/windows_reparse_provenance.hpp>

#include <catch2/catch_test_macros.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace windows = hlclient::platform::windows;

constexpr std::uint32_t kMountPointTag = 0xA0000003U;
constexpr std::uint32_t kSymbolicLinkTag = 0xA000000CU;
constexpr std::uint32_t kAppExecLinkTag = 0x8000001BU;

void append_u16(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    append_u16(bytes, static_cast<std::uint16_t>(value & 0xFFFFU));
    append_u16(bytes, static_cast<std::uint16_t>(value >> 16U));
}

void write_u16(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint16_t value)
{
    REQUIRE(offset + 2U <= bytes.size());
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void write_u32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    REQUIRE(offset + 4U <= bytes.size());
    write_u16(bytes, offset,
              static_cast<std::uint16_t>(value & 0xFFFFU));
    write_u16(bytes, offset + 2U,
              static_cast<std::uint16_t>(value >> 16U));
}

void append_utf16(
    std::vector<std::byte>& bytes,
    const std::wstring_view text)
{
    for (const auto unit : text) {
        append_u16(bytes, static_cast<std::uint16_t>(unit));
    }
}

[[nodiscard]] std::vector<std::byte> mount_point_payload(
    const std::wstring_view substitute,
    const std::wstring_view print)
{
    const auto substitute_bytes = static_cast<std::uint16_t>(
        substitute.size() * sizeof(wchar_t));
    const auto print_bytes =
        static_cast<std::uint16_t>(print.size() * sizeof(wchar_t));
    const auto data_bytes = static_cast<std::uint16_t>(
        8U + substitute_bytes + print_bytes);

    std::vector<std::byte> bytes;
    bytes.reserve(8U + data_bytes);
    append_u32(bytes, kMountPointTag);
    append_u16(bytes, data_bytes);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, substitute_bytes);
    append_u16(bytes, substitute_bytes);
    append_u16(bytes, print_bytes);
    append_utf16(bytes, substitute);
    append_utf16(bytes, print);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> symbolic_link_payload(
    const std::wstring_view substitute,
    const std::wstring_view print,
    const std::uint32_t flags)
{
    const auto substitute_bytes = static_cast<std::uint16_t>(
        substitute.size() * sizeof(wchar_t));
    const auto print_bytes =
        static_cast<std::uint16_t>(print.size() * sizeof(wchar_t));
    const auto data_bytes = static_cast<std::uint16_t>(
        12U + substitute_bytes + print_bytes);

    std::vector<std::byte> bytes;
    bytes.reserve(8U + data_bytes);
    append_u32(bytes, kSymbolicLinkTag);
    append_u16(bytes, data_bytes);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, substitute_bytes);
    append_u16(bytes, substitute_bytes);
    append_u16(bytes, print_bytes);
    append_u32(bytes, flags);
    append_utf16(bytes, substitute);
    append_utf16(bytes, print);
    return bytes;
}

class FixtureRoot final {
public:
    FixtureRoot()
    {
        std::array<wchar_t, 32'768U> module{};
        const DWORD length = ::GetModuleFileNameW(
            nullptr, module.data(), static_cast<DWORD>(module.size()));
        REQUIRE(length > 0U);
        REQUIRE(length < module.size());
        const auto nonce =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::path{std::wstring_view{module.data(), length}}.parent_path() /
               (L"hl-rp-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
                std::to_wstring(nonce));
        fs::create_directories(path);
    }

    ~FixtureRoot()
    {
        if (!link.empty()) {
            static_cast<void>(::RemoveDirectoryW(link.c_str()));
        }
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    FixtureRoot(const FixtureRoot&) = delete;
    FixtureRoot& operator=(const FixtureRoot&) = delete;

    fs::path path;
    fs::path link;
};

[[nodiscard]] bool create_junction(
    const fs::path& link,
    const fs::path& target)
{
    if (!::CreateDirectoryW(link.c_str(), nullptr) &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    const std::wstring substitute = L"\\??\\" + target.native();
    const std::wstring print = target.native();
    const auto substitute_bytes = static_cast<std::uint16_t>(
        substitute.size() * sizeof(wchar_t));
    const auto print_bytes =
        static_cast<std::uint16_t>(print.size() * sizeof(wchar_t));
    const auto print_offset =
        static_cast<std::uint16_t>(substitute_bytes + sizeof(wchar_t));
    const auto data_bytes = static_cast<std::uint16_t>(
        8U + print_offset + print_bytes + sizeof(wchar_t));
    std::vector<std::byte> bytes;
    bytes.reserve(8U + data_bytes);
    append_u32(bytes, kMountPointTag);
    append_u16(bytes, data_bytes);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, substitute_bytes);
    append_u16(bytes, print_offset);
    append_u16(bytes, print_bytes);
    append_utf16(bytes, substitute);
    append_u16(bytes, 0U);
    append_utf16(bytes, print);
    append_u16(bytes, 0U);
    const HANDLE handle = ::CreateFileW(
        link.c_str(), GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        static_cast<void>(::RemoveDirectoryW(link.c_str()));
        return false;
    }
    DWORD returned = 0U;
    const bool created = ::DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, bytes.data(),
        static_cast<DWORD>(bytes.size()), nullptr, 0U, &returned, nullptr) !=
                         FALSE;
    static_cast<void>(::CloseHandle(handle));
    if (!created) {
        static_cast<void>(::RemoveDirectoryW(link.c_str()));
    }
    return created;
}

} // namespace

TEST_CASE(
    "Windows reparse tag catalog preserves category and property boundaries",
    "[windows][stock-runtime][reparse][tag]")
{
    CHECK(windows::classify_windows_reparse_tag(0U) ==
          windows::WindowsReparseTagCategory::none);
    CHECK(windows::classify_windows_reparse_tag(kMountPointTag) ==
          windows::WindowsReparseTagCategory::mount_point);
    CHECK(windows::classify_windows_reparse_tag(kSymbolicLinkTag) ==
          windows::WindowsReparseTagCategory::symbolic_link);
    CHECK(windows::classify_windows_reparse_tag(kAppExecLinkTag) ==
          windows::WindowsReparseTagCategory::app_exec_link);
    CHECK(windows::classify_windows_reparse_tag(0x9000001AU) ==
          windows::WindowsReparseTagCategory::cloud_placeholder);
    CHECK(windows::classify_windows_reparse_tag(0x9000301AU) ==
          windows::WindowsReparseTagCategory::cloud_placeholder_variant);
    CHECK(windows::classify_windows_reparse_tag(0x8FFF0001U) ==
          windows::WindowsReparseTagCategory::malformed_or_unreadable);
    CHECK(windows::classify_windows_reparse_tag(0x00000042U) ==
          windows::WindowsReparseTagCategory::third_party_other);
    CHECK(windows::classify_windows_reparse_tag(0x20000042U) ==
          windows::WindowsReparseTagCategory::third_party_name_surrogate);
    CHECK(windows::to_string(
              windows::WindowsReparseTagCategory::mount_point) ==
          "mount_point");
    CHECK(windows::to_string(
              windows::WindowsReparseTargetReachability::
                  target_component_not_found) ==
          "target_component_not_found");
}

TEST_CASE(
    "Windows mount-point payload decoding is literal bounded and NUL independent",
    "[windows][stock-runtime][reparse][payload]")
{
    const auto bytes =
        mount_point_payload(L"\\??\\C:\\fixed\\asset", L"C:\\fixed\\asset");
    const auto decoded = windows::decode_windows_reparse_payload(bytes, true);
    REQUIRE(decoded);
    REQUIRE(decoded.value);
    CHECK(decoded.value->tag.raw_tag == kMountPointTag);
    CHECK(decoded.value->tag.microsoft);
    CHECK(decoded.value->tag.name_surrogate);
    CHECK(decoded.value->tag.directory);
    CHECK(decoded.value->tag.category ==
          windows::WindowsReparseTagCategory::mount_point);
    CHECK(decoded.value->payload_status ==
          windows::WindowsReparsePayloadStatus::path_contract_decoded);
    CHECK(decoded.value->payload_error ==
          windows::WindowsReparsePayloadErrorCode::none);
    CHECK(decoded.value->private_substitute_name ==
          L"\\??\\C:\\fixed\\asset");
    CHECK(decoded.value->private_print_name == L"C:\\fixed\\asset");
    CHECK(decoded.value->target_expression.kind ==
          windows::WindowsReparseTargetExpressionKind::
              nt_object_manager_path);
    CHECK(decoded.value->private_payload_sha256.size() == 64U);
}

TEST_CASE(
    "Windows symbolic-link decoder distinguishes absolute and relative contracts",
    "[windows][stock-runtime][reparse][payload][symlink]")
{
    SECTION("absolute") {
        const auto bytes = symbolic_link_payload(
            L"C:\\fixed\\asset", L"C:\\fixed\\asset", 0U);
        const auto decoded =
            windows::decode_windows_reparse_payload(bytes, true);
        REQUIRE(decoded);
        REQUIRE(decoded.value);
        CHECK_FALSE(decoded.value->symbolic_link_relative);
        CHECK(decoded.value->target_expression.kind ==
              windows::WindowsReparseTargetExpressionKind::
                  drive_absolute_path);
    }

    SECTION("relative") {
        const auto bytes =
            symbolic_link_payload(L"..\\asset", L"..\\asset", 1U);
        const auto decoded =
            windows::decode_windows_reparse_payload(bytes, true);
        REQUIRE(decoded);
        REQUIRE(decoded.value);
        CHECK(decoded.value->symbolic_link_relative);
        CHECK(decoded.value->target_expression.relative);
        CHECK(decoded.value->target_expression.kind ==
              windows::WindowsReparseTargetExpressionKind::relative_path);
    }
}

TEST_CASE(
    "Windows reparse decoder rejects every malformed literal boundary",
    "[windows][stock-runtime][reparse][payload][malformed]")
{
    const auto expect = [](const std::vector<std::byte>& bytes,
                           const windows::WindowsReparsePayloadErrorCode code) {
        const auto decoded =
            windows::decode_windows_reparse_payload(bytes, true);
        REQUIRE(decoded);
        REQUIRE(decoded.value);
        CHECK(decoded.value->payload_status ==
              windows::WindowsReparsePayloadStatus::malformed);
        CHECK(decoded.value->payload_error == code);
        CHECK(decoded.value->target_expression.kind ==
              windows::WindowsReparseTargetExpressionKind::malformed);
    };

    SECTION("truncated common header") {
        std::vector<std::byte> bytes(7U);
        expect(bytes,
               windows::WindowsReparsePayloadErrorCode::truncated_header);
    }

    SECTION("invalid declared data length") {
        auto bytes = mount_point_payload(L"\\??\\C:\\a", L"C:\\a");
        write_u16(bytes, 4U, 0U);
        expect(bytes,
               windows::WindowsReparsePayloadErrorCode::invalid_data_length);
    }

    SECTION("substitute offset overflow") {
        auto bytes = mount_point_payload(L"\\??\\C:\\a", L"C:\\a");
        write_u16(bytes, 8U, 0xFFFEU);
        expect(bytes,
               windows::WindowsReparsePayloadErrorCode::path_range_overflow);
    }

    SECTION("odd UTF-16 length") {
        auto bytes = mount_point_payload(L"\\??\\C:\\a", L"C:\\a");
        write_u16(bytes, 10U, 3U);
        expect(bytes, windows::WindowsReparsePayloadErrorCode::
                          odd_utf16_offset_or_length);
    }

    SECTION("substitute and print ranges overlap") {
        auto bytes = mount_point_payload(L"\\??\\C:\\a", L"C:\\a");
        write_u16(bytes, 12U, 2U);
        expect(bytes,
               windows::WindowsReparsePayloadErrorCode::path_ranges_overlap);
    }

    SECTION("print range overflow") {
        auto bytes = mount_point_payload(L"\\??\\C:\\a", L"C:\\a");
        write_u16(bytes, 14U, 0xFFFEU);
        expect(bytes,
               windows::WindowsReparsePayloadErrorCode::path_range_overflow);
    }

    SECTION("unsupported symbolic-link flags") {
        const auto bytes =
            symbolic_link_payload(L"..\\asset", L"..\\asset", 2U);
        expect(bytes, windows::WindowsReparsePayloadErrorCode::
                          invalid_symbolic_link_flags);
    }

    SECTION("malformed Microsoft tag") {
        std::vector<std::byte> bytes;
        append_u32(bytes, 0x8FFF0001U);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        const auto decoded =
            windows::decode_windows_reparse_payload(bytes, true);
        REQUIRE(decoded);
        REQUIRE(decoded.value);
        CHECK(decoded.value->tag.category ==
              windows::WindowsReparseTagCategory::malformed_or_unreadable);
        CHECK(decoded.value->payload_status ==
              windows::WindowsReparsePayloadStatus::malformed);
    }

    SECTION("truncated third-party GUID header") {
        std::vector<std::byte> bytes;
        append_u32(bytes, 0x00000042U);
        append_u16(bytes, 4U);
        append_u16(bytes, 0U);
        append_u32(bytes, 0x12345678U);
        expect(bytes,
               windows::WindowsReparsePayloadErrorCode::
                   truncated_guid_header);
    }
}

TEST_CASE(
    "Opaque Windows reparse tags remain bounded and never invent a target path",
    "[windows][stock-runtime][reparse][opaque]")
{
    std::vector<std::byte> bytes;
    append_u32(bytes, 0x00000042U);
    append_u16(bytes, 4U);
    append_u16(bytes, 0U);
    // An independently authored REPARSE_GUID_DATA_BUFFER header. The GUID is
    // opaque to this library and remains covered by the private payload hash.
    append_u32(bytes, 0x12345678U);
    append_u16(bytes, 0x1234U);
    append_u16(bytes, 0x5678U);
    append_u32(bytes, 0x11223344U);
    append_u32(bytes, 0x55667788U);
    append_u32(bytes, 0x12345678U); // Four opaque owner-defined data bytes.

    const auto decoded = windows::decode_windows_reparse_payload(bytes, false);
    REQUIRE(decoded);
    REQUIRE(decoded.value);
    CHECK(decoded.value->tag.category ==
          windows::WindowsReparseTagCategory::third_party_other);
    CHECK(decoded.value->payload_status ==
          windows::WindowsReparsePayloadStatus::opaque_non_path_payload);
    CHECK(decoded.value->target_expression.kind ==
          windows::WindowsReparseTargetExpressionKind::
              opaque_non_path_payload);
    CHECK(decoded.value->private_substitute_name.empty());
    CHECK(decoded.value->private_print_name.empty());
    CHECK(decoded.value->private_payload_sha256.size() == 64U);

    CHECK(windows::classify_windows_reparse_tag(1U) ==
          windows::WindowsReparseTagCategory::malformed_or_unreadable);
    CHECK(windows::classify_windows_reparse_tag(2U) ==
          windows::WindowsReparseTagCategory::malformed_or_unreadable);
    CHECK(windows::classify_windows_reparse_tag(0xC0008000U) ==
          windows::WindowsReparseTagCategory::malformed_or_unreadable);
    CHECK(windows::classify_windows_reparse_tag(0x30000042U) ==
          windows::WindowsReparseTagCategory::malformed_or_unreadable);
}

TEST_CASE(
    "Windows target expression classification is typed and does not establish identity",
    "[windows][stock-runtime][reparse][expression]")
{
    CHECK(windows::classify_windows_reparse_target_expression(
              L"..\\asset", true)
              .kind ==
          windows::WindowsReparseTargetExpressionKind::relative_path);
    CHECK(windows::classify_windows_reparse_target_expression(
              L"C:\\asset", false)
              .kind ==
          windows::WindowsReparseTargetExpressionKind::drive_absolute_path);
    CHECK(windows::classify_windows_reparse_target_expression(
              L"\\??\\C:\\asset", false)
              .kind ==
          windows::WindowsReparseTargetExpressionKind::
              nt_object_manager_path);
    CHECK(windows::classify_windows_reparse_target_expression(
              L"\\??\\Volume{12345678-1234-1234-1234-123456789abc}\\asset",
              false)
              .kind ==
          windows::WindowsReparseTargetExpressionKind::volume_guid_path);
    CHECK(windows::classify_windows_reparse_target_expression(
              L"\\??\\UNC\\server\\share\\asset", false)
              .kind == windows::WindowsReparseTargetExpressionKind::unc_path);
    CHECK(windows::classify_windows_reparse_target_expression(
              L"\\\\server\\share\\asset", false)
              .kind == windows::WindowsReparseTargetExpressionKind::unc_path);
    CHECK(windows::classify_windows_reparse_target_expression(
              L"\\Device\\HarddiskVolume1\\asset", false)
              .kind ==
          windows::WindowsReparseTargetExpressionKind::device_path);
    CHECK(windows::classify_windows_reparse_target_expression(
              L"C:\\absolute", true)
              .kind == windows::WindowsReparseTargetExpressionKind::malformed);
    CHECK(windows::classify_windows_reparse_target_expression(
              L"relative", false)
              .kind == windows::WindowsReparseTargetExpressionKind::malformed);
}

TEST_CASE(
    "Windows native target failures map to stable path-free categories",
    "[windows][stock-runtime][reparse][reachability]")
{
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_SUCCESS) ==
          windows::WindowsReparseTargetReachability::reachable);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_FILE_NOT_FOUND) ==
          windows::WindowsReparseTargetReachability::target_path_not_found);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_PATH_NOT_FOUND) ==
          windows::WindowsReparseTargetReachability::
              target_component_not_found);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_NOT_READY) ==
          windows::WindowsReparseTargetReachability::target_volume_not_found);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_ACCESS_DENIED) ==
          windows::WindowsReparseTargetReachability::target_access_denied);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_DIRECTORY) ==
          windows::WindowsReparseTargetReachability::target_not_directory);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_BAD_NETPATH) ==
          windows::WindowsReparseTargetReachability::
              target_remote_or_device);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_CIRCULAR_DEPENDENCY) ==
          windows::WindowsReparseTargetReachability::target_cycle);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_TOO_MANY_LINKS) ==
          windows::WindowsReparseTargetReachability::target_depth_exceeded);
    CHECK(windows::classify_windows_reparse_target_reachability(
              ERROR_REPARSE_TAG_MISMATCH) ==
          windows::WindowsReparseTargetReachability::target_changed);
    CHECK(windows::classify_windows_reparse_target_reachability(0xFFFFFFFFU) ==
          windows::WindowsReparseTargetReachability::
              target_open_failed_other);
}

TEST_CASE(
    "Nested AppExecLink failures remain unsupported opaque diagnostics",
    "[windows][stock-runtime][reparse][nested-classification]")
{
    windows::WindowsReparseNestedFailure failure{};
    failure.reparse_tag_category =
        windows::WindowsReparseTagCategory::app_exec_link;
    failure.expression_kind =
        windows::WindowsReparseTargetExpressionKind::app_execution_alias;
    failure.reachability =
        windows::WindowsReparseTargetReachability::not_applicable;
    failure.failure_phase =
        windows::StockExternalTopologyFailurePhase::nested_reparse_decode;

    CHECK(windows::classify_windows_reparse_nested_failure_diagnostic(
              failure) ==
          windows::WindowsReparseDiagnosticClassification::
              unsupported_tag_without_path_contract);

    failure.reparse_tag_category =
        windows::WindowsReparseTagCategory::mount_point;
    failure.expression_kind =
        windows::WindowsReparseTargetExpressionKind::malformed;
    CHECK(windows::classify_windows_reparse_nested_failure_diagnostic(
              failure) ==
          windows::WindowsReparseDiagnosticClassification::
              malformed_reparse_payload);
}

TEST_CASE(
    "Windows reparse limits have enforced nonzero defaults and hard caps",
    "[windows][stock-runtime][reparse][bounds]")
{
    CHECK(windows::valid_windows_reparse_provenance_limits({}));

    windows::WindowsReparseProvenanceLimits limits{};
    limits.maximum_reparse_payload_bytes =
        windows::kWindowsReparseHardMaximumPayloadBytes + 1U;
    CHECK_FALSE(windows::valid_windows_reparse_provenance_limits(limits));

    limits = {};
    limits.maximum_target_expression_characters = 0U;
    CHECK_FALSE(windows::valid_windows_reparse_provenance_limits(limits));

    limits = {};
    limits.maximum_failure_witnesses = 0U;
    CHECK_FALSE(windows::valid_windows_reparse_provenance_limits(limits));

    limits = {};
    limits.maximum_nested_reparse_depth =
        windows::kWindowsReparseHardMaximumNestedDepth + 1U;
    CHECK_FALSE(windows::valid_windows_reparse_provenance_limits(limits));

    limits = {};
    limits.maximum_diagnostic_targets = 0U;
    CHECK_FALSE(windows::valid_windows_reparse_provenance_limits(limits));
}

TEST_CASE(
    "Windows no-follow provenance identifies reachable and dangling junctions",
    "[windows][stock-runtime][reparse][junction]")
{
    FixtureRoot fixture;
    const auto target = fixture.path / L"target";
    fixture.link = fixture.path / L"link";
    fs::create_directories(target);
    if (!create_junction(fixture.link, target)) {
        SKIP("NTFS mount-point reparse fixture capability is unavailable");
    }

    const auto read = windows::read_windows_reparse_provenance(fixture.link);
    REQUIRE(read);
    REQUIRE(read.value);
    CHECK(read.value->tag.category ==
          windows::WindowsReparseTagCategory::mount_point);
    CHECK(read.value->payload_status ==
          windows::WindowsReparsePayloadStatus::path_contract_decoded);

    auto reachable =
        windows::observe_windows_reparse_target(fixture.link);
    REQUIRE(reachable);
    REQUIRE(reachable.value);
    CHECK(reachable.value->observation_complete);
    CHECK(reachable.value->reachability ==
          windows::WindowsReparseTargetReachability::reachable);
    CHECK(reachable.value->diagnostic_classification ==
          windows::WindowsReparseDiagnosticClassification::
              reachable_name_surrogate);
    REQUIRE(reachable.value->target_identity);
    CHECK(reachable.value->target_identity->directory);

    // The accepted observation deliberately pins the exact target chain with
    // no delete sharing. Release it before exercising the dangling state.
    reachable.value.reset();
    fs::remove_all(target);
    const auto dangling = windows::observe_windows_reparse_target(fixture.link);
    REQUIRE(dangling);
    REQUIRE(dangling.value);
    CHECK(dangling.value->observation_complete);
    CHECK((dangling.value->reachability ==
               windows::WindowsReparseTargetReachability::
                   target_path_not_found ||
           dangling.value->reachability ==
               windows::WindowsReparseTargetReachability::
                   target_component_not_found));
    CHECK(dangling.value->failure_phase ==
          windows::StockExternalTopologyFailurePhase::target_open);
    CHECK(dangling.value->diagnostic_classification ==
          windows::WindowsReparseDiagnosticClassification::
              dangling_directory_junction);
    CHECK_FALSE(dangling.value->target_identity);
    CHECK_FALSE(fs::exists(target));
}
