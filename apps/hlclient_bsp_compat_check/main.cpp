#include <hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_transform.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

enum class ValidationStage {
    geometry,
    textures,
    render_package,
    spatial_scene,
};

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::optional<std::string> game_directory;
    std::optional<std::string> virtual_map;
    std::optional<ValidationStage> validate_through;
    bool inspect_entities{false};
};

struct CompatibilitySummary {
    std::uint64_t bsp_version{0U};
    std::uint64_t model_count{0U};
    std::uint64_t world_face_count{0U};
    std::uint64_t brush_face_count{0U};
    std::uint64_t canonicalized_face_count{0U};
    std::uint64_t removed_collinear_corner_count{0U};
    double minimum_winding_dot{0.0};
    double maximum_planarity_error{0.0};
    std::uint64_t vertex_count{0U};
    std::uint64_t triangle_count{0U};
    std::uint64_t texture_count{0U};
    std::uint64_t lightmap_page_count{0U};
    std::uint64_t pvs_row_count{0U};
    std::uint64_t brush_model_count{0U};
    std::uint64_t supported_instance_count{0U};
};

[[nodiscard]] bool stage_includes(
    const ValidationStage selected,
    const ValidationStage required) noexcept
{
    return static_cast<unsigned int>(selected) >=
        static_cast<unsigned int>(required);
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

[[nodiscard]] std::optional<Options> parse_options(
    const int argument_count,
    wchar_t* arguments[])
{
    Options options;
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument == L"--inspect-entities") {
            if (options.inspect_entities) return std::nullopt;
            options.inspect_entities = true;
            continue;
        }
        if (argument != L"--basedir" && argument != L"--game" &&
            argument != L"--map" && argument != L"--validate-through") {
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
            if (options.game_directory) {
                return std::nullopt;
            }
            options.game_directory = std::move(*narrow);
        } else if (argument == L"--map") {
            if (options.virtual_map) {
                return std::nullopt;
            }
            options.virtual_map = std::move(*narrow);
        } else {
            if (options.validate_through) {
                return std::nullopt;
            }
            if (*narrow == "geometry") {
                options.validate_through = ValidationStage::geometry;
            } else if (*narrow == "textures") {
                options.validate_through = ValidationStage::textures;
            } else if (*narrow == "render-package") {
                options.validate_through = ValidationStage::render_package;
            } else if (*narrow == "spatial-scene") {
                options.validate_through = ValidationStage::spatial_scene;
            } else {
                return std::nullopt;
            }
        }
    }

    if (!options.base_directory || !options.game_directory ||
        !options.virtual_map || !options.validate_through) {
        return std::nullopt;
    }
    return options;
}

void print_usage()
{
    std::cerr
        << "Usage: hlclient_bsp_compat_check --basedir <Half-Life root> "
           "--game <directory> --map <maps/name.bsp> "
           "--validate-through "
           "<geometry|textures|render-package|spatial-scene> "
           "[--inspect-entities]\n";
}

void print_failure(
    const std::string_view classification,
    const std::optional<std::uint32_t> model = std::nullopt,
    const std::optional<std::size_t> face = std::nullopt,
    const std::optional<std::int16_t> side = std::nullopt,
    const std::optional<std::uint32_t> edges = std::nullopt)
{
    std::cerr << "[compat-error] model=";
    if (model) {
        std::cerr << *model;
    } else {
        std::cerr << "unavailable";
    }
    std::cerr << ";face=";
    if (face) {
        std::cerr << *face;
    } else {
        std::cerr << "unavailable";
    }
    std::cerr << ";side=";
    if (side) {
        std::cerr << *side;
    } else {
        std::cerr << "unavailable";
    }
    std::cerr << ";edges=";
    if (edges) {
        std::cerr << *edges;
    } else {
        std::cerr << "unavailable";
    }
    std::cerr << ";classification=" << classification << '\n';
}

