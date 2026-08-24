#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::string game_directory{"valve"};
    std::optional<std::string> virtual_map;
    bool resolve_textures{false};
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
    bool resolve_seen = false;
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument == L"--resolve-textures") {
            if (resolve_seen) {
                return std::nullopt;
            }
            resolve_seen = true;
            options.resolve_textures = true;
            continue;
        }
        if (argument != L"--basedir" && argument != L"--game" &&
            argument != L"--map") {
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
        auto narrow = narrow_printable_ascii(value);
        if (!narrow) {
            return std::nullopt;
        }
        if (argument == L"--game") {
            if (game_seen) {
                return std::nullopt;
            }
            game_seen = true;
            options.game_directory = std::move(*narrow);
        } else {
            if (options.virtual_map) {
                return std::nullopt;
            }
            options.virtual_map = std::move(*narrow);
        }
    }
    if (!options.base_directory || !options.virtual_map ||
        !options.resolve_textures) {
        return std::nullopt;
    }
    return options;
}

void print_usage()
{
    std::cerr
        << "Usage: hlclient_world_texture_check --basedir <Half-Life root> "
           "[--game <directory>] --map <maps/name.bsp> --resolve-textures\n";
}

[[nodiscard]] bool local_source_terminal(
    const hlclient::local_assets::LocalAssetSourceOpenState state) noexcept
{
    using State = hlclient::local_assets::LocalAssetSourceOpenState;
    return state == State::source_ready || state == State::cancelled ||
        state == State::timed_out || state == State::failed;
}

[[nodiscard]] std::optional<hlclient::local_assets::LocalAssetSource>
open_map_source(
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>& environment,
    hlclient::local_resources::LocalResourceLocator locator)
{
    hlclient::local_assets::LocalAssetSourceOpenLimits limits;
    limits.maximum_source_bytes =
        hlclient::goldsrc::bsp::kGoldSrcBspDefaultMaximumSourceBytes;
    limits.maximum_chunks_per_update = 1U;
    limits.maximum_open_sources = 1U;

    hlclient::local_assets::LocalAssetSourceOpener opener;
    auto started = opener.begin(locator, environment, limits);
    if (!started) {
        const auto code = started.error
                              ? hlclient::local_assets::to_string(
                                    started.error->code)
                              : std::string_view{"source_open_failed"};
        std::cerr << "map-source-open=" << code << '\n';
        return std::nullopt;
    }

    auto& operation = *started.operation;
    constexpr std::size_t maximum_updates = 2'048U;
    const auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < maximum_updates && !local_source_terminal(operation.state());
         ++update) {
        operation.update(now);
    }
    if (operation.state() !=
        hlclient::local_assets::LocalAssetSourceOpenState::source_ready) {
        const auto code = operation.error()
                              ? hlclient::local_assets::to_string(
                                    operation.error()->code)
                              : std::string_view{"source_open_incomplete"};
        std::cerr << "map-source-open=" << code << '\n';
        return std::nullopt;
    }
    return operation.take_result();
}

[[nodiscard]] int print_texture_summary(
    const hlclient::assets::WorldAsset& world,
    const hlclient::assets::WorldTextureSet& textures)
{
    const auto& statistics = textures.statistics();
    std::cout << "bsp-version=" << world.statistics.source_version << '\n';
    std::cout << "geometry-vertices=" << world.vertices.size() << '\n';
    std::cout << "geometry-triangles=" << world.indices.size() / 3U << '\n';
    std::cout << "geometry-surfaces=" << world.surfaces.size() << '\n';
    std::cout << "material-count=" << world.materials.size() << '\n';
    std::cout << "embedded-references="
              << world.statistics.embedded_texture_reference_count << '\n';
    std::cout << "external-references="
              << world.statistics.external_texture_reference_count << '\n';
    std::cout << "missing-references="
              << world.statistics.missing_texture_reference_count << '\n';
    std::cout << "worldspawn-parser=success\n";
    std::cout << "wad-declarations=" << statistics.wad_declaration_count << '\n';
    std::cout << "wad-resolved=" << statistics.wad_archive_resolved_count << '\n';
    std::cout << "wad-missing=" << statistics.wad_archive_missing_count << '\n';
    std::cout << "decoded-textures=" << statistics.decoded_texture_count << '\n';
    std::cout << "decoded-embedded=" << statistics.embedded_texture_count << '\n';
    std::cout << "decoded-wad3=" << statistics.wad3_texture_count << '\n';
    std::cout << "masked-textures=" << statistics.masked_texture_count << '\n';
    std::cout << "total-rgba-bytes=" << statistics.total_rgba_byte_count << '\n';
    std::cout << "unresolved-bindings="
              << statistics.unresolved_material_count << '\n';
    std::cout << "completeness="
              << (textures.complete_for_world_materials() ? "complete"
                                                          : "incomplete")
              << '\n';
    std::cout << "network-operations=0\n";
    std::cout << "writes-performed=0\n";
    return textures.complete_for_world_materials() ? 0 : 1;
}

} // namespace

