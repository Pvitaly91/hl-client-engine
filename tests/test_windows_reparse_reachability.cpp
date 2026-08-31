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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace windows = hlclient::platform::windows;

struct MountPointBuffer final {
    ULONG reparse_tag;
    USHORT reparse_data_length;
    USHORT reserved;
    USHORT substitute_name_offset;
    USHORT substitute_name_length;
    USHORT print_name_offset;
    USHORT print_name_length;
    WCHAR path_buffer[1];
};

class Fixture final {
public:
    Fixture()
    {
        std::array<wchar_t, 32'768U> module{};
        const DWORD size = ::GetModuleFileNameW(
            nullptr, module.data(), static_cast<DWORD>(module.size()));
        REQUIRE(size > 0U);
        REQUIRE(size < module.size());
        const auto nonce =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::path{std::wstring_view{module.data(), size}}.parent_path() /
               (L"hlclient-reachability-" +
                std::to_wstring(::GetCurrentProcessId()) + L"-" +
                std::to_wstring(nonce));
        REQUIRE(fs::create_directory(root));
    }

    ~Fixture()
    {
        for (const auto& link : directory_links) {
            static_cast<void>(::RemoveDirectoryW(link.c_str()));
        }
        std::error_code error;
        fs::remove_all(root, error);
    }

    fs::path root;
    std::vector<fs::path> directory_links;
};

[[nodiscard]] bool create_directory_symlink(
    Fixture& fixture, const fs::path& link, const fs::path& target)
{
    constexpr DWORD unprivileged = 0x2U;
    if (::CreateSymbolicLinkW(
            link.c_str(), target.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY | unprivileged) == FALSE &&
        ::CreateSymbolicLinkW(
            link.c_str(), target.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY) == FALSE) {
        return false;
    }
    fixture.directory_links.push_back(link);
    return true;
}

[[nodiscard]] bool create_junction(
    Fixture& fixture, const fs::path& link, const fs::path& target)
{
    std::error_code error;
    fs::create_directory(link, error);
    if (error) return false;
    const HANDLE handle = ::CreateFileW(
        link.c_str(), GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const std::wstring substitute = L"\\??\\" + target.native();
    const std::wstring print = target.native();
    const auto substitute_bytes =
        static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    const auto print_bytes =
        static_cast<USHORT>(print.size() * sizeof(wchar_t));
    const auto path_bytes = static_cast<std::size_t>(substitute_bytes) +
                            sizeof(wchar_t) + print_bytes + sizeof(wchar_t);
    constexpr auto common_header =
        offsetof(MountPointBuffer, substitute_name_offset);
    std::vector<std::byte> storage(common_header + 8U + path_bytes);
    auto* data = reinterpret_cast<MountPointBuffer*>(storage.data());
    data->reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->reserved = 0U;
    data->substitute_name_offset = 0U;
    data->substitute_name_length = substitute_bytes;
    data->print_name_offset =
        static_cast<USHORT>(substitute_bytes + sizeof(wchar_t));
    data->print_name_length = print_bytes;
    std::copy(substitute.begin(), substitute.end(), data->path_buffer);
    auto* print_at = reinterpret_cast<wchar_t*>(
        reinterpret_cast<std::byte*>(data->path_buffer) +
        data->print_name_offset);
    std::copy(print.begin(), print.end(), print_at);
    data->reparse_data_length = static_cast<USHORT>(8U + path_bytes);
    DWORD returned = 0U;
    const BOOL written = ::DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, data,
        static_cast<DWORD>(common_header + data->reparse_data_length),
        nullptr, 0U, &returned, nullptr);
    static_cast<void>(::CloseHandle(handle));
    if (written == FALSE) return false;
    fixture.directory_links.push_back(link);
    return true;
}

