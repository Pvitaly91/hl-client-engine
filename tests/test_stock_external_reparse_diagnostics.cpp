#include <hlclient/platform/windows/stock_external_target_artifact.hpp>
#include <hlclient/platform/windows/stock_external_target_review.hpp>

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
#include <fstream>
#include <span>
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

[[nodiscard]] fs::path unique_root()
{
    std::array<wchar_t, 32'768U> module{};
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    REQUIRE(size > 0U);
    REQUIRE(size < module.size());
    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::path{std::wstring_view{module.data(), size}}.parent_path() /
           (L"hlclient-rdiag-" + std::to_wstring(::GetCurrentProcessId()) +
            L"-" + std::to_wstring(nonce));
}

void write_file(const fs::path& path, const std::string_view bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

[[nodiscard]] bool create_junction(
    const fs::path& link, const fs::path& target)
{
    std::error_code error;
    fs::create_directories(link, error);
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
    constexpr auto header_bytes =
        offsetof(MountPointBuffer, substitute_name_offset);
    std::vector<std::byte> storage(header_bytes + 8U + path_bytes);
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
    const BOOL ok = ::DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, data,
        static_cast<DWORD>(header_bytes + data->reparse_data_length), nullptr,
        0U, &returned, nullptr);
    static_cast<void>(::CloseHandle(handle));
    return ok != FALSE;
}

[[nodiscard]] std::string read_file(const fs::path& path)
{
    std::ifstream input{path, std::ios::binary};
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

class Fixture final {
public:
    Fixture() : root{unique_root()}
    {
        fs::create_directories(source());
        fs::create_directories(target());
        fs::create_directories(root / L"manual-artifacts");
        write_file(source() / L"ordinary.txt", "source");
        write_file(target() / L"asset.wad", "WAD3");
    }

    ~Fixture()
    {
        static_cast<void>(::RemoveDirectoryW(link().c_str()));
        std::error_code error;
        fs::remove_all(root, error);
    }

    [[nodiscard]] fs::path source() const { return root / L"source"; }
    [[nodiscard]] fs::path target() const { return root / L"target"; }
    [[nodiscard]] fs::path link() const { return source() / L"dangling"; }
    [[nodiscard]] fs::path review_parent() const
    {
        return root / L"manual-artifacts" /
               L"stock-runtime-source-review";
    }

    fs::path root;
};

} // namespace