void print_parser_failure(
    const hlclient::goldsrc::bsp::GoldSrcBspParseResult& result)
{
    if (!result.error) {
        print_failure("bsp_parse_failed");
        return;
    }
    const auto& error = *result.error;
    if (error.face_geometry_diagnostic) {
        const auto& diagnostic = *error.face_geometry_diagnostic;
        print_failure(
            hlclient::goldsrc::bsp::to_string(
                diagnostic.failure_classification),
            diagnostic.source_model_index,
            static_cast<std::size_t>(diagnostic.source_face_ordinal),
            diagnostic.face_side,
            diagnostic.surfedge_count);
        return;
    }
    print_failure(
        hlclient::goldsrc::bsp::to_string(error.code),
        error.source_model_index,
        error.element_index);
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
    const hlclient::local_resources::LocalResourceLocator& locator)
{
    hlclient::local_assets::LocalAssetSourceOpenLimits limits;
    limits.maximum_source_bytes =
        hlclient::goldsrc::bsp::kGoldSrcBspDefaultMaximumSourceBytes;
    limits.maximum_chunks_per_update = 1U;
    limits.maximum_open_sources = 1U;

    hlclient::local_assets::LocalAssetSourceOpener opener;
    auto started = opener.begin(locator, environment, limits);
    if (!started) {
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

[[nodiscard]] std::optional<hlclient::assets::WorldTextureSet>
import_renderable_textures(
    const hlclient::assets::WorldAsset& world,
    const std::span<const std::byte> retained_bsp_source,
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>& environment)
{
    hlclient::goldsrc::GoldSrcWorldTextureImportLimits limits;
    limits.missing_texture_policy = hlclient::goldsrc::MissingWorldTexturePolicy::placeholder_for_absent_name;
    auto started = hlclient::goldsrc::WorldTextureImportOperation::begin(
        world,
        retained_bsp_source,
        environment, limits);
    if (!started) {
        return std::nullopt;
    }

    auto& operation = *started.operation;
    constexpr std::size_t maximum_updates = 1'000'000U;
    const auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < maximum_updates && !operation.terminal();
         ++update) {
        operation.update(now);
    }
    if (!operation.terminal() || operation.result() == nullptr ||
        !operation.result()->renderable_for_world_materials()) {
        return std::nullopt;
    }
    return operation.take_result();
}

[[nodiscard]] std::shared_ptr<const hlclient::world_render::WorldRenderPackage>
build_world_render_package(
    hlclient::assets::WorldAsset world,
    hlclient::assets::WorldTextureSet textures,
    const std::span<const std::byte> retained_bsp_source)
{
    auto imported_lightmaps =
        hlclient::goldsrc::lightmaps::GoldSrcWorldLightmapImporter::import(
            world,
            retained_bsp_source);
    if (!imported_lightmaps || !imported_lightmaps.lightmap_set ||
        !imported_lightmaps.lightmap_set->complete_for_world_surfaces()) {
        return {};
    }

    hlclient::world_render::WorldRenderPackageBuilder builder;
    auto built = builder.build(
        hlclient::assets::TexturedWorldAsset{
            std::move(world),
            std::move(textures),
        },
        std::move(*imported_lightmaps.lightmap_set));
    if (!built || !built.package) {
        return {};
    }
    return std::make_shared<hlclient::world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] bool valid_geometry_summary(
    const CompatibilitySummary& summary) noexcept
{
    if (summary.bsp_version !=
            static_cast<std::uint64_t>(
                hlclient::goldsrc::bsp::kGoldSrcBspVersion) ||
        summary.model_count == 0U || summary.world_face_count == 0U ||
        summary.vertex_count < 3U || summary.triangle_count == 0U ||
        !std::isfinite(summary.minimum_winding_dot) ||
        summary.minimum_winding_dot <= 0.0 ||
        !std::isfinite(summary.maximum_planarity_error) ||
        summary.maximum_planarity_error < 0.0 ||
        summary.maximum_planarity_error >
            static_cast<double>(
                hlclient::goldsrc::bsp::kGoldSrcBspPlanarityTolerance)) {
        return false;
    }
    if (summary.world_face_count >
        std::numeric_limits<std::uint64_t>::max() - summary.brush_face_count) {
        return false;
    }
    return summary.canonicalized_face_count ==
        summary.world_face_count + summary.brush_face_count;
}

void print_summary(const CompatibilitySummary& summary)
{
    std::cout.imbue(std::locale::classic());
    std::cout << "[compat] bsp-version=" << summary.bsp_version << '\n';
    std::cout << "[compat] models=" << summary.model_count << '\n';
    std::cout << "[compat] world-faces=" << summary.world_face_count << '\n';
    std::cout << "[compat] brush-faces=" << summary.brush_face_count << '\n';
    std::cout << "[compat] canonicalized-faces="
              << summary.canonicalized_face_count << '\n';
    std::cout << "[compat] removed-collinear-corners="
              << summary.removed_collinear_corner_count << '\n';
    std::cout << std::defaultfloat
              << std::setprecision(std::numeric_limits<double>::max_digits10);
    std::cout << "[compat] min-winding-dot=" << summary.minimum_winding_dot
              << '\n';
    std::cout << "[compat] max-planarity-error="
              << summary.maximum_planarity_error << '\n';
    std::cout << "[compat] vertices=" << summary.vertex_count << '\n';
    std::cout << "[compat] triangles=" << summary.triangle_count << '\n';
    std::cout << "[compat] textures=" << summary.texture_count << '\n';
    std::cout << "[compat] lightmap-pages=" << summary.lightmap_page_count
              << '\n';
    std::cout << "[compat] pvs-rows=" << summary.pvs_row_count << '\n';
    std::cout << "[compat] brush-models=" << summary.brush_model_count << '\n';
    std::cout << "[compat] supported-instances="
              << summary.supported_instance_count << '\n';
    std::cout << "[compat] result=success\n";
}

// Opt-in read-only metadata inspection. Reuses the production BSP/entity
// parsers and prints only bounded inert identifiers and finite coordinates.
// No entity is spawned and no gameplay meaning is assigned by this tool.
[[nodiscard]] bool print_entities(
    const hlclient::goldsrc::bsp::GoldSrcBspParsedDocument& bsp_document)
{
    namespace bsp = hlclient::goldsrc::bsp;
    const auto parsed = bsp::GoldSrcEntityDocumentParser::parse(
        bsp_document.entity_lump_bytes);
    if (!parsed || !parsed.document) {
        print_failure("entity_document_failed");
        return false;
    }
    const auto print_vector = [](const auto& value) {
        std::cout << value.x << ',' << value.y << ',' << value.z;
    };
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<float>::max_digits10);
    std::size_t ordinal = 0U;
    for (const auto& entity : parsed.document->entities()) {
        const auto* name = bsp::find_interpreted_key(entity,
            bsp::GoldSrcInterpretedEntityKey::classname).unique_pair(entity);
        const bool safe_name = name && !name->value.empty() && name->value.size() <= 128U &&
            name->value.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
        std::cout << "[bsp-entity] index=" << ordinal++
                  << " classname=" << (safe_name ? name->value : "unavailable")
                  << " initial_origin=";
        const auto* origin = bsp::find_interpreted_key(entity,
            bsp::GoldSrcInterpretedEntityKey::origin).unique_pair(entity);
        const auto position = origin ? bsp::parse_entity_vector3(origin->value)
                                     : bsp::GoldSrcEntityVectorResult{};
        if (position) print_vector(*position.value);
        else std::cout << "unavailable";
        const auto* model = bsp::find_interpreted_key(entity,
            bsp::GoldSrcInterpretedEntityKey::model).unique_pair(entity);
        if (model) {
            const auto reference = hlclient::goldsrc::brush_models::parse_brush_model_reference(
                model->value, static_cast<std::size_t>(bsp_document.geometry_statistics.source_model_count));
            if (reference) {
                const auto index = *reference.source_model_index;
                for (const auto& brush : bsp_document.brush_submodels) {
                    if (brush.source_model_index != index) continue;
                    std::cout << " brush_model=" << index << " initial_bounds_min=";
                    print_vector(brush.source_model_bounds.minimum);
                    std::cout << " initial_bounds_max=";
                    print_vector(brush.source_model_bounds.maximum);
                    std::cout << " source_origin=";
                    print_vector(brush.source_model_origin);
                    std::cout << " faces=" << brush.geometry.surfaces.size()
                              << " first_face=" << brush.geometry.surfaces.front().source_surface_ordinal.value_or(0U)
                              << " textures=";
                    for (const auto& material : brush.geometry.materials)
                        if (material.texture_name) std::cout << *material.texture_name << ',';
                    const auto metadata = hlclient::goldsrc::brush_models::interpret_brush_entity(
                        entity, ordinal - 1U, static_cast<std::size_t>(bsp_document.geometry_statistics.source_model_count));
                    if (metadata.metadata) {
                        std::cout << " rendermode=" << metadata.metadata->rendermode << " renderamt=";
                        if (metadata.metadata->render_amount) std::cout << *metadata.metadata->render_amount;
                        else std::cout << "unavailable";
                    }
                    break;
                }
            }
        }
        for (const auto key : {"target", "targetname", "master", "angle", "angles",
                               "speed", "wait", "lip", "height", "spawnflags"}) {
            std::size_t count = 0U;
            std::string_view value;
            for (const auto& pair : entity.pairs()) if (pair.key == key) { ++count; value = pair.value; }
            const bool safe = count == 1U && value.size() <= 128U &&
                value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-. ") == std::string_view::npos;
            std::cout << ' ' << key << '=' << (safe ? value : "unavailable");
        }
        std::cout << '\n';
    }
    std::cout << "[bsp-entities] count=" << ordinal << " result=success\n";
    return true;
}

