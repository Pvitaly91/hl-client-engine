#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/assets/sprite_asset_types.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_animation.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <locale>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

enum class RequestedAssetKind {
    automatic,
    model,
    sprite,
};

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::optional<std::string> game_directory;
    std::optional<std::string> virtual_asset;
    std::optional<RequestedAssetKind> kind;
    std::optional<std::uint32_t> sample_sequence;
    std::optional<std::uint32_t> sample_frame;
};

[[nodiscard]] bool looks_like_option(const std::wstring_view value) noexcept
{
    return value.size() >= 2U && value[0U] == L'-' && value[1U] == L'-';
}

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

[[nodiscard]] std::optional<std::uint32_t> parse_unsigned_integer(
    const std::string_view value) noexcept
{
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint32_t parsed = 0U;
    const auto converted = std::from_chars(
        value.data(), value.data() + value.size(), parsed, 10);
    if (converted.ec != std::errc{} ||
        converted.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] std::optional<RequestedAssetKind> parse_kind(
    const std::string_view value) noexcept
{
    if (value == "auto") {
        return RequestedAssetKind::automatic;
    }
    if (value == "model") {
        return RequestedAssetKind::model;
    }
    if (value == "sprite") {
        return RequestedAssetKind::sprite;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int argument_count,
    wchar_t* arguments[])
{
    Options options;
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument != L"--basedir" && argument != L"--game" &&
            argument != L"--asset" && argument != L"--kind" &&
            argument != L"--sample-sequence" &&
            argument != L"--sample-frame") {
            return std::nullopt;
        }
        if (index + 1 >= argument_count) {
            return std::nullopt;
        }
        const std::wstring_view value{arguments[++index]};
        if (value.empty() || looks_like_option(value)) {
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
            if (options.game_directory) {
                return std::nullopt;
            }
            options.game_directory = std::move(*narrow);
        } else if (argument == L"--asset") {
            if (options.virtual_asset) {
                return std::nullopt;
            }
            options.virtual_asset = std::move(*narrow);
        } else if (argument == L"--kind") {
            if (options.kind) {
                return std::nullopt;
            }
            options.kind = parse_kind(*narrow);
            if (!options.kind) {
                return std::nullopt;
            }
        } else if (argument == L"--sample-sequence") {
            if (options.sample_sequence) {
                return std::nullopt;
            }
            options.sample_sequence = parse_unsigned_integer(*narrow);
            if (!options.sample_sequence) {
                return std::nullopt;
            }
        } else {
            if (options.sample_frame) {
                return std::nullopt;
            }
            options.sample_frame = parse_unsigned_integer(*narrow);
            if (!options.sample_frame) {
                return std::nullopt;
            }
        }
    }

    if (!options.base_directory || !options.game_directory ||
        !options.virtual_asset || !options.kind ||
        options.sample_sequence.has_value() !=
            options.sample_frame.has_value()) {
        return std::nullopt;
    }
    if (*options.kind == RequestedAssetKind::sprite &&
        options.sample_sequence) {
        return std::nullopt;
    }
    return options;
}

void print_usage()
{
    std::cerr
        << "Usage: hlclient_goldsrc_asset_check --basedir <Half-Life root> "
           "--game <directory> --asset <safe virtual asset> "
           "--kind <auto|model|sprite> "
           "[--sample-sequence <index> --sample-frame <integer-frame>]\n";
}

void print_failure(const std::string_view classification)
{
    std::cerr << "[asset-check] error=" << classification << '\n';
}

[[nodiscard]] bool local_source_terminal(
    const hlclient::local_assets::LocalAssetSourceOpenState state) noexcept
{
    using State = hlclient::local_assets::LocalAssetSourceOpenState;
    return state == State::source_ready || state == State::cancelled ||
        state == State::timed_out || state == State::failed;
}

[[nodiscard]] std::optional<hlclient::local_assets::LocalAssetSource>
open_main_source(
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>& environment,
    const hlclient::local_resources::LocalResourceLocator& locator)
{
    hlclient::local_assets::LocalAssetSourceOpenLimits limits;
    limits.maximum_source_bytes =
        hlclient::local_resources::kHardMaximumLocalResourceFileSize;
    limits.maximum_chunks_per_update = 1U;
    limits.maximum_open_sources = 1U;

    hlclient::local_assets::LocalAssetSourceOpener opener;
    auto started = opener.begin(locator, environment, limits);
    if (!started || !started.operation) {
        return std::nullopt;
    }

    auto& operation = *started.operation;
    constexpr std::size_t maximum_updates = 1'000'000U;
    const auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < maximum_updates && !local_source_terminal(operation.state());
         ++update) {
        operation.update(now);
    }
    if (operation.state() !=
        hlclient::local_assets::LocalAssetSourceOpenState::source_ready) {
        return std::nullopt;
    }
    return operation.take_result();
}

