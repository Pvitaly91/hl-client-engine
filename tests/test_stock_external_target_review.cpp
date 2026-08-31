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
#include <optional>
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
           (L"hlclient-external-target-review-test-" +
            std::to_wstring(::GetCurrentProcessId()) + L"-" +
            std::to_wstring(nonce));
}

void write_file(const fs::path& path, const std::string_view bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

void write_bytes(
    const fs::path& path, const std::span<const unsigned char> bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output.good());
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

void write_minimal_wad3(const fs::path& path)
{
    // Empty WAD3 directory: magic, zero lumps and a directory offset directly
    // after the 12-byte header. This is real, bounded GoldSrc asset structure,
    // not an extension-only stand-in.
    static constexpr std::array<unsigned char, 12U> bytes{
        'W', 'A', 'D', '3', 0U, 0U, 0U, 0U, 12U, 0U, 0U, 0U};
    write_bytes(path, bytes);
}

[[nodiscard]] std::vector<unsigned char> minimal_bsp30_bytes()
{
    std::vector<unsigned char> bytes(124U, 0U);
    bytes[0] = 30U;
    for (std::size_t lump = 0U; lump < 15U; ++lump) {
        const auto offset = 4U + lump * 8U;
        bytes[offset] = static_cast<unsigned char>(bytes.size());
    }
    return bytes;
}

void write_minimal_bsp30(const fs::path& path)
{
    // GoldSrc BSP v30 header with all 15 lumps empty. Each empty lump points
    // immediately after the complete 124-byte header.
    const auto bytes = minimal_bsp30_bytes();
    write_bytes(path, bytes);
}

void write_appmanifest(
    const fs::path& steamapps_root, const std::string_view app_id,
    const std::string_view install_directory)
{
    const std::string bytes =
        "\"AppState\"\r\n{\r\n\t\"appid\"\t\"" +
        std::string{app_id} + "\"\r\n\t\"installdir\"\t\"" +
        std::string{install_directory} + "\"\r\n}\r\n";
    write_file(steamapps_root / L"appmanifest_70.acf", bytes);
}

[[nodiscard]] std::string vdf_path(const fs::path& path)
{
    const auto value = path.lexically_normal().native();
    const int required = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    REQUIRE(required > 0);
    std::string utf8(static_cast<std::size_t>(required), '\0');
    REQUIRE(::WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), utf8.data(), required,
                nullptr, nullptr) == required);
    std::string escaped;
    for (const char item : utf8) {
        if (item == '\\' || item == '"') escaped.push_back('\\');
        escaped.push_back(item);
    }
    return escaped;
}

void write_libraryfolders(
    const fs::path& steamapps_root,
    const std::vector<std::pair<fs::path, bool>>& libraries)
{
    std::string bytes{"\"libraryfolders\"\r\n{\r\n"};
    for (std::size_t index = 0U; index < libraries.size(); ++index) {
        bytes += "\t\"" + std::to_string(index) + "\"\r\n\t{\r\n";
        bytes += "\t\t\"path\"\t\"" + vdf_path(libraries[index].first) +
                 "\"\r\n";
        bytes += "\t\t\"apps\"\r\n\t\t{\r\n";
        bytes += libraries[index].second ? "\t\t\t\"70\"\t\"1\"\r\n"
                                         : "\t\t\t\"71\"\t\"1\"\r\n";
        bytes += "\t\t}\r\n\t}\r\n";
    }
    bytes += "}\r\n";
    write_file(steamapps_root / L"libraryfolders.vdf", bytes);
}

