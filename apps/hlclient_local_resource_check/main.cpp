#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/resource_consistency/prepared_local_resource_consistency_provider.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::string game_directory{"valve"};
    bool check_consistency_provider{false};
};

[[nodiscard]] std::optional<std::string> narrow_printable_ascii(
    const std::wstring_view value)
{
    std::string result;
    try {
        result.reserve(value.size());
    } catch (...) {
        return std::nullopt;
    }
    for (const wchar_t character : value) {
        if (character < 0x20 || character > 0x7e) {
            return std::nullopt;
        }
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int argument_count,
    wchar_t* arguments[])
{
    Options options;
    bool game_seen = false;
    bool check_seen = false;

    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument == L"--check-consistency-provider") {
            if (check_seen) {
                return std::nullopt;
            }
            check_seen = true;
            options.check_consistency_provider = true;
            continue;
        }
        if (argument != L"--basedir" && argument != L"--game") {
            return std::nullopt;
        }
        if (index + 1 >= argument_count) {
            return std::nullopt;
        }

        const std::wstring_view value{arguments[++index]};
        if (value.empty()) {
            return std::nullopt;
        }
        if (argument == L"--basedir") {
            if (options.base_directory) {
                return std::nullopt;
            }
            options.base_directory = std::filesystem::path{value};
            continue;
        }

        if (game_seen) {
            return std::nullopt;
        }
        game_seen = true;
        auto game = narrow_printable_ascii(value);
        if (!game) {
            return std::nullopt;
        }
        options.game_directory = std::move(*game);
    }

    if (!options.base_directory || !options.check_consistency_provider) {
        return std::nullopt;
    }
    return options;
}

void print_usage()
{
    std::cerr
        << "Usage: hlclient_local_resource_check --basedir <Half-Life root> "
           "[--game <directory>] --check-consistency-provider\n";
}

} // namespace

int wmain(const int argument_count, wchar_t* arguments[])
{
    const auto options = parse_options(argument_count, arguments);
    if (!options) {
        print_usage();
        return 2;
    }

    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        *options->base_directory,
        options->game_directory);
    if (!roots) {
        const auto code =
            roots.error
                ? hlclient::local_resources::to_string(roots.error->code)
                : std::string_view{"io_error"};
        std::cerr << "[local-resource] root validation failed: " << code
                  << '\n';
        return 1;
    }

    const auto root_count = roots.roots->size();
    auto prepared = hlclient::resource_consistency::
        PreparedLocalResourceConsistencyProvider::prepare(
            std::move(*roots.roots));
    if (!prepared) {
        const auto code =
            prepared.error
                ? hlclient::resource_consistency::to_string(
                      prepared.error->code)
                : std::string_view{"provider_error"};
        std::cerr << "[local-resource] provider check failed: " << code
                  << '\n';
        return 1;
    }

    std::cout << "[local-resource] roots validated: count=" << root_count
              << '\n';
    std::cout << "[local-resource] target resolved\n";
    std::cout << "[local-resource] consistency material ready: byte-count="
              << prepared.provider->byte_count()
              << ", opaque-bytes=" << prepared.provider->opaque_byte_count()
              << '\n';
    std::cout << "[local-resource] no writes performed\n";
    return 0;
}
