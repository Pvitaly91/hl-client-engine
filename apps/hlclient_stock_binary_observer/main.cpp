#include <hlclient/platform/windows/binary_identity.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

namespace windows = hlclient::platform::windows;

[[nodiscard]] std::optional<std::filesystem::path> value_after(
    const int argc,
    wchar_t** argv,
    const std::wstring_view option)
{
    std::optional<std::filesystem::path> result;
    for (int index = 1; index < argc; ++index) {
        if (std::wstring_view{argv[index]} != option) {
            continue;
        }
        if (result || index + 1 >= argc || argv[index + 1][0] == L'\0') {
            return std::nullopt;
        }
        result = std::filesystem::path{argv[++index]};
    }
    return result;
}

} // namespace

int wmain(const int argc, wchar_t** argv)
{
    if (argc != 7) {
        std::cerr << "Usage: hlclient_stock_binary_observer --client <hl.exe> "
                     "--server-launcher <hlds.exe> --app-manifest "
                     "<appmanifest_70.acf>\n";
        return 2;
    }
    const auto client = value_after(argc, argv, L"--client");
    const auto server = value_after(argc, argv, L"--server-launcher");
    const auto manifest = value_after(argc, argv, L"--app-manifest");
    if (!client || !server || !manifest) {
        std::cerr << "binary-profile=invalid\nfailure-category=invalid-arguments\n";
        return 2;
    }
    const auto result = windows::observe_required_stock_binary_profile(
        *client, *server, *manifest);
    if (!result) {
        std::cout << "binary-profile=invalid\n"
                  << "failure-category=" << windows::to_string(result.code)
                  << "\n"
                  << "binary-error=" << windows::to_string(result.binary_error)
                  << "\n"
                  << "manifest-error=" << windows::to_string(result.manifest_error)
                  << "\n";
        return 1;
    }
    const auto& observed = *result.observation;
    std::cout << "binary-profile=valid\n"
              << "client-file-version="
              << windows::to_string(observed.client_file_version) << "\n"
              << "client-pe-machine="
              << windows::to_string(observed.client_machine) << "\n"
              << "client-signature=valid\n"
              << "client-profile-fingerprint="
              << observed.client_profile_fingerprint << "\n"
              << "server-launcher-version="
              << windows::to_string(observed.server_launcher_version) << "\n"
              << "server-pe-machine="
              << windows::to_string(observed.server_machine) << "\n"
              << "server-signature=valid\n"
              << "server-profile-fingerprint="
              << observed.server_profile_fingerprint << "\n"
              << "steam-app-id=" << observed.steam_app_id << "\n"
              << "steam-build-id=" << observed.steam_build_id << "\n"
              << "evidence-status=observed\n";
    return 0;
}