[[nodiscard]] bool try_write_ads(
    const fs::path& path, const std::string_view bytes)
{
    const auto stream = fs::path{path.native() + L":review-fixture"};
    const HANDLE handle = ::CreateFileW(
        stream.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0U;
    const BOOL ok = ::WriteFile(
        handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
        nullptr);
    const BOOL flushed = ok != FALSE ? ::FlushFileBuffers(handle) : FALSE;
    static_cast<void>(::CloseHandle(handle));
    return ok != FALSE && flushed != FALSE &&
           written == static_cast<DWORD>(bytes.size());
}

[[nodiscard]] std::optional<std::size_t> named_ads_count(
    const fs::path& path)
{
    WIN32_FIND_STREAM_DATA data{};
    const HANDLE find =
        ::FindFirstStreamW(path.c_str(), FindStreamInfoStandard, &data, 0U);
    if (find == INVALID_HANDLE_VALUE) return std::nullopt;
    std::size_t count = 0U;
    DWORD error = ERROR_SUCCESS;
    do {
        if (std::wstring_view{data.cStreamName} != L"::$DATA") ++count;
    } while (::FindNextStreamW(find, &data) != FALSE);
    error = ::GetLastError();
    static_cast<void>(::FindClose(find));
    if (error != ERROR_HANDLE_EOF) return std::nullopt;
    return count;
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
    const auto print_bytes = static_cast<USHORT>(print.size() * sizeof(wchar_t));
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

struct Fixture final {
    Fixture() : root{unique_root()}
    {
        fs::create_directories(source_root());
        fs::create_directories(external_root());
        fs::create_directories(root / L"manual-artifacts");
        write_appmanifest(steamapps_root(), "70", "Half-Life");
        write_libraryfolders(
            steamapps_root(), {{steamapps_root().parent_path(), true}});
        write_file(source_root() / L"ordinary.txt", "source");
    }

    ~Fixture()
    {
        for (const auto& link : junctions) {
            static_cast<void>(::RemoveDirectoryW(link.c_str()));
        }
        std::error_code error;
        fs::remove_all(root, error);
    }

    [[nodiscard]] bool link_external()
    {
        const auto link = source_root() / L"shared-assets";
        if (!create_junction(link, external_root())) return false;
        junctions.push_back(link);
        return true;
    }

    [[nodiscard]] fs::path steamapps_root() const
    {
        return root / L"review-library" / L"steamapps";
    }

    [[nodiscard]] fs::path app_root() const
    {
        return steamapps_root() / L"common" / L"Half-Life";
    }

    [[nodiscard]] fs::path source_root() const
    {
        return app_root() / L"source";
    }

    [[nodiscard]] fs::path external_root() const
    {
        return app_root() / L"external-assets";
    }

    fs::path root;
    std::vector<fs::path> junctions;
};

[[nodiscard]] fs::path review_parent(const Fixture& fixture)
{
    return fixture.root / L"manual-artifacts" /
           L"stock-runtime-source-review";
}

} // namespace

TEST_CASE(
    "External target review accepts a bounded non-executable asset tree",
    "[windows][stock-runtime][external-target][review]")
{
    Fixture fixture;
    write_minimal_bsp30(
        fixture.external_root() / L"maps" / L"arena.bsp");
    write_minimal_wad3(
        fixture.external_root() / L"textures" / L"shared.wad");
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }

    CHECK_FALSE(fs::exists(review_parent(fixture)));
    const auto result = windows::review_stock_external_targets(
        fixture.source_root(), review_parent(fixture));
    INFO("review code=" << windows::to_string(result.code)
                         << " native=" << result.native_error);
    REQUIRE(result);
    CHECK(fs::is_directory(review_parent(fixture)));
    REQUIRE(result.value->targets.size() == 1U);
    const auto& target = result.value->targets.front();
    CHECK(result.value->all_targets_eligible);
    CHECK(target.eligible);
    CHECK(target.classification == windows::StockExternalTargetClassification::
                                       eligible_non_executable_asset_tree);
    CHECK(target.executable_count == 0U);
    CHECK(target.script_or_command_count == 0U);
    CHECK(target.mutable_state_count == 0U);
    CHECK(target.nested_link_count == 0U);
    CHECK(target.entry_count >= 5U);
    CHECK_FALSE(target.target_inventory_sha256.empty());
    CHECK_FALSE(result.value->review_set_sha256.empty());
    CHECK(fs::is_regular_file(
        result.value->review_root /
        windows::kStockExternalReviewRequestLeaf));
    CHECK(fs::is_regular_file(
        result.value->review_root /
        windows::kStockExternalReviewSummaryLeaf));
    const auto private_leaf = windows::stock_external_private_target_leaf(1U);
    REQUIRE(private_leaf);
    CHECK(fs::is_regular_file(result.value->review_root / *private_leaf));
}