int wmain(const int argument_count, wchar_t* arguments[])
{
    const auto options = parse_options(argument_count, arguments);
    if (!options) {
        print_usage();
        return 2;
    }

    auto virtual_map =
        hlclient::local_resources::LocalVirtualResourceName::create(
            *options->virtual_map);
    if (!virtual_map) {
        std::cerr << "map-name=unsafe\n";
        return 1;
    }
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        *options->base_directory,
        options->game_directory);
    if (!roots) {
        const auto code = roots.error
                              ? hlclient::local_resources::to_string(
                                    roots.error->code)
                              : std::string_view{"io_error"};
        std::cerr << "root-validation=" << code << '\n';
        return 1;
    }

    auto resolver_limits =
        hlclient::local_resources::LocalResourceResolverLimits{};
    resolver_limits.maximum_file_size =
        hlclient::local_resources::kHardMaximumLocalResourceFileSize;
    auto created_environment =
        hlclient::local_resources::LocalResourceEnvironment::create(
            std::move(*roots.roots),
            resolver_limits);
    if (!created_environment || !created_environment.environment) {
        const auto code = created_environment.error
                              ? hlclient::local_resources::to_string(
                                    created_environment.error->code)
                              : std::string_view{"unable_to_retain_environment"};
        std::cerr << "environment=" << code << '\n';
        return 1;
    }
    auto environment = std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>{
        std::move(created_environment.environment)};

    auto resolved = environment->resolver().resolve(*virtual_map.name);
    if (!resolved) {
        std::cerr << "map-resolution="
                  << hlclient::local_resources::to_string(resolved.code)
                  << '\n';
        return 1;
    }
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto source_size = resolved.file->file_size();
    resolved.file->close();
    auto locator = environment->make_locator(
        root_id,
        std::move(*virtual_map.name),
        identity,
        source_size);
    if (!locator) {
        const auto code = locator.error
                              ? hlclient::local_resources::to_string(
                                    locator.error->code)
                              : std::string_view{"invalid_locator"};
        std::cerr << "map-locator=" << code << '\n';
        return 1;
    }

    auto source = open_map_source(environment, std::move(*locator.locator));
    if (!source) {
        return 1;
    }
    hlclient::goldsrc::bsp::GoldSrcBspWorldImporter importer;
    auto imported = importer.import(source->source());
    if (!imported) {
        std::cerr << "bsp-import=failed\n";
        return 1;
    }
    auto world = std::move(imported).value();
    auto started = hlclient::goldsrc::WorldTextureImportOperation::begin(
        world,
        source->source().bytes(),
        environment);
    if (!started) {
        const auto code = started.error
                              ? hlclient::goldsrc::to_string(
                                    started.error->code)
                              : std::string_view{"texture_import_begin_failed"};
        std::cerr << "texture-import=" << code << '\n';
        return 1;
    }

    auto& operation = *started.operation;
    constexpr std::size_t maximum_updates = 1'000'000U;
    const auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < maximum_updates && !operation.terminal();
         ++update) {
        operation.update(now);
    }
    if (!operation.terminal() || operation.result() == nullptr) {
        const auto code = operation.error()
                              ? hlclient::goldsrc::to_string(
                                    operation.error()->code)
                              : std::string_view{"texture_import_incomplete"};
        std::cerr << "texture-import=" << code << '\n';
        if (operation.error() &&
            operation.error()->code == hlclient::goldsrc::
                WorldTextureImportErrorCode::worldspawn_parse_failed) {
            std::cerr << "worldspawn-parser=failed\n";
        }
        return 1;
    }
    return print_texture_summary(world, *operation.result());
}