TEST_CASE(
    "Dangling external junction completes a V2 ineligible diagnostic",
    "[windows][stock-runtime][reparse][external-target]")
{
    Fixture fixture;
    if (!create_junction(fixture.link(), fixture.target())) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    std::error_code remove_error;
    fs::remove_all(fixture.target(), remove_error);
    REQUIRE_FALSE(remove_error);
    REQUIRE_FALSE(fs::exists(fixture.target()));

    const auto source_before = fs::last_write_time(fixture.source());
    const auto diagnostic =
        windows::diagnose_stock_external_targets(fixture.source());
    INFO("diagnostic code=" << windows::to_string(diagnostic.code)
                             << " native=" << diagnostic.native_error);
    REQUIRE(diagnostic);
    REQUIRE(diagnostic.value->targets.size() == 1U);
    CHECK(diagnostic.value->source_inventory_complete);
    CHECK(diagnostic.value->all_targets_diagnostic_complete);
    CHECK_FALSE(diagnostic.value->all_targets_eligible);
    const auto& observed = diagnostic.value->targets.front();
    REQUIRE(observed.reparse_observation);
    CHECK(observed.diagnostic_complete);
    CHECK_FALSE(observed.inventory_available);
    CHECK_FALSE(observed.eligible);
    CHECK(observed.reparse_observation->provenance.tag.category ==
          windows::WindowsReparseTagCategory::mount_point);
    CHECK(observed.reparse_observation->reachability ==
          windows::WindowsReparseTargetReachability::
              target_path_not_found);
    CHECK(observed.reparse_observation->failure_phase ==
          windows::StockExternalTopologyFailurePhase::target_open);
    REQUIRE(observed.failure_witness);
    CHECK_FALSE(observed.failure_witness->private_witness_sha256.empty());

    const auto review = windows::review_stock_external_targets(
        fixture.source(), fixture.review_parent());
    INFO("review code=" << windows::to_string(review.code)
                         << " native=" << review.native_error);
    REQUIRE(review);
    REQUIRE(review.value->targets.size() == 1U);
    CHECK(review.value->completed_target_count == 1U);
    CHECK(review.value->ineligible_target_count == 1U);
    CHECK(review.value->incomplete_target_count == 0U);
    CHECK_FALSE(review.value->all_targets_eligible);

    const auto summary = windows::parse_stock_external_review_summary_v2(
        read_file(review.value->review_root /
                  windows::kStockExternalReviewSummaryLeaf));
    REQUIRE(summary);
    REQUIRE(summary.value->targets.size() == 1U);
    CHECK(summary.value->completed_count == 1U);
    CHECK(summary.value->ineligible_count == 1U);
    CHECK_FALSE(summary.value->targets.front().inventory_available);
    CHECK_FALSE(
        summary.value->targets.front().target_inventory_sha256.has_value());

    const auto private_leaf = windows::stock_external_private_target_leaf(1U);
    REQUIRE(private_leaf);
    const auto private_target =
        windows::parse_stock_external_private_target_v2(
            read_file(review.value->review_root / *private_leaf));
    REQUIRE(private_target);
    CHECK_FALSE(private_target.value->target_inventory.has_value());
    CHECK_FALSE(private_target.value->target_identity.has_value());
    CHECK(private_target.value->tag_category == "mount_point");
    CHECK(private_target.value->reachability == "target_path_not_found");

    const auto approval = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
    CHECK_FALSE(approval);
    CHECK(approval.code ==
          windows::StockExternalReviewErrorCode::target_ineligible);
    CHECK_FALSE(fs::exists(
        review.value->review_root / windows::kStockExternalApprovalLeaf));
    CHECK_FALSE(fs::exists(fixture.target()));
    CHECK(fs::last_write_time(fixture.source()) == source_before);
}

TEST_CASE(
    "External-source diagnostic pins the confirmed source namespace",
    "[windows][stock-runtime][reparse][external-target][mutation]")
{
    Fixture fixture;
    if (!create_junction(fixture.link(), fixture.target())) {
        SKIP("Windows junction fixture capability is unavailable");
    }

    auto diagnostic =
        windows::diagnose_stock_external_targets(fixture.source());
    INFO("diagnostic code=" << windows::to_string(diagnostic.code)
                             << " native=" << diagnostic.native_error);
    REQUIRE(diagnostic);
    REQUIRE(diagnostic.value->source_inventory_complete);

    const auto displaced = fixture.root / L"source-displaced";
    ::SetLastError(ERROR_SUCCESS);
    CHECK_FALSE(::MoveFileExW(
        fixture.source().c_str(), displaced.c_str(), MOVEFILE_WRITE_THROUGH));
    const auto pinned_error = ::GetLastError();
    CHECK((pinned_error == ERROR_SHARING_VIOLATION ||
           pinned_error == ERROR_ACCESS_DENIED));
    CHECK(fs::exists(fixture.source()));
    CHECK_FALSE(fs::exists(displaced));

    // Releasing the public result also releases its opaque confirmed-scan
    // ownership. The same namespace mutation must then become possible.
    diagnostic.value.reset();
    REQUIRE(::MoveFileExW(
                fixture.source().c_str(), displaced.c_str(),
                MOVEFILE_WRITE_THROUGH) != FALSE);
    REQUIRE(::MoveFileExW(
                displaced.c_str(), fixture.source().c_str(),
                MOVEFILE_WRITE_THROUGH) != FALSE);
}