TEST_CASE(
    "External target review leaves an unproven asset-only tree ineligible",
    "[windows][stock-runtime][external-target][review][provenance]")
{
    Fixture fixture;
    const auto unproven = fixture.root / L"unproven-assets";
    write_minimal_bsp30(unproven / L"maps" / L"arena.bsp");
    const auto link = fixture.source_root() / L"unproven-assets";
    if (!create_junction(link, unproven)) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    fixture.junctions.push_back(link);

    const auto result = windows::review_stock_external_targets(
        fixture.source_root(), review_parent(fixture));
    INFO("review code=" << windows::to_string(result.code)
                         << " native=" << result.native_error);
    REQUIRE(result);
    REQUIRE(result.value->targets.size() == 1U);
    CHECK_FALSE(result.value->all_targets_eligible);
    CHECK_FALSE(result.value->targets.front().eligible);
    CHECK(result.value->targets.front().classification ==
          windows::StockExternalTargetClassification::unknown);
}

TEST_CASE(
    "External target review requires exact AppID 70 application provenance",
    "[windows][stock-runtime][external-target][review][provenance][appmanifest]")
{
    SECTION("independently registered secondary app root") {
        Fixture fixture;
        const auto spoof_steamapps =
            fixture.root / L"spoof-library" / L"steamapps";
        const auto spoof_target = spoof_steamapps / L"common" /
                                  L"Half-Life" / L"external-assets";
        write_appmanifest(spoof_steamapps, "70", "Half-Life");
        write_libraryfolders(
            fixture.steamapps_root(),
            {{fixture.steamapps_root().parent_path(), true},
             {spoof_steamapps.parent_path(), true}});
        write_minimal_wad3(spoof_target / L"asset.wad");
        const auto link = fixture.source_root() / L"spoof-assets";
        if (!create_junction(link, spoof_target)) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        fixture.junctions.push_back(link);
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  eligible_non_executable_asset_tree);
    }

    SECTION("same lexical app name without registration is a spoof") {
        Fixture fixture;
        const auto spoof_target = fixture.root / L"spoof-library" /
                                  L"steamapps" / L"common" /
                                  L"Half-Life" / L"external-assets";
        write_minimal_wad3(spoof_target / L"asset.wad");
        const auto link = fixture.source_root() / L"spoof-assets";
        if (!create_junction(link, spoof_target)) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        fixture.junctions.push_back(link);
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK_FALSE(result.value->targets.front().eligible);
        CHECK(result.value->targets.front().classification !=
              windows::StockExternalTargetClassification::
                  eligible_non_executable_asset_tree);
    }

    SECTION("missing appmanifest") {
        Fixture fixture;
        REQUIRE(fs::remove(
            fixture.steamapps_root() / L"appmanifest_70.acf"));
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
        CHECK(result.value->targets.front().classification !=
              windows::StockExternalTargetClassification::
                  eligible_non_executable_asset_tree);
    }

    SECTION("wrong AppID") {
        Fixture fixture;
        write_appmanifest(fixture.steamapps_root(), "71", "Half-Life");
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
    }

    SECTION("wrong install directory") {
        Fixture fixture;
        write_appmanifest(fixture.steamapps_root(), "70", "OtherGame");
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
    }

    SECTION("conflicting duplicate manifest property") {
        Fixture fixture;
        write_file(
            fixture.steamapps_root() / L"appmanifest_70.acf",
            "\"AppState\"\r\n{\r\n\t\"appid\"\t\"70\"\r\n"
            "\t\"appid\"\t\"71\"\r\n"
            "\t\"installdir\"\t\"Half-Life\"\r\n}\r\n");
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
    }

    SECTION("nested manifest pair spoof") {
        Fixture fixture;
        write_file(
            fixture.steamapps_root() / L"appmanifest_70.acf",
            "\"AppState\"\r\n{\r\n\t\"Nested\"\r\n\t{\r\n"
            "\t\t\"appid\"\t\"70\"\r\n"
            "\t\t\"installdir\"\t\"Half-Life\"\r\n\t}\r\n}\r\n");
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
    }

    SECTION("direct manifest pair with nested conflicting pair") {
        Fixture fixture;
        write_file(
            fixture.steamapps_root() / L"appmanifest_70.acf",
            "\"AppState\"\r\n{\r\n\t\"appid\"\t\"70\"\r\n"
            "\t\"installdir\"\t\"Half-Life\"\r\n"
            "\t\"Nested\"\r\n\t{\r\n\t\t\"appid\"\t\"71\"\r\n"
            "\t}\r\n}\r\n");
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
    }

    SECTION("oversized appmanifest") {
        Fixture fixture;
        write_file(
            fixture.steamapps_root() / L"appmanifest_70.acf",
            std::string(65U * 1'024U, 'x'));
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
    }

    SECTION("hardlinked appmanifest") {
        Fixture fixture;
        const auto manifest =
            fixture.steamapps_root() / L"appmanifest_70.acf";
        const auto alias = fixture.root / L"appmanifest-alias.acf";
        if (::CreateHardLinkW(alias.c_str(), manifest.c_str(), nullptr) ==
            FALSE) {
            SKIP("Windows hardlink fixture capability is unavailable");
        }
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
    }

    SECTION("appmanifest ADS") {
        Fixture fixture;
        const auto manifest =
            fixture.steamapps_root() / L"appmanifest_70.acf";
        if (!try_write_ads(manifest, "hidden")) {
            SKIP("NTFS alternate-stream fixture capability is unavailable");
        }
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
    }
}

TEST_CASE(
    "External target review requires exact libraryfolders AppID 70 membership",
    "[windows][stock-runtime][external-target][review][provenance][libraryfolders]")
{
    const auto review_fixture = [](Fixture& fixture) {
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        return result.value->targets.front().eligible;
    };

    SECTION("missing libraryfolders") {
        Fixture fixture;
        REQUIRE(fs::remove(
            fixture.steamapps_root() / L"libraryfolders.vdf"));
        CHECK_FALSE(review_fixture(fixture));
    }

    SECTION("wrong application membership") {
        Fixture fixture;
        write_libraryfolders(
            fixture.steamapps_root(),
            {{fixture.steamapps_root().parent_path(), false}});
        CHECK_FALSE(review_fixture(fixture));
    }

    SECTION("duplicate application membership") {
        Fixture fixture;
        const auto root = vdf_path(fixture.steamapps_root().parent_path());
        write_file(
            fixture.steamapps_root() / L"libraryfolders.vdf",
            "\"libraryfolders\"\n{\n\"0\"\n{\n\"path\" \"" + root +
                "\"\n\"apps\"\n{\n\"70\" \"1\"\n"
                "\"70\" \"2\"\n}\n}\n}\n");
        CHECK_FALSE(review_fixture(fixture));
    }

    SECTION("duplicate library path") {
        Fixture fixture;
        const auto library = fixture.steamapps_root().parent_path();
        write_libraryfolders(
            fixture.steamapps_root(),
            {{library, true}, {library, true}});
        CHECK_FALSE(review_fixture(fixture));
    }

    SECTION("oversized libraryfolders") {
        Fixture fixture;
        write_file(
            fixture.steamapps_root() / L"libraryfolders.vdf",
            std::string(1U * 1'024U * 1'024U + 1U, 'x'));
        CHECK_FALSE(review_fixture(fixture));
    }

    SECTION("hardlinked libraryfolders") {
        Fixture fixture;
        const auto metadata =
            fixture.steamapps_root() / L"libraryfolders.vdf";
        const auto alias = fixture.root / L"libraryfolders-alias.vdf";
        if (::CreateHardLinkW(alias.c_str(), metadata.c_str(), nullptr) ==
            FALSE) {
            SKIP("Windows hardlink fixture capability is unavailable");
        }
        CHECK_FALSE(review_fixture(fixture));
    }

    SECTION("libraryfolders ADS") {
        Fixture fixture;
        const auto metadata =
            fixture.steamapps_root() / L"libraryfolders.vdf";
        if (!try_write_ads(metadata, "hidden")) {
            SKIP("NTFS alternate-stream fixture capability is unavailable");
        }
        CHECK_FALSE(review_fixture(fixture));
    }

    SECTION("secondary application root is not listed") {
        Fixture fixture;
        const auto secondary_steamapps =
            fixture.root / L"secondary-library" / L"steamapps";
        const auto secondary_target =
            secondary_steamapps / L"common" / L"Half-Life" / L"assets";
        write_appmanifest(secondary_steamapps, "70", "Half-Life");
        write_minimal_wad3(secondary_target / L"asset.wad");
        const auto link = fixture.source_root() / L"secondary-assets";
        if (!create_junction(link, secondary_target)) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        fixture.junctions.push_back(link);
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->targets.front().eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::unknown);
    }
}