[[nodiscard]] bool sample_model_sequence(
    const hlclient::assets::ModelAsset& model,
    const std::uint32_t sequence_index,
    const std::uint32_t integer_frame) noexcept
{
    if (!model.skeletal_data ||
        sequence_index >= model.skeletal_data->sequences.size()) {
        return false;
    }
    const auto& sequence = model.skeletal_data->sequences[sequence_index];
    if (integer_frame >= sequence.frame_count) {
        return false;
    }
    for (const auto& blend : sequence.animation_blends) {
        for (const auto& track : blend.bone_tracks) {
            for (const auto& channel : track.channels) {
                const auto sampled = hlclient::goldsrc::studio::
                    StudioAnimationChannelSampler::sample_default_scaled(
                        channel, integer_frame);
                if (!sampled || !std::isfinite(*sampled)) {
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] constexpr std::string_view sprite_orientation_name(
    const hlclient::assets::SpriteOrientation orientation) noexcept
{
    using Orientation = hlclient::assets::SpriteOrientation;
    switch (orientation) {
    case Orientation::view_parallel_upright:
        return "view_parallel_upright";
    case Orientation::facing_upright: return "facing_upright";
    case Orientation::view_parallel: return "view_parallel";
    case Orientation::oriented: return "oriented";
    case Orientation::view_parallel_oriented:
        return "view_parallel_oriented";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view sprite_format_name(
    const hlclient::assets::SpriteTextureFormat format) noexcept
{
    using Format = hlclient::assets::SpriteTextureFormat;
    switch (format) {
    case Format::normal: return "normal";
    case Format::additive: return "additive";
    case Format::index_alpha: return "index_alpha";
    case Format::alpha_test: return "alpha_test";
    }
    return "unknown";
}

void print_model_summary(
    const hlclient::assets::ModelAsset& model,
    const std::size_t source_count)
{
    const auto& skeletal = *model.skeletal_data;
    const auto& statistics = skeletal.statistics;
    std::cout << "[model] importer="
              << hlclient::goldsrc::studio::kGoldSrcStudioModelImporterId
              << '\n';
    std::cout << "[model] version=10\n";
    std::cout << "[model] sources=" << source_count << '\n';
    std::cout << "[model] bones=" << skeletal.bones.size() << '\n';
    std::cout << "[model] bodyparts=" << skeletal.bodyparts.size() << '\n';
    std::cout << "[model] submodels=" << skeletal.submodels.size() << '\n';
    std::cout << "[model] meshes=" << statistics.mesh_count << '\n';
    std::cout << "[model] vertices=" << statistics.emitted_vertex_count
              << '\n';
    std::cout << "[model] triangles=" << statistics.emitted_triangle_count
              << '\n';
    std::cout << "[model] textures=" << skeletal.textures.size() << '\n';
    std::cout << "[model] skins=" << skeletal.skin_families.size() << '\n';
    std::cout << "[model] sequences=" << skeletal.sequences.size() << '\n';
    std::cout << "[model] sequence-groups="
              << skeletal.sequence_groups.size() << '\n';
    std::cout << "[model] animation-runs=" << statistics.animation_run_count
              << '\n';
    std::cout << "[model] result=complete\n";
    std::cout << "[model] complete=true\n";
}

void print_sprite_summary(const hlclient::assets::SpriteAsset& sprite)
{
    const auto& source = *sprite.source_data;
    const auto& statistics = source.statistics;
    std::cout << "[sprite] importer="
              << hlclient::goldsrc::sprite::kGoldSrcSpriteImporterId << '\n';
    std::cout << "[sprite] sources=1\n";
    std::cout << "[sprite] version=" << source.source_version << '\n';
    std::cout << "[sprite] orientation="
              << sprite_orientation_name(source.orientation) << '\n';
    std::cout << "[sprite] format="
              << sprite_format_name(source.texture_format) << '\n';
    std::cout << "[sprite] top-level-entries="
              << statistics.top_level_entry_count << '\n';
    std::cout << "[sprite] entries="
              << statistics.top_level_entry_count << '\n';
    std::cout << "[sprite] flattened-frames="
              << statistics.flattened_frame_count << '\n';
    std::cout << "[sprite] frames="
              << statistics.flattened_frame_count << '\n';
    std::cout << "[sprite] groups=" << statistics.group_count << '\n';
    std::cout << "[sprite] indexed-bytes="
              << statistics.indexed_pixel_byte_count << '\n';
    std::cout << "[sprite] result=complete\n";
}

[[nodiscard]] bool requested_category_matches(
    const Options& options,
    const hlclient::assets::AssetImporterCategory category) noexcept
{
    if (*options.kind == RequestedAssetKind::automatic) {
        return category == hlclient::assets::AssetImporterCategory::model ||
               category == hlclient::assets::AssetImporterCategory::sprite;
    }
    return (*options.kind == RequestedAssetKind::model &&
            category == hlclient::assets::AssetImporterCategory::model) ||
           (*options.kind == RequestedAssetKind::sprite &&
            category == hlclient::assets::AssetImporterCategory::sprite);
}

[[nodiscard]] int import_visual_asset(
    const Options& options,
    const hlclient::local_assets::LocalAssetSource& main_source,
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>& environment,
    const hlclient::assets::AssetImporterRegistries& registries)
{
    auto started = hlclient::goldsrc::visual_assets::
        GoldSrcVisualAssetImportOperation::begin(
            main_source, environment, registries);
    if (!started || !started.operation) {
        print_failure("visual_import_begin_failed");
        return 1;
    }

    auto& operation = *started.operation;
    constexpr std::size_t maximum_updates = 1'000'000U;
    const auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < maximum_updates && !operation.terminal(); ++update) {
        operation.update(now);
    }
    if (!operation.terminal() || operation.result() == nullptr) {
        const auto classification = operation.error()
                                        ? hlclient::goldsrc::visual_assets::
                                              to_string(operation.error()->code)
                                        : std::string_view{
                                              "visual_import_incomplete"};
        print_failure(classification);
        return 1;
    }

    const auto& result = *operation.result();
    if (!requested_category_matches(options, result.selected_category())) {
        print_failure("asset_kind_mismatch");
        return 1;
    }

    if (const auto* model =
            std::get_if<hlclient::assets::ModelAsset>(&result.asset())) {
        if (!model->skeletal_data) {
            print_failure("model_metadata_missing");
            return 1;
        }
        if (options.sample_sequence &&
            !sample_model_sequence(
                *model, *options.sample_sequence, *options.sample_frame)) {
            print_failure("model_sample_failed");
            return 1;
        }
        print_model_summary(
            *model, result.dependency_statistics().source_count);
        return 0;
    }

    if (const auto* sprite =
            std::get_if<hlclient::assets::SpriteAsset>(&result.asset())) {
        if (options.sample_sequence) {
            print_failure("model_sample_requested_for_sprite");
            return 1;
        }
        if (!sprite->source_data) {
            print_failure("sprite_metadata_missing");
            return 1;
        }
        print_sprite_summary(*sprite);
        return 0;
    }

    print_failure("visual_asset_type_mismatch");
    return 1;
}

[[nodiscard]] int run_checker(const int argument_count, wchar_t* arguments[])
{
    const auto options = parse_options(argument_count, arguments);
    if (!options) {
        print_usage();
        return 2;
    }

    auto virtual_asset =
        hlclient::local_resources::LocalVirtualResourceName::create(
            *options->virtual_asset);
    if (!virtual_asset || !virtual_asset.name) {
        print_failure("unsafe_virtual_asset");
        return 1;
    }
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        *options->base_directory, *options->game_directory);
    if (!roots || !roots.roots) {
        print_failure("invalid_local_roots");
        return 1;
    }

    hlclient::local_resources::LocalResourceResolverLimits resolver_limits;
    resolver_limits.maximum_file_size =
        hlclient::local_resources::kHardMaximumLocalResourceFileSize;
    auto created_environment =
        hlclient::local_resources::LocalResourceEnvironment::create(
            std::move(*roots.roots), resolver_limits);
    if (!created_environment || !created_environment.environment) {
        print_failure("local_environment_failed");
        return 1;
    }
    auto environment = std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>{
        std::move(created_environment.environment)};

    auto resolved = environment->resolver().resolve(*virtual_asset.name);
    if (!resolved || !resolved.file) {
        print_failure("asset_resolution_failed");
        return 1;
    }
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto source_size = resolved.file->file_size();
    resolved.file->close();
    auto locator = environment->make_locator(
        root_id, std::move(*virtual_asset.name), identity, source_size);
    resolved.file.reset();
    if (!locator || !locator.locator) {
        print_failure("asset_locator_failed");
        return 1;
    }

    auto main_source = open_main_source(environment, *locator.locator);
    if (!main_source) {
        print_failure("asset_source_open_failed");
        return 1;
    }

    hlclient::assets::AssetImporterRegistries registries;
    if (!hlclient::goldsrc::register_builtin_asset_importers(
            registries)) {
        print_failure("importer_registration_failed");
        return 1;
    }
    return import_visual_asset(
        *options, *main_source, environment, registries);
}

} // namespace

int wmain(const int argument_count, wchar_t* arguments[])
{
    std::cout.imbue(std::locale::classic());
    std::cerr.imbue(std::locale::classic());
    try {
        return run_checker(argument_count, arguments);
    } catch (const std::bad_alloc&) {
        print_failure("allocation_failed");
    } catch (const std::exception&) {
        // Exception text can contain platform paths. Keep diagnostics
        // metadata-only at this boundary.
        print_failure("checker_failed");
    } catch (...) {
        print_failure("checker_failed");
    }
    return 1;
}
