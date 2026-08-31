#include <hlclient/platform/windows/stock_source_eligibility.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

namespace windows = hlclient::platform::windows;

struct Options final {
    std::filesystem::path source_root;
    std::filesystem::path app_manifest;
    std::uint64_t expected_app_build{
        windows::kDefaultExpectedStockSteamBuild};
};

[[nodiscard]] bool parse_decimal(
    const std::wstring_view text, std::uint64_t& value) noexcept
{
    if (text.empty() || text.size() > 20U) return false;
    std::string narrow;
    try {
        narrow.reserve(text.size());
        for (const wchar_t character : text) {
            if (character < L'0' || character > L'9') return false;
            narrow.push_back(static_cast<char>(character));
        }
    } catch (...) {
        return false;
    }
    const auto parsed = std::from_chars(
        narrow.data(), narrow.data() + narrow.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == narrow.data() + narrow.size();
}

[[nodiscard]] std::optional<Options> parse_options(
    const int argc, wchar_t** const argv)
{
    try {
        Options options;
        bool expected_seen = false;
        for (int index = 1; index < argc; ++index) {
            const std::wstring_view argument{argv[index]};
            if (argument == L"--source-root") {
                if (++index >= argc || !options.source_root.empty() ||
                    argv[index][0] == L'\0') {
                    return std::nullopt;
                }
                options.source_root = argv[index];
            } else if (argument == L"--app-manifest") {
                if (++index >= argc || !options.app_manifest.empty() ||
                    argv[index][0] == L'\0') {
                    return std::nullopt;
                }
                options.app_manifest = argv[index];
            } else if (argument == L"--expected-app-build") {
                if (++index >= argc || expected_seen ||
                    !parse_decimal(argv[index], options.expected_app_build) ||
                    options.expected_app_build == 0U) {
                    return std::nullopt;
                }
                expected_seen = true;
            } else {
                return std::nullopt;
            }
        }
        if (options.source_root.empty() || options.app_manifest.empty()) {
            return std::nullopt;
        }
        return options;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] const char* public_profile(
    const windows::StockSourceComponentProfileStatus status) noexcept
{
    return status == windows::StockSourceComponentProfileStatus::valid
               ? "valid"
               : "invalid";
}

void print_summary(const windows::StockSourceEligibilitySummary& summary)
{
    std::cout << "[stock-source] topology="
              << (summary.topology_safe ? "safe" : "unsafe") << '\n'
              << "[stock-source] escaped-targets="
              << summary.escaped_target_count << '\n'
              << "[stock-source] dangling-targets="
              << summary.dangling_target_count << '\n'
              << "[stock-source] unsupported-tags="
              << summary.unsupported_tag_count << '\n'
              << "[stock-source] ads="
              << summary.alternate_data_stream_count << '\n'
              << "[stock-source] client-profile="
              << public_profile(summary.client_profile) << '\n'
              << "[stock-source] server-profile="
              << public_profile(summary.server_profile) << '\n'
              << "[stock-source] app-profile="
              << public_profile(summary.app_profile) << '\n'
              << "[stock-source] research-copy-eligible="
              << (summary.research_copy_eligible ? "true" : "false")
              << '\n'
              << "[stock-source] result="
              << windows::to_string(summary.status) << '\n';
}

} // namespace

int wmain(const int argc, wchar_t** const argv)
{
    const auto options = parse_options(argc, argv);
    if (!options) {
        std::cerr << "[stock-source] result=invalid_argument\n";
        return 2;
    }

    windows::StockSourceEligibilityOptions validation_options;
    validation_options.expected_app_build = options->expected_app_build;
    const auto result = windows::validate_stock_runtime_candidate_source(
        options->source_root, options->app_manifest, validation_options);
    if (!result) {
        std::cerr << "[stock-source] result="
                  << windows::to_string(result.status) << '\n';
        return result.status ==
                       windows::StockSourceEligibilityStatus::invalid_argument
                   ? 2
                   : 1;
    }
    print_summary(*result.summary);
    return result.summary->research_copy_eligible ? 0 : 1;
}