TEST_CASE(
    "External target review detects disguised code scripts and mutable state",
    "[windows][stock-runtime][external-target][review][classification]")
{
    Fixture fixture;

    SECTION("renamed MZ content") {
        write_file(fixture.external_root() / L"texture.dat",
                   "MZnot-a-real-image");
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  contains_executable_code);
        CHECK(result.value->targets.front().executable_count == 1U);
    }

    SECTION("renamed script content") {
        write_file(fixture.external_root() / L"notes.dat",
                   "#!/usr/bin/env python\nprint('unsafe')\n");
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  contains_script_or_command);
        CHECK(result.value->targets.front().script_or_command_count == 1U);
    }

    SECTION("mutable user-state directory") {
        write_file(
            fixture.external_root() / L"save" / L"slot1.sav",
            "mutable state");
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  contains_mutable_user_state);
        CHECK(result.value->targets.front().mutable_state_count == 2U);
    }
}

TEST_CASE(
    "External target review detects renamed VBS JS and WSF after a long prefix",
    "[windows][stock-runtime][external-target][review][classification][script]")
{
    struct ScriptCase final {
        std::string_view label;
        std::string_view body;
    };
    static constexpr std::array scripts{
        ScriptCase{"VBS", "Set shell = CreateObject(\"WScript.Shell\")\r\n"},
        ScriptCase{"JS", "var shell = new ActiveXObject(\"WScript.Shell\");\r\n"},
        ScriptCase{
            "WSF",
            "<job><script language=\"VBScript\">MsgBox \"unsafe\""
            "</script></job>\r\n"},
    };
    for (std::size_t index = 0U; index < scripts.size(); ++index) {
        Fixture fixture;
        std::string bytes(5U * 1'024U, ' ');
        bytes.append(scripts[index].body);
        write_file(
            fixture.external_root() /
                (L"renamed-script-" + std::to_wstring(index) + L".wad"),
            bytes);
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        INFO("script format=" << scripts[index].label);
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  contains_script_or_command);
        CHECK(result.value->targets.front().script_or_command_count == 1U);
    }
}