[[nodiscard]] std::optional<wchar_t> missing_drive_letter()
{
    const DWORD drives = ::GetLogicalDrives();
    if (drives == 0U) return std::nullopt;
    for (wchar_t letter = L'Z'; letter >= L'D'; --letter) {
        const auto bit = static_cast<unsigned int>(letter - L'A');
        if ((drives & (1UL << bit)) == 0U) return letter;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE(
    "No-follow reachability distinguishes directory symlink outcomes",
    "[windows][stock-runtime][reparse][reachability-fixture]")
{
    Fixture fixture;
    const auto target = fixture.root / L"target";
    REQUIRE(fs::create_directory(target));
    const auto reachable_link = fixture.root / L"reachable-link";
    if (!create_directory_symlink(fixture, reachable_link, target)) {
        SKIP("Directory symlink fixture capability is unavailable");
    }

    const auto reachable =
        windows::observe_windows_reparse_target(reachable_link);
    REQUIRE(reachable);
    CHECK(reachable.value->provenance.tag.category ==
          windows::WindowsReparseTagCategory::symbolic_link);
    CHECK(reachable.value->reachability ==
          windows::WindowsReparseTargetReachability::reachable);

    const auto dangling_link = fixture.root / L"dangling-link";
    REQUIRE(create_directory_symlink(
        fixture, dangling_link, fixture.root / L"absent"));
    const auto dangling =
        windows::observe_windows_reparse_target(dangling_link);
    REQUIRE(dangling);
    CHECK(dangling.value->reachability ==
          windows::WindowsReparseTargetReachability::target_path_not_found);
    CHECK(dangling.value->diagnostic_classification ==
          windows::WindowsReparseDiagnosticClassification::
              dangling_directory_symlink);

    const auto intermediate_link = fixture.root / L"intermediate-link";
    REQUIRE(create_directory_symlink(
        fixture, intermediate_link,
        fixture.root / L"absent-parent" / L"child"));
    const auto intermediate =
        windows::observe_windows_reparse_target(intermediate_link);
    REQUIRE(intermediate);
    CHECK(intermediate.value->reachability ==
          windows::WindowsReparseTargetReachability::
              target_component_not_found);
}

TEST_CASE(
    "UNC expressions are classified without a remote open",
    "[windows][stock-runtime][reparse][reachability-fixture]")
{
    Fixture fixture;
    const auto link = fixture.root / L"unc-link";
    if (!create_directory_symlink(
            fixture, link,
            fs::path{L"\\\\hlclient-invalid.invalid\\share\\asset"})) {
        SKIP("Directory symlink fixture capability is unavailable");
    }
    const auto observed = windows::observe_windows_reparse_target(link);
    REQUIRE(observed);
    CHECK(observed.value->provenance.target_expression.kind ==
          windows::WindowsReparseTargetExpressionKind::unc_path);
    CHECK(observed.value->reachability ==
          windows::WindowsReparseTargetReachability::
              target_remote_or_device);
    CHECK_FALSE(observed.value->target_identity.has_value());
}

TEST_CASE(
    "A junction to an absent drive is a missing volume, not an empty target",
    "[windows][stock-runtime][reparse][reachability-fixture]")
{
    Fixture fixture;
    const auto drive = missing_drive_letter();
    if (!drive) SKIP("Every drive letter is currently assigned");
    const auto link = fixture.root / L"missing-volume";
    const fs::path target{
        std::wstring{*drive} + L":\\hlclient-absent-target"};
    if (!create_junction(fixture, link, target)) {
        SKIP("Junction fixture capability is unavailable");
    }
    const auto observed = windows::observe_windows_reparse_target(link);
    REQUIRE(observed);
    CHECK(observed.value->reachability ==
          windows::WindowsReparseTargetReachability::
              target_volume_not_found);
    CHECK(observed.value->native_error_category ==
          windows::WindowsReparseNativeErrorCategory::device_not_exist);
    CHECK_FALSE(observed.value->target_identity.has_value());
    CHECK_FALSE(fs::exists(target));
}

TEST_CASE(
    "Nested junction cycles are detected by pinned file identity",
    "[windows][stock-runtime][reparse][reachability-fixture][cycle]")
{
    Fixture fixture;
    const auto first = fixture.root / L"cycle-first";
    const auto second = fixture.root / L"cycle-second";
    if (!create_junction(fixture, first, second) ||
        !create_junction(fixture, second, first)) {
        SKIP("Junction cycle fixture capability is unavailable");
    }

    const auto observed = windows::observe_windows_reparse_target(first);
    REQUIRE(observed);
    CHECK(observed.value->reachability ==
          windows::WindowsReparseTargetReachability::target_cycle);
    CHECK(observed.value->native_error_category ==
          windows::WindowsReparseNativeErrorCategory::circular_dependency);
    CHECK(observed.value->diagnostic_classification ==
          windows::WindowsReparseDiagnosticClassification::cyclic_target);
    CHECK_FALSE(observed.value->target_identity.has_value());
    REQUIRE(observed.value->nested_failure);
    const auto& failure = *observed.value->nested_failure;
    CHECK(failure.traversal_depth == 2U);
    CHECK(failure.nested_ordinal == 2U);
    CHECK(failure.reparse_tag_category ==
          windows::WindowsReparseTagCategory::mount_point);
    CHECK(failure.expression_kind ==
          windows::WindowsReparseTargetExpressionKind::
              nt_object_manager_path);
    CHECK(failure.reachability ==
          windows::WindowsReparseTargetReachability::target_cycle);
    CHECK(failure.failure_phase ==
          windows::StockExternalTopologyFailurePhase::nested_reparse_decode);
    CHECK(failure.directory);
    CHECK(failure.native_error_category ==
          windows::WindowsReparseNativeErrorCategory::circular_dependency);
    CHECK(failure.native_error == ERROR_CIRCULAR_DEPENDENCY);
    CHECK(failure.private_link_path.filename() == first.filename());
}

TEST_CASE(
    "Nested junction depth is bounded independently of lexical components",
    "[windows][stock-runtime][reparse][reachability-fixture][depth]")
{
    Fixture fixture;
    const auto target = fixture.root / L"depth-target";
    REQUIRE(fs::create_directory(target));
    const auto third = fixture.root / L"depth-third";
    const auto second = fixture.root / L"depth-second";
    const auto first = fixture.root / L"depth-first";
    if (!create_junction(fixture, third, target) ||
        !create_junction(fixture, second, third) ||
        !create_junction(fixture, first, second)) {
        SKIP("Junction depth fixture capability is unavailable");
    }

    windows::WindowsReparseProvenanceLimits shallow{};
    shallow.maximum_nested_reparse_depth = 1U;
    const auto bounded =
        windows::observe_windows_reparse_target(first, shallow);
    REQUIRE(bounded);
    CHECK(bounded.value->reachability ==
          windows::WindowsReparseTargetReachability::target_depth_exceeded);
    CHECK(bounded.value->native_error_category ==
          windows::WindowsReparseNativeErrorCategory::too_many_links);
    CHECK(bounded.value->diagnostic_classification ==
          windows::WindowsReparseDiagnosticClassification::
              target_depth_exceeded);
    CHECK_FALSE(bounded.value->target_identity.has_value());
    REQUIRE(bounded.value->nested_failure);
    const auto& failure = *bounded.value->nested_failure;
    CHECK(failure.traversal_depth == 2U);
    CHECK(failure.nested_ordinal == 2U);
    CHECK(failure.reparse_tag_category ==
          windows::WindowsReparseTagCategory::mount_point);
    CHECK(failure.expression_kind ==
          windows::WindowsReparseTargetExpressionKind::
              nt_object_manager_path);
    CHECK(failure.reachability ==
          windows::WindowsReparseTargetReachability::target_depth_exceeded);
    CHECK(failure.failure_phase ==
          windows::StockExternalTopologyFailurePhase::nested_reparse_decode);
    CHECK(failure.directory);
    CHECK(failure.native_error_category ==
          windows::WindowsReparseNativeErrorCategory::too_many_links);
    CHECK(failure.native_error == ERROR_TOO_MANY_LINKS);
    CHECK(failure.private_link_path.filename() == third.filename());

    windows::WindowsReparseProvenanceLimits exact{};
    exact.maximum_nested_reparse_depth = 2U;
    const auto reachable =
        windows::observe_windows_reparse_target(first, exact);
    REQUIRE(reachable);
    CHECK(reachable.value->reachability ==
          windows::WindowsReparseTargetReachability::reachable);
    CHECK(reachable.value->target_identity.has_value());
    CHECK_FALSE(reachable.value->nested_failure.has_value());
}

TEST_CASE(
    "A missing target after a nested junction retains that junction context",
    "[windows][stock-runtime][reparse][reachability-fixture][nested-open]")
{
    Fixture fixture;
    const auto absent = fixture.root / L"nested-absent";
    const auto second = fixture.root / L"nested-second";
    const auto first = fixture.root / L"nested-first";
    if (!create_junction(fixture, second, absent) ||
        !create_junction(fixture, first, second)) {
        SKIP("Nested junction fixture capability is unavailable");
    }

    const auto observed = windows::observe_windows_reparse_target(first);
    REQUIRE(observed);
    CHECK(observed.value->reachability ==
          windows::WindowsReparseTargetReachability::target_path_not_found);
    CHECK(observed.value->failure_phase ==
          windows::StockExternalTopologyFailurePhase::nested_entry_open);
    CHECK(observed.value->native_error_category ==
          windows::WindowsReparseNativeErrorCategory::file_not_found);
    CHECK(observed.value->native_error == ERROR_FILE_NOT_FOUND);
    CHECK(observed.value->diagnostic_classification ==
          windows::WindowsReparseDiagnosticClassification::
              dangling_directory_junction);
    REQUIRE(observed.value->nested_failure);
    const auto& failure = *observed.value->nested_failure;
    CHECK(failure.traversal_depth == 1U);
    CHECK(failure.nested_ordinal == 1U);
    CHECK(failure.reparse_tag_category ==
          windows::WindowsReparseTagCategory::mount_point);
    CHECK(failure.expression_kind ==
          windows::WindowsReparseTargetExpressionKind::
              nt_object_manager_path);
    CHECK(failure.reachability == observed.value->reachability);
    CHECK(failure.failure_phase == observed.value->failure_phase);
    CHECK(failure.directory);
    CHECK(failure.native_error_category ==
          observed.value->native_error_category);
    CHECK(failure.native_error == observed.value->native_error);
    CHECK(failure.private_link_path.filename() == second.filename());
    CHECK_FALSE(fs::exists(absent));
}

TEST_CASE(
    "A protected local directory is typed as inaccessible when supported",
    "[windows][stock-runtime][reparse][reachability-fixture][access-denied]")
{
    Fixture fixture;
    const auto protected_target =
        fixture.root.root_path() / L"System Volume Information";
    const auto link = fixture.root / L"protected-target";
    if (!create_junction(fixture, link, protected_target)) {
        SKIP("Protected-directory junction fixture capability is unavailable");
    }

    const auto observed = windows::observe_windows_reparse_target(link);
    REQUIRE(observed);
    if (observed.value->reachability !=
        windows::WindowsReparseTargetReachability::target_access_denied) {
        SKIP("This host grants access to its protected-directory fixture");
    }
    CHECK(observed.value->native_error_category ==
          windows::WindowsReparseNativeErrorCategory::access_denied);
    CHECK(observed.value->diagnostic_classification ==
          windows::WindowsReparseDiagnosticClassification::
              inaccessible_target);
    CHECK_FALSE(observed.value->target_identity.has_value());
}

TEST_CASE(
    "A reachable observation pins the exact target against mutation",
    "[windows][stock-runtime][reparse][reachability-fixture][mutation]")
{
    Fixture fixture;
    const auto target = fixture.root / L"mutation-target";
    const auto renamed = fixture.root / L"mutation-renamed";
    REQUIRE(fs::create_directory(target));
    const auto link = fixture.root / L"mutation-link";
    if (!create_junction(fixture, link, target)) {
        SKIP("Junction mutation fixture capability is unavailable");
    }

    auto observed = windows::observe_windows_reparse_target(link);
    REQUIRE(observed);
    REQUIRE(observed.value->reachability ==
            windows::WindowsReparseTargetReachability::reachable);
    CHECK(::MoveFileExW(target.c_str(), renamed.c_str(), 0U) == FALSE);
    const auto blocked_error = ::GetLastError();
    CHECK((blocked_error == ERROR_SHARING_VIOLATION ||
           blocked_error == ERROR_ACCESS_DENIED));

    observed.value.reset();
    REQUIRE(::MoveFileExW(target.c_str(), renamed.c_str(), 0U) != FALSE);
}