TEST_CASE(
    "A nested junction cycle publishes the exact failing-chain witness",
    "[windows][stock-runtime][reparse][external-target][chain]")
{
    Fixture fixture;
    const auto nested = fixture.root / L"cycle-nested";
    if (!create_junction(nested, fixture.link()) ||
        !create_junction(fixture.link(), nested)) {
        SKIP("Windows junction-cycle fixture capability is unavailable");
    }

    const auto source_before = fs::last_write_time(fixture.source());
    const auto diagnostic =
        windows::diagnose_stock_external_targets(fixture.source());
    INFO("diagnostic code=" << windows::to_string(diagnostic.code)
                             << " native=" << diagnostic.native_error);
    REQUIRE(diagnostic);
    REQUIRE(diagnostic.value->targets.size() == 1U);
    const auto& target = diagnostic.value->targets.front();
    REQUIRE(target.reparse_observation);
    REQUIRE(target.reparse_observation->nested_failure);
    const auto& nested_failure =
        *target.reparse_observation->nested_failure;
    CHECK(nested_failure.traversal_depth == 2U);
    CHECK(nested_failure.nested_ordinal == 2U);
    CHECK(nested_failure.reparse_tag_category ==
          windows::WindowsReparseTagCategory::mount_point);
    CHECK(nested_failure.expression_kind ==
          windows::WindowsReparseTargetExpressionKind::
              nt_object_manager_path);
    CHECK(nested_failure.reachability ==
          windows::WindowsReparseTargetReachability::target_cycle);
    CHECK(nested_failure.failure_phase ==
          windows::StockExternalTopologyFailurePhase::nested_reparse_decode);
    CHECK(nested_failure.directory);
    CHECK(nested_failure.native_error_category ==
          windows::WindowsReparseNativeErrorCategory::circular_dependency);
    CHECK(nested_failure.native_error == ERROR_CIRCULAR_DEPENDENCY);
    CHECK(nested_failure.private_link_path.filename() ==
          fixture.link().filename());

    REQUIRE(target.failure_witness);
    const auto& witness = *target.failure_witness;
    CHECK(witness.target_ordinal == 1U);
    CHECK(witness.traversal_depth == nested_failure.traversal_depth);
    CHECK(witness.nested_ordinal == nested_failure.nested_ordinal);
    CHECK(witness.reparse_tag_category ==
          nested_failure.reparse_tag_category);
    CHECK(witness.expression_kind == nested_failure.expression_kind);
    CHECK(witness.reachability == nested_failure.reachability);
    CHECK(witness.failure_phase == nested_failure.failure_phase);
    CHECK(witness.directory == nested_failure.directory);
    CHECK(witness.native_error_category ==
          nested_failure.native_error_category);
    CHECK(witness.private_witness_sha256.size() == 64U);

    const auto review = windows::review_stock_external_targets(
        fixture.source(), fixture.review_parent());
    INFO("review code=" << windows::to_string(review.code)
                         << " native=" << review.native_error);
    REQUIRE(review);
    REQUIRE(review.value->targets.size() == 1U);
    REQUIRE(review.value->targets.front().failure_witness);
    CHECK(review.value->targets.front().failure_witness->traversal_depth ==
          2U);
    CHECK(review.value->targets.front().failure_witness->nested_ordinal ==
          2U);
    CHECK(review.value->targets.front().failure_witness->failure_phase ==
          windows::StockExternalTopologyFailurePhase::nested_reparse_decode);

    const auto summary = windows::parse_stock_external_review_summary_v2(
        read_file(review.value->review_root /
                  windows::kStockExternalReviewSummaryLeaf));
    REQUIRE(summary);
    REQUIRE(summary.value->targets.size() == 1U);
    CHECK(summary.value->targets.front().failure_phase ==
          "nested_reparse_decode");
    CHECK(summary.value->targets.front().reachability == "target_cycle");
    CHECK(summary.value->targets.front().native_error_category ==
          "circular_dependency");

    const auto private_leaf = windows::stock_external_private_target_leaf(1U);
    REQUIRE(private_leaf);
    const auto private_target =
        windows::parse_stock_external_private_target_v2(
            read_file(review.value->review_root / *private_leaf));
    REQUIRE(private_target);
    CHECK(private_target.value->failure_phase == "nested_reparse_decode");
    CHECK(private_target.value->reachability == "target_cycle");
    CHECK(private_target.value->native_error_category ==
          "circular_dependency");
    CHECK(private_target.value->native_error == ERROR_CIRCULAR_DEPENDENCY);
    CHECK(private_target.value->witness_sha256 ==
          review.value->targets.front()
              .failure_witness->private_witness_sha256);
    CHECK(fs::last_write_time(fixture.source()) == source_before);
}