TEST_CASE(
    "External target review detects renamed installer and package signatures",
    "[windows][stock-runtime][external-target][review][classification][package]")
{
    struct SignatureCase final {
        std::string_view label;
        std::vector<unsigned char> bytes;
    };
    const std::array signatures{
        SignatureCase{
            "OLE compound file",
            {0xd0U, 0xcfU, 0x11U, 0xe0U, 0xa1U, 0xb1U, 0x1aU, 0xe1U}},
        SignatureCase{"CAB", {'M', 'S', 'C', 'F'}},
        SignatureCase{"CHM", {'I', 'T', 'S', 'F'}},
        SignatureCase{"ZIP package", {'P', 'K', 0x03U, 0x04U}},
    };
    for (std::size_t index = 0U; index < signatures.size(); ++index) {
        Fixture fixture;
        write_bytes(
            fixture.external_root() /
                (L"renamed-package-" + std::to_wstring(index) + L".wad"),
            signatures[index].bytes);
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        INFO("package signature=" << signatures[index].label);
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  contains_executable_code);
        CHECK(result.value->targets.front().executable_count == 1U);
    }
}

TEST_CASE(
    "External target review rejects truncated and incoherent asset headers",
    "[windows][stock-runtime][external-target][review][asset-structure]")
{
    struct AssetCase final {
        std::wstring_view leaf;
        std::vector<unsigned char> bytes;
    };
    auto bad_bsp_lump = minimal_bsp30_bytes();
    bad_bsp_lump[4U] = 250U;
    bad_bsp_lump[8U] = 1U;
    auto nonempty_bsp_lump = minimal_bsp30_bytes();
    nonempty_bsp_lump[8U] = 1U;
    nonempty_bsp_lump.push_back(0x42U);
    const std::array assets{
        AssetCase{L"truncated.bsp", {30U, 0U, 0U, 0U}},
        AssetCase{L"out-of-range-lump.bsp", std::move(bad_bsp_lump)},
        AssetCase{
            L"non-empty-unparsed-lump.bsp", std::move(nonempty_bsp_lump)},
        AssetCase{
            L"truncated.mdl", {'I', 'D', 'S', 'T', 10U, 0U, 0U, 0U}},
        AssetCase{
            L"truncated.spr", {'I', 'D', 'S', 'P', 2U, 0U, 0U, 0U}},
        AssetCase{
            L"missing-wad-directory.wad",
            {'W', 'A', 'D', '3', 1U, 0U, 0U, 0U, 12U, 0U, 0U, 0U}},
    };
    for (std::size_t index = 0U; index < assets.size(); ++index) {
        Fixture fixture;
        const auto& asset = assets[index];
        write_bytes(fixture.external_root() / asset.leaf, asset.bytes);
        REQUIRE(fixture.link_external());
        INFO("asset case=" << index);
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK_FALSE(result.value->targets.front().eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::unknown);
    }
}

