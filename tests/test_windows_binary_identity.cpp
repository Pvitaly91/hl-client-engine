#include <hlclient/platform/windows/binary_identity.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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
        std::wstring buffer(32'768U, L'\0');
        const DWORD length = ::GetTempPathW(
            static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0U || length >= buffer.size()) return;
        buffer.resize(length);
        parent_ = fs::path{std::move(buffer)}.lexically_normal();
        static std::atomic_uint32_t ordinal{0U};
        for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
            const auto name = L"hlclient-stock-binary-identity-test-" +
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
                L"hlclient-stock-binary-identity-test-")) {
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

[[nodiscard]] fs::path sibling(const wchar_t* name)
{
    std::wstring module(32'768U, L'\0');
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    REQUIRE(size > 0U);
    REQUIRE(size < module.size());
    module.resize(size);
    return fs::path{std::move(module)}.parent_path() / name;
}

[[nodiscard]] bool write_text(
    const fs::path& path,
    const std::string_view text)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    return static_cast<bool>(output);
}

[[nodiscard]] windows::WindowsBinaryObservationPolicy project_policy() noexcept
{
    return {windows::AuthenticodePolicy::not_required_for_project_owned_binary,
            false};
}

[[nodiscard]] windows::WindowsBinaryIdentity synthetic_identity(
    const windows::WindowsFileVersion version,
    const char fingerprint)
{
    windows::WindowsBinaryIdentity identity;
    identity.file_version = version;
    identity.pe_machine = windows::WindowsPeMachine::x86;
    identity.authenticode_valid = true;
    identity.anonymized_profile_fingerprint.assign(64U, fingerprint);
    return identity;
}

} // namespace

TEST_CASE("Windows binary observation is stable and never exposes a raw digest",
          "[platform][windows][stock-runtime][binary-identity]")
{
    const auto executable = sibling(L"hlclient_stock_runtime_fake_server.exe");
    const auto first = windows::observe_windows_binary_identity(
        executable, windows::kMaximumObservedExecutableBytes, project_policy());
    const auto second = windows::observe_windows_binary_identity(
        executable, windows::kMaximumObservedExecutableBytes, project_policy());
    REQUIRE(first);
    REQUIRE(second);
    CHECK(windows::same_windows_file_identity(*first.identity, *second.identity));
    REQUIRE(first.identity->anonymized_profile_fingerprint.size() == 64U);
    CHECK(std::ranges::all_of(
        first.identity->anonymized_profile_fingerprint,
        [](const char value) {
            return (value >= '0' && value <= '9') ||
                   (value >= 'a' && value <= 'f');
        }));

    const auto unsigned_required = windows::observe_windows_binary_identity(
        executable, windows::kMaximumObservedExecutableBytes,
        {windows::AuthenticodePolicy::required, false});
    CHECK_FALSE(unsigned_required);
    CHECK(unsigned_required.code ==
          windows::WindowsBinaryIdentityErrorCode::authenticode_invalid);
}