[[nodiscard]] int run_checker(const int argument_count, wchar_t* arguments[])
{
    const auto options = parse_options(argument_count, arguments);
    if (!options) {
        print_usage();
        return 2;
    }

    auto virtual_map =
        hlclient::local_resources::LocalVirtualResourceName::create(
            *options->virtual_map);
    if (!virtual_map || !virtual_map.name) {
        print_failure("unsafe_virtual_map");
        return 1;
    }
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        *options->base_directory,
        *options->game_directory);
    if (!roots || !roots.roots) {
        print_failure("invalid_local_roots");
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
        print_failure("local_environment_failed");
        return 1;
    }
    auto environment = std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>{
        std::move(created_environment.environment)};

    auto resolved = environment->resolver().resolve(*virtual_map.name);
    if (!resolved || !resolved.file) {
        print_failure("map_resolution_failed");
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
    if (!locator || !locator.locator) {
        print_failure("map_locator_failed");
        return 1;
    }
    resolved.file.reset();

    auto source = open_map_source(environment, *locator.locator);
    if (!source) {
        print_failure("map_source_open_failed");
        return 1;
    }
    const auto retained_bsp_source = source->source().bytes();
    auto parsed = hlclient::goldsrc::bsp::GoldSrcBspParser::parse(
        retained_bsp_source,
        {},
        hlclient::goldsrc::bsp::GoldSrcBspParseOptions{true});
    if (!parsed || !parsed.document) {
        print_parser_failure(parsed);
        return 1;
    }

    auto document = std::move(*parsed.document);
    document.world_asset.identity.source_name = *options->virtual_map;
    const auto& geometry = document.geometry_statistics;
    CompatibilitySummary summary{
        static_cast<std::uint64_t>(document.world_asset.statistics.source_version),
        geometry.source_model_count,
        geometry.world_face_count,
        geometry.brush_face_count,
        geometry.canonicalized_face_count,
        geometry.removed_collinear_corner_count,
        geometry.minimum_accepted_winding_margin,
        geometry.maximum_planarity_deviation,
        static_cast<std::uint64_t>(document.world_asset.vertices.size()),
        static_cast<std::uint64_t>(document.world_asset.indices.size() / 3U),
        0U,
        0U,
        0U,
        0U,
        0U,
    };
    if (!valid_geometry_summary(summary)) {
        print_failure("invalid_geometry_summary");
        return 1;
    }

    if (options->inspect_entities && !print_entities(document)) return 1;

    if (!stage_includes(
            *options->validate_through, ValidationStage::textures)) {
        print_summary(summary);
        return 0;
    }

    auto textures = import_renderable_textures(
        document.world_asset,
        retained_bsp_source,
        environment);
    if (!textures) {
        print_failure("texture_import_failed");
        return 1;
    }
    summary.texture_count = static_cast<std::uint64_t>(
        textures->texture_count());
    if (textures->statistics().placeholder_material_count != 0U) {
        std::cout << "[compat-warning] classification=missing_texture_placeholder;bindings="
                  << textures->statistics().placeholder_material_count
                  << ";generated_images=" << textures->statistics().generated_placeholder_texture_count
                  << ";source_textures_complete=false\n";
    }
    if (summary.texture_count == 0U) {
        print_failure("empty_texture_set");
        return 1;
    }
    if (!stage_includes(
            *options->validate_through, ValidationStage::render_package)) {
        print_summary(summary);
        return 0;
    }

    auto world_package = build_world_render_package(
        document.world_asset,
        std::move(*textures),
        retained_bsp_source);
    if (!world_package) {
        print_failure("render_package_failed");
        return 1;
    }
    summary.lightmap_page_count = static_cast<std::uint64_t>(
        world_package->lightmaps().statistics().atlas_page_count);
    if (world_package->statistics().vertex_count == 0U ||
        world_package->statistics().triangle_count == 0U) {
        print_failure("incomplete_render_package");
        return 1;
    }
    if (!stage_includes(
            *options->validate_through, ValidationStage::spatial_scene)) {
        print_summary(summary);
        return 0;
    }

    namespace brush = hlclient::goldsrc::brush_models;
    auto built_library = brush::GoldSrcBrushRenderLibraryBuilder::build(
        document,
        retained_bsp_source,
        environment);
    if (!built_library || !built_library.library) {
        print_failure("brush_library_failed");
        return 1;
    }
    std::optional<hlclient::world_scene_render::BrushSubmodelRenderLibrary>
        brush_library;
    brush_library.emplace(std::move(*built_library.library));

    auto built_scene = brush::GoldSrcWorldSceneBuilder::build(
        document,
        std::move(world_package),
        std::move(brush_library),
        brush::GoldSrcWorldSceneBuildConfig{
            brush::GoldSrcWorldSceneBrushMode::static_initial,
            false,
        });
    if (!built_scene || !built_scene.scene_package) {
        print_failure("spatial_scene_failed");
        return 1;
    }

    const auto& scene = *built_scene.scene_package;
    summary.pvs_row_count =
        scene.spatial_package().statistics().unique_pvs_row_count;
    summary.brush_model_count = static_cast<std::uint64_t>(
        scene.statistics().brush_model_count);
    summary.supported_instance_count = static_cast<std::uint64_t>(
        scene.statistics().supported_brush_instance_count);
    if (summary.pvs_row_count == 0U ||
        summary.brush_model_count != geometry.source_model_count - 1U) {
        print_failure("incomplete_spatial_scene");
        return 1;
    }

    print_summary(summary);
    return 0;
}

} // namespace

int wmain(const int argument_count, wchar_t* arguments[])
{
    try {
        return run_checker(argument_count, arguments);
    } catch (const std::bad_alloc&) {
        print_failure("allocation_failed");
    } catch (const std::exception&) {
        // Exception text may contain platform paths. Keep this boundary
        // metadata-only and bounded.
        print_failure("checker_failed");
    }
    return 1;
}