TEST_CASE(
    "External target review detects embedded platform signatures in BSP payloads",
    "[windows][stock-runtime][external-target][review][classification][embedded-signature]")
{
    struct SignatureCase final {
        std::string_view label;
        std::vector<unsigned char> bytes;
    };
    const std::array signatures{
        SignatureCase{"Mach-O big endian", {0xfeU, 0xedU, 0xfaU, 0xceU}},
        SignatureCase{"Mach-O little endian", {0xceU, 0xfaU, 0xedU, 0xfeU}},
        SignatureCase{"Mach-O fat big endian", {0xcaU, 0xfeU, 0xbaU, 0xbeU}},
        SignatureCase{"Mach-O fat little endian", {0xbeU, 0xbaU, 0xfeU, 0xcaU}},
        SignatureCase{
            "Windows shortcut",
            {0x4cU, 0x00U, 0x00U, 0x00U, 0x01U, 0x14U, 0x02U,
             0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xc0U, 0x00U,
             0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x46U}},
        SignatureCase{"gzip", {0x1fU, 0x8bU}},
    };
    for (std::size_t index = 0U; index < signatures.size(); ++index) {
        Fixture fixture;
        auto bytes = minimal_bsp30_bytes();
        bytes[8U] = static_cast<unsigned char>(signatures[index].bytes.size());
        bytes.insert(
            bytes.end(), signatures[index].bytes.begin(),
            signatures[index].bytes.end());
        write_bytes(
            fixture.external_root() /
                (L"embedded-platform-" + std::to_wstring(index) + L".bsp"),
            bytes);
        REQUIRE(fixture.link_external());
        INFO("embedded signature=" << signatures[index].label);
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  contains_executable_code);
        CHECK(result.value->targets.front().executable_count == 1U);
    }
}