TEST_CASE("Windows binary observation rejects hardlinks ADS and reparses",
          "[platform][windows][stock-runtime][binary-identity][unsafe-path]")
{
    ExactTemporaryDirectory temporary;
    REQUIRE(temporary.valid());
    const auto executable = sibling(L"hlclient_stock_runtime_fake_server.exe");

    const auto hardlinked = temporary.path() / L"hardlinked.exe";
    const auto second_link = temporary.path() / L"second-link.exe";
    REQUIRE(::CopyFileW(executable.c_str(), hardlinked.c_str(), TRUE) != FALSE);
    REQUIRE(::CreateHardLinkW(second_link.c_str(), hardlinked.c_str(), nullptr) !=
            FALSE);
    const auto hardlink_result = windows::observe_windows_binary_identity(
        hardlinked, windows::kMaximumObservedExecutableBytes, project_policy());
    CHECK_FALSE(hardlink_result);
    CHECK(hardlink_result.code ==
          windows::WindowsBinaryIdentityErrorCode::hardlink_rejected);

    const auto streamed = temporary.path() / L"streamed.exe";
    REQUIRE(::CopyFileW(executable.c_str(), streamed.c_str(), TRUE) != FALSE);
    const auto stream_name = streamed.wstring() + L":hlclient-test";
    const HANDLE stream = ::CreateFileW(
        stream_name.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(stream != INVALID_HANDLE_VALUE);
    DWORD written = 0U;
    const std::byte marker{0x2a};
    REQUIRE(::WriteFile(stream, &marker, 1U, &written, nullptr) != FALSE);
    REQUIRE(written == 1U);
    REQUIRE(::CloseHandle(stream) != FALSE);
    const auto stream_result = windows::observe_windows_binary_identity(
        streamed, windows::kMaximumObservedExecutableBytes, project_policy());
    CHECK_FALSE(stream_result);
    CHECK(stream_result.code ==
          windows::WindowsBinaryIdentityErrorCode::alternate_data_stream);
    CHECK(windows::observe_windows_binary_identity(
              fs::path{stream_name}, windows::kMaximumObservedExecutableBytes,
              project_policy()).code ==
          windows::WindowsBinaryIdentityErrorCode::alternate_data_stream);

    const auto copied = temporary.path() / L"copied.exe";
    const auto linked = temporary.path() / L"linked.exe";
    REQUIRE(::CopyFileW(executable.c_str(), copied.c_str(), TRUE) != FALSE);
    if (::CreateSymbolicLinkW(
            linked.c_str(), copied.c_str(),
            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE) {
        const auto reparse_result = windows::observe_windows_binary_identity(
            linked, windows::kMaximumObservedExecutableBytes, project_policy());
        CHECK_FALSE(reparse_result);
        CHECK(reparse_result.code ==
              windows::WindowsBinaryIdentityErrorCode::reparse_point);
    } else {
        WARN("Symbolic-link capability unavailable; reparse assertion skipped");
    }
}

TEST_CASE("Steam appmanifest parser is bounded exact and duplicate rejecting",
          "[platform][windows][stock-runtime][binary-identity][appmanifest]")
{
    ExactTemporaryDirectory temporary;
    REQUIRE(temporary.valid());
    const auto manifest = temporary.path() / L"appmanifest_70.acf";
    REQUIRE(write_text(
        manifest,
        "\"AppState\"\n{\n\"appid\" \"70\"\n"
        "\"buildid\" \"15961492\"\n}\n"));
    const auto valid = windows::observe_steam_app_manifest_70(manifest);
    REQUIRE(valid);
    CHECK(valid.observation->app_id == 70U);
    CHECK(valid.observation->build_id == 15'961'492U);

    REQUIRE(write_text(
        manifest,
        "\"AppState\"\n{\n\"appid\" \"71\"\n"
        "\"buildid\" \"15961492\"\n}\n"));
    CHECK(windows::observe_steam_app_manifest_70(manifest).code ==
          windows::SteamAppManifestErrorCode::unexpected_app_id);

    REQUIRE(write_text(
        manifest,
        "\"AppState\"\n{\n\"appid\" \"70\"\n\"appid\" \"70\"\n"
        "\"buildid\" \"15961492\"\n}\n"));
    CHECK(windows::observe_steam_app_manifest_70(manifest).code ==
          windows::SteamAppManifestErrorCode::duplicate_field);

    REQUIRE(write_text(
        manifest,
        "\"AppState\"\n{\n\"appid\" \"70\"\n"
        "\"buildid\" \"15961493\"\n}\n"));
    CHECK(windows::observe_steam_app_manifest_70(manifest).code ==
          windows::SteamAppManifestErrorCode::unexpected_build_id);
}

TEST_CASE("Stock profile validation rejects each mismatched trusted field",
          "[platform][windows][stock-runtime][binary-identity][stock-profile]")
{
    auto client = synthetic_identity({1U, 1U, 1U, 1U}, 'a');
    auto server = synthetic_identity({4U, 1U, 1U, 1U}, 'b');
    windows::SteamAppManifestObservation manifest;
    manifest.app_id = 70U;
    manifest.build_id = 15'961'492U;
    REQUIRE(windows::validate_required_stock_binary_profile_observations(
        client, server, manifest));

    client.file_version = windows::WindowsFileVersion{1U, 1U, 1U, 2U};
    CHECK(windows::validate_required_stock_binary_profile_observations(
              client, server, manifest).code ==
          windows::StockBinaryProfileErrorCode::client_version_mismatch);
    client.file_version = windows::WindowsFileVersion{1U, 1U, 1U, 1U};
    client.authenticode_valid = false;
    CHECK(windows::validate_required_stock_binary_profile_observations(
              client, server, manifest).code ==
          windows::StockBinaryProfileErrorCode::client_signature_invalid);
    client.authenticode_valid = true;

    server.file_version = windows::WindowsFileVersion{4U, 1U, 1U, 2U};
    CHECK(windows::validate_required_stock_binary_profile_observations(
              client, server, manifest).code ==
          windows::StockBinaryProfileErrorCode::server_version_mismatch);
    server.file_version = windows::WindowsFileVersion{4U, 1U, 1U, 1U};
    server.authenticode_valid = false;
    CHECK(windows::validate_required_stock_binary_profile_observations(
              client, server, manifest).code ==
          windows::StockBinaryProfileErrorCode::server_signature_invalid);
    server.authenticode_valid = true;

    manifest.build_id = 15'961'493U;
    CHECK(windows::validate_required_stock_binary_profile_observations(
              client, server, manifest).code ==
          windows::StockBinaryProfileErrorCode::app_manifest_invalid);
}