TEST_CASE(
    "External target review rejects payloads appended after a valid BSP header",
    "[windows][stock-runtime][external-target][review][asset-polyglot]")
{
    SECTION("unlisted command marker") {
        Fixture fixture;
        auto bytes = minimal_bsp30_bytes();
        constexpr std::string_view payload{
            "@%windir%\\system32\\hlclient-test-marker.exe\r\n"};
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        write_bytes(fixture.external_root() / L"polyglot.bsp", bytes);
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK_FALSE(result.value->targets.front().eligible);
        CHECK(result.value->targets.front().classification !=
              windows::StockExternalTargetClassification::
                  eligible_non_executable_asset_tree);
    }

    SECTION("embedded package signature") {
        Fixture fixture;
        auto bytes = minimal_bsp30_bytes();
        static constexpr std::array<unsigned char, 8U> package{
            0xd0U, 0xcfU, 0x11U, 0xe0U, 0xa1U, 0xb1U, 0x1aU, 0xe1U};
        bytes.insert(bytes.end(), package.begin(), package.end());
        write_bytes(fixture.external_root() / L"package-polyglot.bsp", bytes);
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK_FALSE(result.value->targets.front().eligible);
        CHECK(result.value->targets.front().classification !=
              windows::StockExternalTargetClassification::
                  eligible_non_executable_asset_tree);
    }
}

TEST_CASE(
    "External target review rejects a reparse-backed source root",
    "[windows][stock-runtime][external-target][review][root-reparse]")
{
    Fixture fixture;
    write_minimal_wad3(fixture.external_root() / L"asset.wad");
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto source_alias = fixture.root / L"source-root-alias";
    if (!create_junction(source_alias, fixture.source_root())) {
        SKIP("Windows root-junction fixture capability is unavailable");
    }
    fixture.junctions.push_back(source_alias);

    const auto result = windows::review_stock_external_targets(
        source_alias, review_parent(fixture));
    CHECK_FALSE(result);
    CHECK(result.code == windows::StockExternalReviewErrorCode::source_invalid);
}

TEST_CASE(
    "External target review supports a regular-file link when capability exists",
    "[windows][stock-runtime][external-target][review][file-link]")
{
    Fixture fixture;
    const auto target = fixture.external_root() / L"single-asset.bsp";
    const auto link = fixture.source_root() / L"single-asset.bsp";
    write_minimal_bsp30(target);
    if (::CreateSymbolicLinkW(
            link.c_str(), target.c_str(),
            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) == FALSE) {
        SKIP("Windows file-symlink capability is unavailable");
    }
    const auto result = windows::review_stock_external_targets(
        fixture.source_root(), review_parent(fixture));
    INFO("review code=" << windows::to_string(result.code)
                         << " native=" << result.native_error);
    REQUIRE(result);
    REQUIRE(result.value->targets.size() == 1U);
    CHECK(result.value->all_targets_eligible);
    CHECK(result.value->targets.front().classification ==
          windows::StockExternalTargetClassification::
              eligible_non_executable_asset_tree);
    static_cast<void>(::DeleteFileW(link.c_str()));
}

TEST_CASE(
    "External target review rejects nested links and bounded overflows",
    "[windows][stock-runtime][external-target][review][bounds]")
{
    SECTION("nested contained link") {
        Fixture fixture;
        const auto nested_target = fixture.external_root() / L"real";
        write_minimal_wad3(nested_target / L"asset.wad");
        const auto nested_link =
            fixture.external_root() / L"nested-link";
        if (!create_junction(nested_link, nested_target)) {
            SKIP("Nested Windows junction fixture capability is unavailable");
        }
        fixture.junctions.push_back(nested_link);
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  nested_external_link);
        CHECK(result.value->targets.front().nested_link_count == 1U);
    }

    SECTION("entry bound") {
        Fixture fixture;
        write_minimal_bsp30(fixture.external_root() / L"one.bsp");
        write_minimal_bsp30(fixture.external_root() / L"two.bsp");
        REQUIRE(fixture.link_external());
        windows::StockResearchCopyLimits limits;
        limits.maximum_entries = 2U;
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture), limits);
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  content_limit_exceeded);
    }

    SECTION("byte bound") {
        Fixture fixture;
        write_file(fixture.external_root() / L"large.bsp",
                   "0123456789abcdef");
        REQUIRE(fixture.link_external());
        windows::StockResearchCopyLimits limits;
        limits.maximum_file_bytes = 8U;
        limits.maximum_total_bytes = 8U;
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture), limits);
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  content_limit_exceeded);
    }
}

TEST_CASE(
    "External target review rejects ADS escaped links and cycles",
    "[windows][stock-runtime][external-target][review][hostile-topology]")
{
    SECTION("target ADS") {
        Fixture fixture;
        const auto asset =
            fixture.external_root() / L"maps" / L"arena.bsp";
        write_minimal_bsp30(asset);
        if (!try_write_ads(asset, "hidden")) {
            SKIP("NTFS alternate-stream fixture capability is unavailable");
        }
        const auto streams = named_ads_count(asset);
        REQUIRE(streams);
        REQUIRE(*streams == 1U);
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        INFO("review code=" << windows::to_string(result.code)
                             << " native=" << result.native_error);
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK_FALSE(result.value->all_targets_eligible);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  unsupported_reparse_topology);
    }

    SECTION("nested escaped link") {
        Fixture fixture;
        const auto escaped = fixture.root / L"escaped-assets";
        write_file(escaped / L"outside.wad", "outside");
        const auto nested =
            fixture.external_root() / L"escaped-link";
        if (!create_junction(nested, escaped)) {
            SKIP("Nested Windows junction fixture capability is unavailable");
        }
        fixture.junctions.push_back(nested);
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        INFO("review code=" << windows::to_string(result.code)
                             << " native=" << result.native_error);
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  nested_external_link);
        CHECK(result.value->targets.front().nested_link_count == 1U);
    }

    SECTION("nested cycle") {
        Fixture fixture;
        write_minimal_wad3(fixture.external_root() / L"asset.wad");
        const auto cycle = fixture.external_root() / L"cycle";
        if (!create_junction(cycle, fixture.external_root())) {
            SKIP("Cyclic Windows junction fixture capability is unavailable");
        }
        fixture.junctions.push_back(cycle);
        REQUIRE(fixture.link_external());
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        INFO("review code=" << windows::to_string(result.code)
                             << " native=" << result.native_error);
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  nested_external_link);
        CHECK(result.value->targets.front().nested_link_count == 1U);
    }
}

TEST_CASE(
    "External target review rejects another Steam application and workshop",
    "[windows][stock-runtime][external-target][review][path-policy]")
{
    SECTION("another Steam application") {
        Fixture fixture;
        const auto source = fixture.root / L"steamapps" / L"common" /
                            L"Half-Life";
        const auto target = fixture.root / L"steamapps" / L"common" /
                            L"OtherGame";
        write_file(source / L"ordinary.txt", "source");
        write_file(target / L"asset.wad", "asset");
        const auto link = source / L"shared-assets";
        if (!create_junction(link, target)) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        fixture.junctions.push_back(link);
        const auto result = windows::review_stock_external_targets(
            source, review_parent(fixture));
        INFO("review code=" << windows::to_string(result.code)
                             << " native=" << result.native_error);
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  another_application_tree);
    }

    SECTION("workshop tree") {
        Fixture fixture;
        const auto target =
            fixture.root / L"steamapps" / L"workshop" / L"content" /
            L"70";
        write_file(target / L"asset.wad", "asset");
        const auto link = fixture.source_root() / L"workshop-assets";
        if (!create_junction(link, target)) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        fixture.junctions.push_back(link);
        const auto result = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        INFO("review code=" << windows::to_string(result.code)
                             << " native=" << result.native_error);
        REQUIRE(result);
        REQUIRE(result.value->targets.size() == 1U);
        CHECK(result.value->targets.front().classification ==
              windows::StockExternalTargetClassification::
                  another_application_tree);
    }
}
