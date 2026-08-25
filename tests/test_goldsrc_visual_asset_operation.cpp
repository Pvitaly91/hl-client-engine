#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_bundle_import.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include "goldsrc_sprite_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace goldsrc = hlclient::goldsrc;
namespace local = hlclient::local_resources;
namespace local_assets = hlclient::local_assets;
namespace readiness = hlclient::tests::readiness_fixture;
namespace sprite_fixture = hlclient::tests::sprite_fixture;
namespace visual = hlclient::goldsrc::visual_assets;
using hlclient::tests::ScopedLocalResourceTestRoot;

struct VisualImporterCallCounts {
    std::size_t model_probe_calls{0U};
    std::size_t model_import_calls{0U};
    std::size_t sprite_probe_calls{0U};
    std::size_t sprite_import_calls{0U};
};

class CountingStudioModelImporter final
    : public goldsrc::studio::IGoldSrcStudioModelImporterWithLimits {
public:
    explicit CountingStudioModelImporter(
        std::shared_ptr<VisualImporterCallCounts> calls)
        : calls_{std::move(calls)}
    {
    }

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return delegate_.id();
    }

    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override
    {
        ++calls_->model_probe_calls;
        return delegate_.probe(probe);
    }

    [[nodiscard]] assets::ModelAssetResult import(
        const assets::AssetSource& source) const override
    {
        ++calls_->model_import_calls;
        return delegate_.import(source);
    }

    [[nodiscard]] assets::ModelAssetResult import_with_limits(
        const assets::AssetSource& source,
        const goldsrc::studio::GoldSrcStudioModelImportLimits& limits)
        const override
    {
        ++calls_->model_import_calls;
        return delegate_.import_with_limits(source, limits);
    }

private:
    std::shared_ptr<VisualImporterCallCounts> calls_;
    goldsrc::studio::GoldSrcStudioModelImporter delegate_;
};

class CountingSpriteImporter final : public assets::ISpriteImporter {
public:
    explicit CountingSpriteImporter(
        std::shared_ptr<VisualImporterCallCounts> calls)
        : calls_{std::move(calls)}
    {
    }

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return delegate_.id();
    }

    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override
    {
        ++calls_->sprite_probe_calls;
        return delegate_.probe(probe);
    }

    [[nodiscard]] assets::SpriteAssetResult import(
        const assets::AssetSource& source) const override
    {
        ++calls_->sprite_import_calls;
        return delegate_.import(source);
    }

private:
    std::shared_ptr<VisualImporterCallCounts> calls_;
    goldsrc::sprite::GoldSrcSpriteImporter delegate_;
};

class IncompatibleProductionIdStudioImporter final
    : public assets::IModelImporter {
public:
    explicit IncompatibleProductionIdStudioImporter(
        std::shared_ptr<VisualImporterCallCounts> calls)
        : calls_{std::move(calls)}
    {
    }

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return delegate_.id();
    }

    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override
    {
        ++calls_->model_probe_calls;
        return delegate_.probe(probe);
    }

    [[nodiscard]] assets::ModelAssetResult import(
        const assets::AssetSource& source) const override
    {
        ++calls_->model_import_calls;
        return delegate_.import(source);
    }

private:
    std::shared_ptr<VisualImporterCallCounts> calls_;
    goldsrc::studio::GoldSrcStudioModelImporter delegate_;
};

void register_counting_visual_importers(
    assets::AssetImporterRegistries& registries,
    const std::shared_ptr<VisualImporterCallCounts>& calls)
{
    REQUIRE(registries.models.register_importer(
        std::make_unique<CountingStudioModelImporter>(calls),
        goldsrc::studio::kGoldSrcStudioModelImporterPriority));
    REQUIRE(registries.sprites.register_importer(
        std::make_unique<CountingSpriteImporter>(calls),
        goldsrc::sprite::kGoldSrcSpriteImporterPriority));
}

struct TestRootFileSnapshot {
    std::string relative_name;
    std::vector<char> bytes;

    [[nodiscard]] friend bool operator==(
        const TestRootFileSnapshot&,
        const TestRootFileSnapshot&) = default;
};

[[nodiscard]] std::vector<TestRootFileSnapshot> snapshot_test_root(
    const std::filesystem::path& root)
{
    std::vector<TestRootFileSnapshot> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{
             root}) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream stream{entry.path(), std::ios::binary};
        if (!stream) {
            throw std::runtime_error{"Unable to snapshot synthetic test file"};
        }
        std::vector<char> bytes{
            std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
        result.push_back(TestRootFileSnapshot{
            std::filesystem::relative(entry.path(), root).generic_string(),
            std::move(bytes)});
    }
    std::sort(
        result.begin(), result.end(),
        [](const TestRootFileSnapshot& left,
           const TestRootFileSnapshot& right) {
            return left.relative_name < right.relative_name;
        });
    return result;
}

[[nodiscard]] std::vector<std::uint64_t> model_structure_snapshot(
    const assets::ModelAsset& model)
{
    if (!model.skeletal_data) {
        throw std::runtime_error{"Synthetic model omitted skeletal data"};
    }
    const auto& skeletal = *model.skeletal_data;
    const auto& statistics = skeletal.statistics;
    std::uint64_t submodel_vertices = 0U;
    std::uint64_t submodel_indices = 0U;
    std::uint64_t submodel_meshes = 0U;
    for (const auto& submodel : skeletal.submodels) {
        submodel_vertices += submodel.vertices.size();
        submodel_indices += submodel.indices.size();
        submodel_meshes += submodel.meshes.size();
    }
    std::uint64_t texture_rgba_bytes = 0U;
    for (const auto& texture : skeletal.textures) {
        texture_rgba_bytes += texture.rgba8_level_zero.size();
    }
    return {
        static_cast<std::uint64_t>(model.vertices.size()),
        static_cast<std::uint64_t>(model.indices.size()),
        static_cast<std::uint64_t>(skeletal.bones.size()),
        static_cast<std::uint64_t>(skeletal.bone_controllers.size()),
        static_cast<std::uint64_t>(skeletal.hitboxes.size()),
        static_cast<std::uint64_t>(skeletal.attachments.size()),
        static_cast<std::uint64_t>(skeletal.bodyparts.size()),
        static_cast<std::uint64_t>(skeletal.submodels.size()),
        submodel_vertices,
        submodel_indices,
        submodel_meshes,
        static_cast<std::uint64_t>(skeletal.textures.size()),
        texture_rgba_bytes,
        static_cast<std::uint64_t>(skeletal.skin_families.size()),
        static_cast<std::uint64_t>(skeletal.sequences.size()),
        static_cast<std::uint64_t>(skeletal.sequence_groups.size()),
        statistics.source_count,
        statistics.bone_count,
        statistics.bodypart_count,
        statistics.submodel_count,
        statistics.mesh_count,
        statistics.emitted_vertex_count,
        statistics.emitted_triangle_count,
        statistics.texture_count,
        statistics.skin_family_count,
        statistics.sequence_count,
        statistics.sequence_group_count,
        statistics.animation_run_count,
        statistics.animation_value_bytes,
    };
}

[[nodiscard]] std::vector<std::uint64_t> sprite_structure_snapshot(
    const assets::SpriteAsset& sprite)
{
    if (!sprite.source_data) {
        throw std::runtime_error{"Synthetic sprite omitted source data"};
    }
    const auto& source = *sprite.source_data;
    std::uint64_t retained_indexed_bytes = 0U;
    std::uint64_t retained_rgba_bytes = 0U;
    for (const auto& frame : source.indexed_frames) {
        retained_indexed_bytes += frame.indexed_pixels.size();
        retained_rgba_bytes += frame.derived_rgba8.size();
    }
    return {
        static_cast<std::uint64_t>(sprite.frames.size()),
        static_cast<std::uint64_t>(source.indexed_frames.size()),
        static_cast<std::uint64_t>(source.groups.size()),
        static_cast<std::uint64_t>(source.top_level_entries.size()),
        retained_indexed_bytes,
        retained_rgba_bytes,
        source.statistics.top_level_entry_count,
        source.statistics.flattened_frame_count,
        source.statistics.group_count,
        source.statistics.indexed_pixel_byte_count,
        source.statistics.derived_rgba_byte_count,
    };
}

[[nodiscard]] std::shared_ptr<const local::LocalResourceEnvironment>
make_environment(
    const ScopedLocalResourceTestRoot& root,
    const std::string_view game = "valve")
{
    auto roots = local::LocalResourceSearchRoots::create(root.path(), game);
    INFO((roots.error ? roots.error->context : std::string{}));
    REQUIRE(roots);
    REQUIRE(roots.roots);
    auto created = local::LocalResourceEnvironment::create(
        std::move(*roots.roots));
    INFO((created.error ? created.error->context : std::string{}));
    REQUIRE(created);
    REQUIRE(created.environment);
    return std::shared_ptr<const local::LocalResourceEnvironment>{
        std::move(created.environment)};
}

[[nodiscard]] local_assets::LocalAssetSource open_local_source(
    const std::shared_ptr<const local::LocalResourceEnvironment>& environment,
    const std::string_view virtual_name)
{
    auto classified = local::LocalVirtualResourceName::create(virtual_name);
    INFO((classified.error ? classified.error->context : std::string{}));
    REQUIRE(classified);
    REQUIRE(classified.name);
    auto resolved = environment->resolver().resolve(*classified.name);
    INFO(resolved.context);
    REQUIRE(resolved);
    REQUIRE(resolved.file);
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto size = resolved.file->file_size();
    resolved.file->close();
    auto locator = environment->make_locator(
        root_id, std::move(*classified.name), identity, size);
    INFO((locator.error ? locator.error->context : std::string{}));
    REQUIRE(locator);
    REQUIRE(locator.locator);
    local_assets::LocalAssetSourceOpener opener;
    auto begun = opener.begin(*locator.locator, environment);
    INFO((begun.error ? begun.error->context : std::string{}));
    REQUIRE(begun);
    REQUIRE(begun.operation);
    auto operation = std::move(*begun.operation);
    for (std::size_t update = 0U; update < 64U; ++update) {
        if (operation.state() ==
            local_assets::LocalAssetSourceOpenState::source_ready) {
            break;
        }
        operation.update(
            local_assets::LocalAssetSourceOpenTimePoint{} +
            std::chrono::milliseconds{static_cast<std::int64_t>(update)});
    }
    INFO((operation.error() ? operation.error()->context : std::string{}));
    REQUIRE(operation.state() ==
            local_assets::LocalAssetSourceOpenState::source_ready);
    auto source = operation.take_result();
    REQUIRE(source);
    return std::move(*source);
}

void update_until_terminal(
    visual::GoldSrcStudioModelBundleImportOperation& operation,
    const visual::GoldSrcStudioModelBundleImportTimePoint start = {})
{
    for (std::size_t update = 0U; update < 256U; ++update) {
        if (operation.terminal()) {
            return;
        }
        operation.update(
            start +
            std::chrono::milliseconds{static_cast<std::int64_t>(update)});
    }
    FAIL("Studio bundle import operation did not terminate");
}

void update_until_terminal(
    visual::GoldSrcVisualAssetImportOperation& operation,
    const visual::GoldSrcVisualAssetImportTimePoint start = {})
{
    for (std::size_t update = 0U; update < 256U; ++update) {
        if (operation.terminal()) {
            return;
        }
        operation.update(
            start +
            std::chrono::milliseconds{static_cast<std::int64_t>(update)});
    }
    FAIL("General visual asset import operation did not terminate");
}

[[nodiscard]] std::vector<std::byte>
synthetic_two_external_sequence_main()
{
    auto bytes = hlclient::tests::literal_minimal_goldsrc_studio_v10();
    constexpr auto sequence_size =
        hlclient::goldsrc::studio::kGoldSrcStudioSequenceWireSize;
    constexpr auto group_size =
        hlclient::goldsrc::studio::kGoldSrcStudioSequenceGroupWireSize;
    const std::vector<std::byte> source_sequence{
        bytes.begin() + static_cast<std::ptrdiff_t>(
                            hlclient::tests::kSyntheticStudioSequenceOffset),
        bytes.begin() + static_cast<std::ptrdiff_t>(
                            hlclient::tests::kSyntheticStudioSequenceOffset +
                            sequence_size)};
    const auto sequences_offset = bytes.size();
    const auto groups_offset = sequences_offset + 2U * sequence_size;
    bytes.resize(groups_offset + 3U * group_size, std::byte{0});
    std::copy(
        source_sequence.begin(), source_sequence.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(sequences_offset));
    std::copy(
        source_sequence.begin(), source_sequence.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(
                            sequences_offset + sequence_size));

    hlclient::tests::studio_write_i32le(
        bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    hlclient::tests::studio_write_i32le(bytes, 164U, 2);
    hlclient::tests::studio_write_i32le(
        bytes, 168U, static_cast<std::int32_t>(sequences_offset));
    hlclient::tests::studio_write_i32le(bytes, 172U, 3);
    hlclient::tests::studio_write_i32le(
        bytes, 176U, static_cast<std::int32_t>(groups_offset));

    for (std::size_t index = 0U; index < 2U; ++index) {
        const auto sequence_offset = sequences_offset + index * sequence_size;
        hlclient::tests::studio_write_fixed_string(
            bytes,
            sequence_offset,
            32U,
            index == 0U ? "external01" : "external02");
        hlclient::tests::studio_write_i32le(
            bytes, sequence_offset + 124U, 76);
        hlclient::tests::studio_write_i32le(
            bytes,
            sequence_offset + 156U,
            static_cast<std::int32_t>(index + 1U));
    }
    for (std::size_t index = 0U; index < 3U; ++index) {
        const auto group_offset = groups_offset + index * group_size;
        hlclient::tests::studio_write_fixed_string(
            bytes,
            group_offset,
            32U,
            index == 0U ? "default"
                        : index == 1U ? "group01" : "group02");
        if (index != 0U) {
            hlclient::tests::studio_write_fixed_string(
                bytes,
                group_offset + 32U,
                64U,
                index == 1U ? "ignored\\header\\first.mdl"
                            : "ignored\\header\\second.mdl");
        }
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte>
synthetic_two_attachment_studio_main()
{
    auto bytes = hlclient::tests::literal_minimal_goldsrc_studio_v10();
    constexpr auto record_size =
        goldsrc::studio::kGoldSrcStudioAttachmentWireSize;
    const std::vector<std::byte> attachment{
        bytes.begin() + static_cast<std::ptrdiff_t>(
                            hlclient::tests::kSyntheticStudioAttachmentOffset),
        bytes.begin() + static_cast<std::ptrdiff_t>(
                            hlclient::tests::kSyntheticStudioAttachmentOffset +
                            record_size)};
    const auto attachments_offset = bytes.size();
    bytes.insert(bytes.end(), attachment.begin(), attachment.end());
    bytes.insert(bytes.end(), attachment.begin(), attachment.end());
    hlclient::tests::studio_write_fixed_string(
        bytes, attachments_offset + record_size, 32U, "tag_two");
    hlclient::tests::studio_write_i32le(
        bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    hlclient::tests::studio_write_i32le(bytes, 212U, 2);
    hlclient::tests::studio_write_i32le(
        bytes, 216U, static_cast<std::int32_t>(attachments_offset));
    return bytes;
}

[[nodiscard]] visual::GoldSrcStudioModelBundleImportOperation begin_bundle(
    const local_assets::LocalAssetSource& main_source,
    const std::shared_ptr<const local::LocalResourceEnvironment>& environment,
    const visual::GoldSrcStudioModelBundleImportLimits limits = {})
{
    auto begun = visual::GoldSrcStudioModelBundleImportOperation::begin(
        main_source, environment, limits);
    INFO((begun.error ? begun.error->context : std::string{}));
    REQUIRE(begun);
    REQUIRE(begun.operation);
    return std::move(*begun.operation);
}

[[nodiscard]] goldsrc::ApprovedAssetSource open_approved_model_source(
    const std::shared_ptr<const local::LocalResourceEnvironment>& environment,
    const std::string_view virtual_name)
{
    const auto list = readiness::parse_resource_list({
        {2U, "maps/world.bsp", 1U, 0U, 0U},
        {2U, std::string{virtual_name}, 2U, 0U, 0U},
    });
    const auto inventory = readiness::build_inventory(list, *environment);
    const auto server = readiness::parse_server_info("maps/world.bsp");
    auto built = readiness::build_manifest(
        list, inventory, server, *environment);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    const auto* entry = built.state->find(goldsrc::ResourceType::model, 2U);
    REQUIRE(entry);
    auto plan = goldsrc::AssetDispatchPlanBuilder{}.build(
        *built.state, *entry);
    INFO((plan.error ? plan.error->context : std::string{}));
    REQUIRE(plan);
    REQUIRE(plan.plan);
    goldsrc::ApprovedAssetSourceOpener opener;
    auto begun = opener.begin(*plan.plan, environment);
    INFO((begun.error ? begun.error->context : std::string{}));
    REQUIRE(begun);
    REQUIRE(begun.operation);
    auto operation = std::move(*begun.operation);
    for (std::size_t update = 0U; update < 64U; ++update) {
        if (operation.state() ==
            goldsrc::ApprovedAssetSourceOpenState::source_ready) {
            break;
        }
        operation.update(
            goldsrc::ApprovedAssetSourceOpenTimePoint{} +
            std::chrono::milliseconds{static_cast<std::int64_t>(update)});
    }
    INFO((operation.error() ? operation.error()->context : std::string{}));
    REQUIRE(operation.state() ==
            goldsrc::ApprovedAssetSourceOpenState::source_ready);
    auto approved = operation.take_result();
    REQUIRE(approved);
    return std::move(*approved);
}

TEST_CASE(
    "GoldSrc visual import operation exposes bounded caller-driven defaults",
    "[goldsrc][visual-assets][operation][limits]")
{
    CHECK(visual::valid_goldsrc_studio_model_bundle_import_limits({}));
    CHECK(visual::valid_goldsrc_visual_asset_import_limits({}));
    CHECK(visual::to_string(
              visual::GoldSrcStudioModelBundleImportState::inspecting_main) ==
          "inspecting_main");
    CHECK(visual::to_string(
              visual::GoldSrcVisualAssetImportState::dispatching) ==
          "dispatching");

    visual::GoldSrcStudioModelBundleImportLimits invalid;
    invalid.companion_source_open.maximum_open_sources = 2U;
    CHECK_FALSE(
        visual::valid_goldsrc_studio_model_bundle_import_limits(invalid));
    invalid = {};
    invalid.timeout =
        visual::kHardMaximumGoldSrcStudioModelBundleImportTimeout +
        std::chrono::milliseconds{1};
    CHECK_FALSE(
        visual::valid_goldsrc_studio_model_bundle_import_limits(invalid));
}

TEST_CASE(
    "Visual dispatch does not apply the Studio byte cap to sprites",
    "[goldsrc][visual-assets][operation][limits][cross-category]")
{
    visual::GoldSrcVisualAssetImportLimits limits;
    limits.studio_bundle.studio.maximum_main_source_bytes =
        goldsrc::studio::kGoldSrcStudioHeaderWireSize;
    REQUIRE(visual::valid_goldsrc_visual_asset_import_limits(limits));

    assets::AssetImporterRegistries registries;
    REQUIRE(goldsrc::register_builtin_asset_importers(registries));

    SECTION("a valid sprite larger than the configured Studio cap imports")
    {
        const auto sprite_bytes = sprite_fixture::literal_single_sprite();
        REQUIRE(sprite_bytes.size() >
                limits.studio_bundle.studio.maximum_main_source_bytes);
        ScopedLocalResourceTestRoot root;
        root.write("valve", "sprites/large-for-studio.spr", sprite_bytes);
        const auto environment = make_environment(root);
        const auto source =
            open_local_source(environment, "sprites/large-for-studio.spr");

        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            source, environment, registries, limits);
        INFO((begun.error ? begun.error->context : std::string{}));
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);
        INFO((operation.error() ? operation.error()->context
                                : std::string{}));
        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::asset_ready);
        REQUIRE(operation.result());
        CHECK(operation.result()->selected_category() ==
              assets::AssetImporterCategory::sprite);
        CHECK(std::holds_alternative<assets::SpriteAsset>(
            operation.result()->asset()));
        CHECK_FALSE(operation.result()->studio_sources());
    }

    SECTION("a self-contained Studio model over the cap is not published")
    {
        const auto model_bytes =
            hlclient::tests::literal_minimal_goldsrc_studio_v10();
        REQUIRE(model_bytes.size() >
                limits.studio_bundle.studio.maximum_main_source_bytes);
        ScopedLocalResourceTestRoot root;
        root.write("valve", "models/over-limit.mdl", model_bytes);
        const auto environment = make_environment(root);
        const auto source =
            open_local_source(environment, "models/over-limit.mdl");

        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            source, environment, registries, limits);
        INFO((begun.error ? begun.error->context : std::string{}));
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);
        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::failed);
        CHECK(operation.result() == nullptr);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcVisualAssetImportErrorCode::dispatch_failed);
        CHECK(operation.error()->dispatch_state ==
              assets::AssetDispatchState::import_failed);
        CHECK(operation.error()->asset_code ==
              assets::AssetErrorCode::MalformedData);
    }

    SECTION("self-contained Studio structural limits match direct import")
    {
        auto structural_limits = limits;
        structural_limits.studio_bundle.studio.maximum_main_source_bytes =
            goldsrc::studio::kGoldSrcStudioHardMaximumMainSourceBytes;
        structural_limits.studio_bundle.studio.maximum_attachments = 1U;
        REQUIRE(visual::valid_goldsrc_visual_asset_import_limits(
            structural_limits));
        ScopedLocalResourceTestRoot root;
        root.write(
            "valve", "models/two-attachments.mdl",
            synthetic_two_attachment_studio_main());
        const auto environment = make_environment(root);
        const auto source =
            open_local_source(environment, "models/two-attachments.mdl");
        const goldsrc::studio::GoldSrcStudioModelImporter direct_importer{
            structural_limits.studio_bundle.studio};
        auto direct = direct_importer.import(source.source());
        REQUIRE_FALSE(direct);
        const auto direct_code = direct.error().code;

        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            source, environment, registries, structural_limits);
        INFO((begun.error ? begun.error->context : std::string{}));
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);
        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcVisualAssetImportErrorCode::dispatch_failed);
        CHECK(operation.error()->dispatch_state ==
              assets::AssetDispatchState::import_failed);
        CHECK(operation.error()->asset_code == direct_code);
        CHECK(operation.result() == nullptr);
    }

    SECTION("a dependency Studio model over the caller cap fails before composition")
    {
        const auto model_bytes =
            hlclient::tests::synthetic_split_texture_main();
        REQUIRE(model_bytes.size() >
                limits.studio_bundle.studio.maximum_main_source_bytes);
        ScopedLocalResourceTestRoot root;
        root.write("valve", "models/over-limit-split.mdl", model_bytes);
        const auto environment = make_environment(root);
        const auto source = open_local_source(
            environment, "models/over-limit-split.mdl");

        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            source, environment, registries, limits);
        INFO((begun.error ? begun.error->context : std::string{}));
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);
        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::failed);
        CHECK(operation.result() == nullptr);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcVisualAssetImportErrorCode::dispatch_failed);
        CHECK(operation.error()->dispatch_state ==
              assets::AssetDispatchState::import_failed);
        CHECK(operation.error()->asset_code ==
              assets::AssetErrorCode::MalformedData);
    }
}

TEST_CASE(
    "Approved retained visual bytes are independent until Studio dependencies are selected",
    "[goldsrc][visual-assets][operation][approved][retained][drift]")
{
    SECTION("sprite survives backing-file removal")
    {
        ScopedLocalResourceTestRoot root;
        root.write("valve", "maps/world.bsp", "synthetic world placeholder");
        root.write(
            "valve", "sprites/retained.spr",
            sprite_fixture::literal_single_sprite());
        const auto environment = make_environment(root);
        auto approved =
            open_approved_model_source(environment, "sprites/retained.spr");
        const auto calls = std::make_shared<VisualImporterCallCounts>();
        assets::AssetImporterRegistries registries;
        register_counting_visual_importers(registries, calls);
        REQUIRE(std::filesystem::remove(
            root.path() / "valve" / "sprites" / "retained.spr"));

        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            approved, environment, registries);
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);

        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::asset_ready);
        REQUIRE(operation.result());
        CHECK(operation.result()->selected_category() ==
              assets::AssetImporterCategory::sprite);
        CHECK(calls->model_probe_calls == 1U);
        CHECK(calls->sprite_probe_calls == 1U);
        CHECK(calls->model_import_calls == 0U);
        CHECK(calls->sprite_import_calls == 1U);
    }

    SECTION("self-contained Studio survives backing-file mutation")
    {
        ScopedLocalResourceTestRoot root;
        root.write("valve", "maps/world.bsp", "synthetic world placeholder");
        root.write(
            "valve", "models/retained.mdl",
            hlclient::tests::literal_minimal_goldsrc_studio_v10());
        const auto environment = make_environment(root);
        auto approved =
            open_approved_model_source(environment, "models/retained.mdl");
        const auto calls = std::make_shared<VisualImporterCallCounts>();
        assets::AssetImporterRegistries registries;
        register_counting_visual_importers(registries, calls);
        root.write(
            "valve", "models/retained.mdl",
            std::vector<std::byte>{std::byte{0x00U}});

        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            approved, environment, registries);
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);

        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::asset_ready);
        REQUIRE(operation.result());
        CHECK(operation.result()->selected_category() ==
              assets::AssetImporterCategory::model);
        CHECK_FALSE(operation.result()->studio_sources());
        CHECK(calls->model_probe_calls == 1U);
        CHECK(calls->sprite_probe_calls == 1U);
        CHECK(calls->model_import_calls == 1U);
        CHECK(calls->sprite_import_calls == 0U);
    }

    SECTION("dependency Studio detects removed or mutated main evidence lazily")
    {
        for (const bool remove_main : {false, true}) {
            INFO(remove_main);
            ScopedLocalResourceTestRoot root;
            root.write(
                "valve", "maps/world.bsp", "synthetic world placeholder");
            root.write(
                "valve", "models/dependent.mdl",
                hlclient::tests::synthetic_split_texture_main());
            root.write(
                "valve", "models/dependentT.mdl",
                hlclient::tests::synthetic_texture_companion());
            const auto environment = make_environment(root);
            auto approved = open_approved_model_source(
                environment, "models/dependent.mdl");
            const auto calls = std::make_shared<VisualImporterCallCounts>();
            assets::AssetImporterRegistries registries;
            register_counting_visual_importers(registries, calls);
            if (remove_main) {
                REQUIRE(std::filesystem::remove(
                    root.path() / "valve" / "models" / "dependent.mdl"));
            } else {
                root.write(
                    "valve", "models/dependent.mdl",
                    std::vector<std::byte>{std::byte{0x00U}});
            }

            auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
                approved, environment, registries);
            REQUIRE(begun);
            REQUIRE(begun.operation);
            auto operation = std::move(*begun.operation);
            update_until_terminal(operation);

            REQUIRE(operation.state() ==
                    visual::GoldSrcVisualAssetImportState::dependency_invalid);
            REQUIRE(operation.error());
            CHECK(operation.error()->code ==
                  visual::GoldSrcVisualAssetImportErrorCode::
                      dependency_invalid);
            CHECK(operation.error()->dispatch_state ==
                  assets::AssetDispatchState::import_failed);
            CHECK(operation.error()->asset_code ==
                  assets::AssetErrorCode::ExternalDependencyRequired);
            CHECK(operation.error()->studio_bundle_code ==
                  visual::GoldSrcStudioModelBundleImportErrorCode::
                      approved_source_invalid);
            CHECK(operation.result() == nullptr);
            CHECK_FALSE(operation.take_result());
            CHECK(calls->model_probe_calls == 1U);
            CHECK(calls->sprite_probe_calls == 1U);
            CHECK(calls->model_import_calls == 1U);
            CHECK(calls->sprite_import_calls == 0U);
        }
    }
}

TEST_CASE(
    "Network-free visual composition invokes exactly one selected importer",
    "[goldsrc][visual-assets][operation][local-source][counting]")
{
    SECTION("self-contained Studio model")
    {
        ScopedLocalResourceTestRoot root;
        root.write(
            "valve",
            "models/self-contained.mdl",
            hlclient::tests::literal_minimal_goldsrc_studio_v10());
        const auto environment = make_environment(root);
        const auto source =
            open_local_source(environment, "models/self-contained.mdl");
        const auto calls = std::make_shared<VisualImporterCallCounts>();
        assets::AssetImporterRegistries registries;
        register_counting_visual_importers(registries, calls);
        const auto files_before = snapshot_test_root(root.path());
        std::optional<std::vector<std::uint64_t>> expected_structure;
        std::optional<std::vector<assets::AssetSourceFingerprint>>
            expected_fingerprints;

        for (std::size_t repetition = 0U; repetition < 20U; ++repetition) {
            INFO(repetition);
            auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
                source, environment, registries);
            INFO((begun.error ? begun.error->context : std::string{}));
            REQUIRE(begun);
            REQUIRE(begun.operation);
            auto operation = std::move(*begun.operation);
            update_until_terminal(operation);
            INFO((operation.error() ? operation.error()->context
                                    : std::string{}));
            REQUIRE(operation.state() ==
                    visual::GoldSrcVisualAssetImportState::asset_ready);
            auto imported = operation.take_result();
            REQUIRE(imported);
            CHECK(operation.result() == nullptr);
            CHECK_FALSE(operation.take_result().has_value());
            CHECK(imported->selected_category() ==
                  assets::AssetImporterCategory::model);
            CHECK(imported->selected_importer_id() ==
                  "model:goldsrc-studio-mdl-v10");
            CHECK(imported->dependency_statistics().source_count == 1U);
            CHECK(imported->dependency_statistics().total_source_bytes ==
                  source.byte_count());
            CHECK_FALSE(imported->studio_sources().has_value());
            const auto* model =
                std::get_if<assets::ModelAsset>(&imported->asset());
            REQUIRE(model);
            CHECK(model->identity.source_name ==
                  "models/self-contained.mdl");
            const auto structure = model_structure_snapshot(*model);
            const std::vector<assets::AssetSourceFingerprint> fingerprints{
                imported->source_fingerprints().begin(),
                imported->source_fingerprints().end()};
            if (!expected_structure) {
                expected_structure = structure;
                expected_fingerprints = fingerprints;
            } else {
                CHECK(structure == *expected_structure);
                CHECK(fingerprints == *expected_fingerprints);
            }
            CHECK(calls->model_probe_calls == repetition + 1U);
            CHECK(calls->sprite_probe_calls == repetition + 1U);
            CHECK(calls->model_import_calls == repetition + 1U);
            CHECK(calls->sprite_import_calls == 0U);
        }
        CHECK(snapshot_test_root(root.path()) == files_before);
    }

    SECTION("Studio model with a split texture dependency")
    {
        ScopedLocalResourceTestRoot root;
        root.write(
            "valve",
            "models/split.mdl",
            hlclient::tests::synthetic_split_texture_main());
        root.write(
            "valve",
            "models/splitT.mdl",
            hlclient::tests::synthetic_texture_companion());
        const auto environment = make_environment(root);
        const auto source = open_local_source(environment, "models/split.mdl");
        const auto texture_source =
            open_local_source(environment, "models/splitT.mdl");
        const auto calls = std::make_shared<VisualImporterCallCounts>();
        assets::AssetImporterRegistries registries;
        register_counting_visual_importers(registries, calls);
        const auto files_before = snapshot_test_root(root.path());
        std::optional<std::vector<std::uint64_t>> expected_structure;
        std::optional<std::vector<assets::AssetSourceFingerprint>>
            expected_fingerprints;

        for (std::size_t repetition = 0U; repetition < 20U; ++repetition) {
            INFO(repetition);
            auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
                source, environment, registries);
            INFO((begun.error ? begun.error->context : std::string{}));
            REQUIRE(begun);
            REQUIRE(begun.operation);
            auto operation = std::move(*begun.operation);
            update_until_terminal(operation);
            INFO((operation.error() ? operation.error()->context
                                    : std::string{}));
            REQUIRE(operation.state() ==
                    visual::GoldSrcVisualAssetImportState::asset_ready);
            auto imported = operation.take_result();
            REQUIRE(imported);
            CHECK(operation.result() == nullptr);
            CHECK_FALSE(operation.take_result().has_value());
            CHECK(imported->selected_category() ==
                  assets::AssetImporterCategory::model);
            CHECK(imported->selected_importer_id() ==
                  "model:goldsrc-studio-mdl-v10");
            CHECK(imported->dependency_statistics().source_count == 2U);
            CHECK(imported->dependency_statistics().texture_companion_present);
            CHECK(imported->source_fingerprints().size() == 2U);
            REQUIRE(imported->studio_sources());
            const auto& sources = *imported->studio_sources();
            CHECK(sources.resolved_dependency_plan().main_root_id() ==
                  source.root_id());
            CHECK(sources.main_source_identity().root_id() ==
                  source.root_id());
            CHECK(sources.main_source_identity().virtual_resource_id() ==
                  source.virtual_resource_id());
            CHECK(sources.main_source_identity().stable_file_identity() ==
                  source.expected_identity());
            REQUIRE(sources.texture_source_identity());
            CHECK(sources.texture_source_identity()->root_id() ==
                  source.root_id());
            CHECK(sources.texture_source_identity()->virtual_resource_id() ==
                  texture_source.virtual_resource_id());
            CHECK(sources.texture_source_identity()->stable_file_identity() ==
                  texture_source.expected_identity());
            const auto* model =
                std::get_if<assets::ModelAsset>(&imported->asset());
            REQUIRE(model);
            CHECK(model->identity.source_name == "models/split.mdl");
            const auto structure = model_structure_snapshot(*model);
            const std::vector<assets::AssetSourceFingerprint> fingerprints{
                imported->source_fingerprints().begin(),
                imported->source_fingerprints().end()};
            if (!expected_structure) {
                expected_structure = structure;
                expected_fingerprints = fingerprints;
            } else {
                CHECK(structure == *expected_structure);
                CHECK(fingerprints == *expected_fingerprints);
            }
            CHECK(calls->model_probe_calls == repetition + 1U);
            CHECK(calls->sprite_probe_calls == repetition + 1U);
            CHECK(calls->model_import_calls == repetition + 1U);
            CHECK(calls->sprite_import_calls == 0U);
        }
        CHECK(snapshot_test_root(root.path()) == files_before);
    }

    SECTION("sprite")
    {
        ScopedLocalResourceTestRoot root;
        root.write(
            "valve", "sprites/test.spr",
            sprite_fixture::literal_group_sprite());
        const auto environment = make_environment(root);
        const auto source = open_local_source(environment, "sprites/test.spr");
        const auto calls = std::make_shared<VisualImporterCallCounts>();
        assets::AssetImporterRegistries registries;
        register_counting_visual_importers(registries, calls);
        const auto files_before = snapshot_test_root(root.path());
        std::optional<std::vector<std::uint64_t>> expected_structure;
        std::optional<std::vector<assets::AssetSourceFingerprint>>
            expected_fingerprints;

        for (std::size_t repetition = 0U; repetition < 20U; ++repetition) {
            INFO(repetition);
            auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
                source, environment, registries);
            INFO((begun.error ? begun.error->context : std::string{}));
            REQUIRE(begun);
            REQUIRE(begun.operation);
            auto operation = std::move(*begun.operation);
            update_until_terminal(operation);
            INFO((operation.error() ? operation.error()->context
                                    : std::string{}));
            REQUIRE(operation.state() ==
                    visual::GoldSrcVisualAssetImportState::asset_ready);
            auto imported = operation.take_result();
            REQUIRE(imported);
            CHECK(operation.result() == nullptr);
            CHECK_FALSE(operation.take_result().has_value());
            CHECK(imported->selected_category() ==
                  assets::AssetImporterCategory::sprite);
            CHECK(imported->selected_importer_id() ==
                  "sprite:goldsrc-sprite-v2");
            CHECK(imported->dependency_statistics().source_count == 1U);
            CHECK(imported->dependency_statistics().total_source_bytes ==
                  source.byte_count());
            CHECK_FALSE(imported->studio_sources().has_value());
            const auto* sprite =
                std::get_if<assets::SpriteAsset>(&imported->asset());
            REQUIRE(sprite);
            CHECK(sprite->identity.source_name == "sprites/test.spr");
            REQUIRE(sprite->source_data);
            CHECK(sprite->source_data->source_version == 2);
            CHECK(sprite->source_data->orientation ==
                  assets::SpriteOrientation::view_parallel_oriented);
            CHECK(sprite->source_data->texture_format ==
                  assets::SpriteTextureFormat::normal);
            const auto structure = sprite_structure_snapshot(*sprite);
            const std::vector<assets::AssetSourceFingerprint> fingerprints{
                imported->source_fingerprints().begin(),
                imported->source_fingerprints().end()};
            if (!expected_structure) {
                expected_structure = structure;
                expected_fingerprints = fingerprints;
            } else {
                CHECK(structure == *expected_structure);
                CHECK(fingerprints == *expected_fingerprints);
            }
            CHECK(calls->model_probe_calls == repetition + 1U);
            CHECK(calls->sprite_probe_calls == repetition + 1U);
            CHECK(calls->model_import_calls == 0U);
            CHECK(calls->sprite_import_calls == repetition + 1U);
        }
        CHECK(snapshot_test_root(root.path()) == files_before);
    }

    SECTION("production Studio ID requires the constrained-instance contract")
    {
        ScopedLocalResourceTestRoot root;
        root.write(
            "valve", "models/incompatible.mdl",
            hlclient::tests::literal_minimal_goldsrc_studio_v10());
        const auto environment = make_environment(root);
        const auto source =
            open_local_source(environment, "models/incompatible.mdl");
        const auto calls = std::make_shared<VisualImporterCallCounts>();
        assets::AssetImporterRegistries registries;
        REQUIRE(registries.models.register_importer(
            std::make_unique<IncompatibleProductionIdStudioImporter>(calls),
            goldsrc::studio::kGoldSrcStudioModelImporterPriority));
        REQUIRE(registries.sprites.register_importer(
            std::make_unique<CountingSpriteImporter>(calls),
            goldsrc::sprite::kGoldSrcSpriteImporterPriority));

        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            source, environment, registries);
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);

        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcVisualAssetImportErrorCode::dispatch_failed);
        CHECK(operation.error()->asset_code ==
              assets::AssetErrorCode::ImportFailed);
        CHECK(operation.result() == nullptr);
        CHECK(calls->model_probe_calls == 1U);
        CHECK(calls->sprite_probe_calls == 1U);
        CHECK(calls->model_import_calls == 0U);
        CHECK(calls->sprite_import_calls == 0U);
    }
}

TEST_CASE(
    "Studio bundle operation imports split textures and sequence groups from the exact main root",
    "[goldsrc][visual-assets][operation][exact-root]")
{
    SECTION("split texture")
    {
        ScopedLocalResourceTestRoot root;
        root.write(
            "valve",
            "models/foo.mdl",
            hlclient::tests::synthetic_split_texture_main());
        root.write(
            "valve",
            "models/fooT.mdl",
            hlclient::tests::synthetic_texture_companion());
        const auto environment = make_environment(root);
        const auto main_source =
            open_local_source(environment, "models/foo.mdl");
        const auto texture_source =
            open_local_source(environment, "models/fooT.mdl");
        auto operation = begin_bundle(main_source, environment);
        update_until_terminal(operation);
        INFO((operation.error() ? operation.error()->context : std::string{}));
        REQUIRE(operation.state() ==
                visual::GoldSrcStudioModelBundleImportState::model_ready);
        auto imported = operation.take_result();
        REQUIRE(imported);
        REQUIRE(imported->model().skeletal_data);
        CHECK(imported->sources().statistics().source_count == 2U);
        CHECK(imported->sources().statistics().texture_companion_present);
        REQUIRE(imported->sources().texture_source());
        CHECK(imported->sources()
                  .texture_source()
                  ->virtual_path()
                  .generic_string() == "models/fooT.mdl");
        CHECK(imported->sources().sequence_group_sources().empty());
        const auto& main_identity =
            imported->sources().main_source_identity();
        CHECK(main_identity.root_id() == main_source.root_id());
        CHECK(main_identity.virtual_resource_id() ==
              main_source.virtual_resource_id());
        CHECK(main_identity.stable_file_identity() ==
              main_source.expected_identity());
        REQUIRE(imported->sources().texture_source_identity());
        const auto& texture_identity =
            *imported->sources().texture_source_identity();
        CHECK(texture_identity.root_id() == main_source.root_id());
        CHECK(texture_identity.virtual_resource_id() ==
              texture_source.virtual_resource_id());
        CHECK(texture_identity.stable_file_identity() ==
              texture_source.expected_identity());
        const auto& resolved =
            imported->sources().resolved_dependency_plan();
        CHECK(resolved.main_root_id() == main_source.root_id());
        CHECK(resolved.main_identity() == main_source.expected_identity());
        CHECK(resolved.main_fingerprint() ==
              imported->sources().main_fingerprint());
        CHECK(resolved.main_virtual_name().value() == "models/foo.mdl");
        REQUIRE(resolved.texture_companion_name());
        CHECK(resolved.texture_companion_name()->value() ==
              "models/fooT.mdl");
        CHECK(resolved.sequence_group_dependencies().empty());
        CHECK(resolved.compatibility_profile() ==
              assets::ModelSkeletalCompatibilityProfile::goldsrc_studio_v10);
        CHECK(resolved.evidence_profile() ==
              assets::ModelSkeletalEvidenceProfile::
                  public_valve_wire_profile);
        CHECK(operation.progress().source_open_attempt_count == 1U);
        CHECK(operation.progress().source_count_ready == 2U);
    }

    SECTION("sequence group 01")
    {
        ScopedLocalResourceTestRoot root;
        root.write(
            "valve",
            "models/group.mdl",
            hlclient::tests::synthetic_external_sequence_main());
        root.write(
            "valve",
            "models/group01.mdl",
            hlclient::tests::synthetic_sequence_group_01());
        const auto environment = make_environment(root);
        const auto main_source =
            open_local_source(environment, "models/group.mdl");
        const auto group_source =
            open_local_source(environment, "models/group01.mdl");
        auto operation = begin_bundle(main_source, environment);
        update_until_terminal(operation);
        INFO((operation.error() ? operation.error()->context : std::string{}));
        REQUIRE(operation.state() ==
                visual::GoldSrcStudioModelBundleImportState::model_ready);
        auto imported = operation.take_result();
        REQUIRE(imported);
        REQUIRE(imported->model().skeletal_data);
        REQUIRE(imported->sources().sequence_group_sources().size() == 1U);
        CHECK(imported->sources().sequence_group_sources().front().ordinal ==
              1U);
        CHECK(imported->sources()
                  .sequence_group_sources()
                  .front()
                  .source.virtual_path()
                  .generic_string() == "models/group01.mdl");
        CHECK(imported->sources().statistics().source_count == 2U);
        const auto& main_identity =
            imported->sources().main_source_identity();
        CHECK(main_identity.root_id() == main_source.root_id());
        CHECK(main_identity.virtual_resource_id() ==
              main_source.virtual_resource_id());
        CHECK(main_identity.stable_file_identity() ==
              main_source.expected_identity());
        const auto& group_identity =
            imported->sources().sequence_group_sources().front().identity;
        CHECK(group_identity.root_id() == main_source.root_id());
        CHECK(group_identity.virtual_resource_id() ==
              group_source.virtual_resource_id());
        CHECK(group_identity.stable_file_identity() ==
              group_source.expected_identity());
        const auto& resolved =
            imported->sources().resolved_dependency_plan();
        REQUIRE(resolved.sequence_group_dependencies().size() == 1U);
        CHECK(resolved.sequence_group_dependencies().front().ordinal == 1U);
        CHECK(resolved.sequence_group_dependencies()
                  .front()
                  .virtual_name.value() == "models/group01.mdl");
        // The fixture header says synthetic_minimal.mdl. Safe sibling naming
        // is nevertheless derived only from the verified virtual main name.
        CHECK(resolved.main_virtual_name().value() == "models/group.mdl");
        CHECK(resolved.sequence_group_dependencies()
                  .front()
                  .virtual_name.value()
                  .find("synthetic_minimal") == std::string_view::npos);
    }
}

TEST_CASE(
    "Studio bundle operation keeps self-contained models self-contained",
    "[goldsrc][visual-assets][operation][self-contained]")
{
    ScopedLocalResourceTestRoot root;
    root.write(
        "valve",
        "models/single.mdl",
        hlclient::tests::literal_minimal_goldsrc_studio_v10());
    const auto environment = make_environment(root);
    const auto main_source =
        open_local_source(environment, "models/single.mdl");
    auto operation = begin_bundle(main_source, environment);
    update_until_terminal(operation);
    INFO((operation.error() ? operation.error()->context : std::string{}));
    REQUIRE(operation.state() ==
            visual::GoldSrcStudioModelBundleImportState::model_ready);
    auto imported = operation.take_result();
    REQUIRE(imported);
    CHECK(imported->sources().statistics().source_count == 1U);
    CHECK_FALSE(imported->sources().statistics().texture_companion_present);
    CHECK(imported->sources().sequence_group_sources().empty());
    CHECK(imported->sources()
              .resolved_dependency_plan()
              .source_plan()
              .expected_source_count == 1U);
    CHECK(operation.progress().source_open_attempt_count == 0U);
}

TEST_CASE(
    "Studio bundle exact-root miss never combines a mod main with a fallback companion",
    "[goldsrc][visual-assets][operation][exact-root][negative]")
{
    ScopedLocalResourceTestRoot root;
    root.create_game("mymod");
    root.write(
        "mymod",
        "models/foo.mdl",
        hlclient::tests::synthetic_split_texture_main());
    root.write(
        "valve",
        "models/fooT.mdl",
        hlclient::tests::synthetic_texture_companion());
    const auto environment = make_environment(root, "mymod");
    const auto main_source = open_local_source(environment, "models/foo.mdl");
    REQUIRE(main_source.root_id().value() == 0U);

    auto operation = begin_bundle(main_source, environment);
    update_until_terminal(operation);

    REQUIRE(operation.state() ==
            visual::GoldSrcStudioModelBundleImportState::dependency_missing);
    REQUIRE(operation.error());
    CHECK(operation.error()->code ==
          visual::GoldSrcStudioModelBundleImportErrorCode::dependency_missing);
    CHECK(operation.error()->asset_code ==
          assets::AssetErrorCode::ExternalDependencyRequired);
    CHECK(operation.error()->resolution_code ==
          local::LocalResourceResolutionCode::not_found);
    CHECK(operation.result() == nullptr);
    CHECK(operation.progress().source_open_attempt_count == 0U);
}

TEST_CASE(
    "Studio bundle import is transactional under cancellation timeout and malformed companions",
    "[goldsrc][visual-assets][operation][transaction]")
{
    ScopedLocalResourceTestRoot root;
    root.write(
        "valve",
        "models/foo.mdl",
        hlclient::tests::synthetic_split_texture_main());
    root.write(
        "valve",
        "models/fooT.mdl",
        hlclient::tests::synthetic_texture_companion());
    const auto environment = make_environment(root);
    const auto main_source = open_local_source(environment, "models/foo.mdl");

    SECTION("cancel")
    {
        auto operation = begin_bundle(main_source, environment);
        operation.cancel();
        CHECK(operation.state() ==
              visual::GoldSrcStudioModelBundleImportState::cancelled);
        CHECK(operation.result() == nullptr);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcStudioModelBundleImportErrorCode::cancelled);
    }

    SECTION("timeout")
    {
        visual::GoldSrcStudioModelBundleImportLimits limits;
        limits.timeout = std::chrono::milliseconds{1};
        auto operation = begin_bundle(main_source, environment, limits);
        const auto start =
            visual::GoldSrcStudioModelBundleImportTimePoint{};
        operation.update(start);
        operation.update(start + std::chrono::milliseconds{1});
        CHECK(operation.state() ==
              visual::GoldSrcStudioModelBundleImportState::timed_out);
        CHECK(operation.result() == nullptr);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcStudioModelBundleImportErrorCode::timed_out);
    }

    SECTION("wrong texture companion profile")
    {
        auto malformed = hlclient::tests::synthetic_texture_companion();
        malformed[0U] = std::byte{0U};
        root.write("valve", "models/fooT.mdl", malformed);
        auto operation = begin_bundle(main_source, environment);
        update_until_terminal(operation);
        CHECK(operation.state() ==
              visual::GoldSrcStudioModelBundleImportState::
                  dependency_invalid);
        CHECK(operation.result() == nullptr);
        REQUIRE(operation.error());
        CHECK(operation.error()->asset_code ==
              assets::AssetErrorCode::MalformedData);
    }

    SECTION("wrong texture companion version")
    {
        auto malformed = hlclient::tests::synthetic_texture_companion();
        hlclient::tests::studio_write_i32le(malformed, 4U, 11);
        root.write("valve", "models/fooT.mdl", malformed);
        auto operation = begin_bundle(main_source, environment);
        update_until_terminal(operation);
        CHECK(operation.state() ==
              visual::GoldSrcStudioModelBundleImportState::
                  dependency_invalid);
        CHECK(operation.result() == nullptr);
        REQUIRE(operation.error());
        CHECK(operation.error()->asset_code ==
              assets::AssetErrorCode::MalformedData);
    }
}

TEST_CASE(
    "Studio sequence dependency requires group 01 and never substitutes another ordinal",
    "[goldsrc][visual-assets][operation][sequence-group][negative]")
{
    ScopedLocalResourceTestRoot root;
    root.write(
        "valve",
        "models/group.mdl",
        hlclient::tests::synthetic_external_sequence_main());
    root.write(
        "valve",
        "models/group02.mdl",
        hlclient::tests::synthetic_sequence_group_01());
    const auto environment = make_environment(root);
    const auto main_source =
        open_local_source(environment, "models/group.mdl");
    auto operation = begin_bundle(main_source, environment);
    update_until_terminal(operation);
    REQUIRE(operation.state() ==
            visual::GoldSrcStudioModelBundleImportState::dependency_missing);
    REQUIRE(operation.error());
    CHECK(operation.error()->asset_code ==
          assets::AssetErrorCode::ExternalDependencyRequired);
    CHECK(operation.error()->sequence_group_ordinal == 1U);
    CHECK(operation.progress().source_open_attempt_count == 0U);
}

TEST_CASE(
    "Studio bundle resolves multiple sequence groups in ordinal order and stops before a cumulative-byte overflow",
    "[goldsrc][visual-assets][operation][sequence-group][limits]")
{
    const auto main_bytes = synthetic_two_external_sequence_main();
    const auto group_bytes = hlclient::tests::synthetic_sequence_group_01();
    ScopedLocalResourceTestRoot root;
    root.write("valve", "models/multi.mdl", main_bytes);
    root.write("valve", "models/multi01.mdl", group_bytes);
    root.write("valve", "models/multi02.mdl", group_bytes);
    const auto environment = make_environment(root);
    const auto main_source =
        open_local_source(environment, "models/multi.mdl");
    const auto group_01_source =
        open_local_source(environment, "models/multi01.mdl");
    const auto group_02_source =
        open_local_source(environment, "models/multi02.mdl");

    SECTION("ordered exact-root dependencies import deterministically")
    {
        auto operation = begin_bundle(main_source, environment);
        update_until_terminal(operation);
        INFO((operation.error() ? operation.error()->context : std::string{}));
        REQUIRE(operation.state() ==
                visual::GoldSrcStudioModelBundleImportState::model_ready);
        auto imported = operation.take_result();
        REQUIRE(imported);
        const auto groups = imported->sources().sequence_group_sources();
        REQUIRE(groups.size() == 2U);
        CHECK(groups[0U].ordinal == 1U);
        CHECK(groups[0U].source.virtual_path().generic_string() ==
              "models/multi01.mdl");
        CHECK(groups[1U].ordinal == 2U);
        CHECK(groups[1U].source.virtual_path().generic_string() ==
              "models/multi02.mdl");
        CHECK(imported->sources().main_source_identity().root_id() ==
              main_source.root_id());
        CHECK(imported->sources()
                  .main_source_identity()
                  .virtual_resource_id() ==
              main_source.virtual_resource_id());
        CHECK(imported->sources()
                  .main_source_identity()
                  .stable_file_identity() ==
              main_source.expected_identity());
        CHECK(groups[0U].identity.root_id() == main_source.root_id());
        CHECK(groups[0U].identity.virtual_resource_id() ==
              group_01_source.virtual_resource_id());
        CHECK(groups[0U].identity.stable_file_identity() ==
              group_01_source.expected_identity());
        CHECK(groups[1U].identity.root_id() == main_source.root_id());
        CHECK(groups[1U].identity.virtual_resource_id() ==
              group_02_source.virtual_resource_id());
        CHECK(groups[1U].identity.stable_file_identity() ==
              group_02_source.expected_identity());
        const auto resolved = imported->sources()
                                  .resolved_dependency_plan()
                                  .sequence_group_dependencies();
        REQUIRE(resolved.size() == 2U);
        CHECK(resolved[0U].ordinal == 1U);
        CHECK(resolved[0U].virtual_name.value() ==
              "models/multi01.mdl");
        CHECK(resolved[1U].ordinal == 2U);
        CHECK(resolved[1U].virtual_name.value() ==
              "models/multi02.mdl");
        CHECK(imported->sources().statistics().source_count == 3U);
        CHECK(operation.progress().source_open_attempt_count == 2U);
    }

    SECTION("group 02 is not opened after group 01 reaches the byte budget")
    {
        visual::GoldSrcStudioModelBundleImportLimits limits;
        limits.bundle.maximum_total_source_bytes =
            main_bytes.size() + group_bytes.size();
        auto operation = begin_bundle(main_source, environment, limits);
        update_until_terminal(operation);
        REQUIRE(operation.state() ==
                visual::GoldSrcStudioModelBundleImportState::
                    dependency_invalid);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcStudioModelBundleImportErrorCode::
                  bundle_validation_failed);
        CHECK(operation.error()->asset_code ==
              assets::AssetErrorCode::MalformedData);
        CHECK(operation.error()->bundle_code ==
              visual::GoldSrcStudioModelSourceBundleErrorCode::
                  total_source_bytes_limit_exceeded);
        CHECK(operation.error()->sequence_group_ordinal == 2U);
        CHECK(operation.result() == nullptr);
        CHECK(operation.progress().source_open_attempt_count == 1U);
        CHECK(operation.progress().source_count_ready == 2U);
        CHECK(operation.progress().source_bytes_ready ==
              main_bytes.size() + group_bytes.size());
    }

    SECTION("the stricter parser byte budget also stops before group 02 open")
    {
        visual::GoldSrcStudioModelBundleImportLimits limits;
        limits.studio.maximum_main_source_bytes = main_bytes.size();
        limits.studio.maximum_total_bundle_bytes =
            main_bytes.size() + group_bytes.size();
        REQUIRE(limits.studio.maximum_total_bundle_bytes <
                limits.bundle.maximum_total_source_bytes);
        auto operation = begin_bundle(main_source, environment, limits);
        update_until_terminal(operation);
        REQUIRE(operation.state() ==
                visual::GoldSrcStudioModelBundleImportState::
                    dependency_invalid);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcStudioModelBundleImportErrorCode::
                  bundle_validation_failed);
        CHECK(operation.error()->studio_code ==
              hlclient::goldsrc::studio::GoldSrcStudioErrorCode::
                  source_limit_exceeded);
        CHECK_FALSE(operation.error()->bundle_code.has_value());
        CHECK(operation.error()->sequence_group_ordinal == 2U);
        CHECK(operation.result() == nullptr);
        CHECK(operation.progress().source_open_attempt_count == 1U);
        CHECK(operation.progress().source_count_ready == 2U);
    }
}

TEST_CASE(
    "Studio bundle operation enforces exact companion and total-bundle byte limits",
    "[goldsrc][visual-assets][operation][limits]")
{
    const auto main_bytes = hlclient::tests::synthetic_split_texture_main();
    const auto companion_bytes =
        hlclient::tests::synthetic_texture_companion();
    ScopedLocalResourceTestRoot root;
    root.write("valve", "models/foo.mdl", main_bytes);
    root.write("valve", "models/fooT.mdl", companion_bytes);
    const auto environment = make_environment(root);
    const auto main_source = open_local_source(environment, "models/foo.mdl");

    SECTION("exact limits are accepted")
    {
        visual::GoldSrcStudioModelBundleImportLimits limits;
        limits.companion_source_open.maximum_source_bytes =
            companion_bytes.size();
        limits.bundle.maximum_total_source_bytes =
            main_bytes.size() + companion_bytes.size();
        auto operation = begin_bundle(main_source, environment, limits);
        update_until_terminal(operation);
        INFO((operation.error() ? operation.error()->context : std::string{}));
        CHECK(operation.state() ==
              visual::GoldSrcStudioModelBundleImportState::model_ready);
    }

    SECTION("one byte below companion size fails closed")
    {
        visual::GoldSrcStudioModelBundleImportLimits limits;
        limits.companion_source_open.maximum_source_bytes =
            companion_bytes.size() - 1U;
        auto operation = begin_bundle(main_source, environment, limits);
        update_until_terminal(operation);
        CHECK(operation.state() ==
              visual::GoldSrcStudioModelBundleImportState::
                  dependency_invalid);
        CHECK(operation.result() == nullptr);
    }

    SECTION("one byte below total size fails closed")
    {
        visual::GoldSrcStudioModelBundleImportLimits limits;
        limits.bundle.maximum_total_source_bytes =
            main_bytes.size() + companion_bytes.size() - 1U;
        auto operation = begin_bundle(main_source, environment, limits);
        update_until_terminal(operation);
        CHECK(operation.state() ==
              visual::GoldSrcStudioModelBundleImportState::
                  dependency_invalid);
        CHECK(operation.result() == nullptr);
        REQUIRE(operation.error());
        CHECK(operation.error()->bundle_code ==
              visual::GoldSrcStudioModelSourceBundleErrorCode::
                  total_source_bytes_limit_exceeded);
    }
}

TEST_CASE(
    "Studio exact-root bundle imports are deterministic across twenty repeated split and group runs",
    "[goldsrc][visual-assets][operation][repeated]")
{
    ScopedLocalResourceTestRoot root;
    root.write(
        "valve",
        "models/split.mdl",
        hlclient::tests::synthetic_split_texture_main());
    root.write(
        "valve",
        "models/splitT.mdl",
        hlclient::tests::synthetic_texture_companion());
    root.write(
        "valve",
        "models/group.mdl",
        hlclient::tests::synthetic_external_sequence_main());
    root.write(
        "valve",
        "models/group01.mdl",
        hlclient::tests::synthetic_sequence_group_01());
    const auto environment = make_environment(root);
    const auto split = open_local_source(environment, "models/split.mdl");
    const auto group = open_local_source(environment, "models/group.mdl");
    const auto files_before = snapshot_test_root(root.path());
    std::optional<std::vector<std::uint64_t>> expected_split_structure;
    std::optional<std::vector<assets::AssetSourceFingerprint>>
        expected_split_fingerprints;
    std::optional<std::vector<std::uint64_t>> expected_group_structure;
    std::optional<std::vector<assets::AssetSourceFingerprint>>
        expected_group_fingerprints;

    for (std::size_t repetition = 0U; repetition < 20U; ++repetition) {
        INFO(repetition);
        auto split_operation = begin_bundle(split, environment);
        update_until_terminal(split_operation);
        REQUIRE(split_operation.state() ==
                visual::GoldSrcStudioModelBundleImportState::model_ready);
        CHECK(split_operation.progress().source_open_attempt_count == 1U);
        CHECK(split_operation.progress().source_count_ready == 2U);
        auto split_imported = split_operation.take_result();
        REQUIRE(split_imported);
        CHECK(split_operation.result() == nullptr);
        CHECK_FALSE(split_operation.take_result().has_value());
        CHECK(split_imported->sources().statistics().source_count == 2U);
        CHECK(split_imported->sources().statistics().texture_companion_present);
        REQUIRE(split_imported->sources().texture_source());
        REQUIRE(split_imported->sources().texture_fingerprint());
        const auto split_structure =
            model_structure_snapshot(split_imported->model());
        std::vector<assets::AssetSourceFingerprint> split_fingerprints{
            split_imported->sources().main_fingerprint()};
        split_fingerprints.push_back(
            *split_imported->sources().texture_fingerprint());
        if (!expected_split_structure) {
            expected_split_structure = split_structure;
            expected_split_fingerprints = split_fingerprints;
        } else {
            CHECK(split_structure == *expected_split_structure);
            CHECK(split_fingerprints == *expected_split_fingerprints);
        }

        auto group_operation = begin_bundle(group, environment);
        update_until_terminal(group_operation);
        REQUIRE(group_operation.state() ==
                visual::GoldSrcStudioModelBundleImportState::model_ready);
        CHECK(group_operation.progress().source_open_attempt_count == 1U);
        CHECK(group_operation.progress().source_count_ready == 2U);
        auto group_imported = group_operation.take_result();
        REQUIRE(group_imported);
        CHECK(group_operation.result() == nullptr);
        CHECK_FALSE(group_operation.take_result().has_value());
        REQUIRE(group_imported->sources().sequence_group_sources().size() ==
                1U);
        CHECK(group_imported->sources()
                  .sequence_group_sources()
                  .front()
                  .ordinal == 1U);
        CHECK(group_imported->sources().statistics().source_count == 2U);
        CHECK(group_imported->sources().statistics().sequence_group_source_count ==
              1U);
        const auto group_structure =
            model_structure_snapshot(group_imported->model());
        std::vector<assets::AssetSourceFingerprint> group_fingerprints{
            group_imported->sources().main_fingerprint(),
            group_imported->sources()
                .sequence_group_sources()
                .front()
                .fingerprint};
        if (!expected_group_structure) {
            expected_group_structure = group_structure;
            expected_group_fingerprints = group_fingerprints;
        } else {
            CHECK(group_structure == *expected_group_structure);
            CHECK(group_fingerprints == *expected_group_fingerprints);
        }
    }
    CHECK(snapshot_test_root(root.path()) == files_before);
}

TEST_CASE(
    "Studio companion header mutations fail closed without partial assets",
    "[goldsrc][visual-assets][operation][mutation]")
{
    ScopedLocalResourceTestRoot root;
    root.write(
        "valve",
        "models/foo.mdl",
        hlclient::tests::synthetic_split_texture_main());
    root.write(
        "valve",
        "models/fooT.mdl",
        hlclient::tests::synthetic_texture_companion());
    const auto environment = make_environment(root);
    const auto main_source = open_local_source(environment, "models/foo.mdl");
    constexpr std::size_t mutation_offsets[] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 72U, 73U,
        74U, 75U, 180U, 181U, 182U, 183U, 192U, 193U, 196U, 197U};

    for (const auto offset : mutation_offsets) {
        INFO(offset);
        auto mutated = hlclient::tests::synthetic_texture_companion();
        mutated[offset] ^= std::byte{0xFFU};
        root.write("valve", "models/fooT.mdl", mutated);
        auto operation = begin_bundle(main_source, environment);
        update_until_terminal(operation);
        CHECK(operation.state() ==
              visual::GoldSrcStudioModelBundleImportState::
                  dependency_invalid);
        CHECK(operation.result() == nullptr);
    }
}

TEST_CASE(
    "General approved visual operation retains manifest evidence and imports sprites repeatedly",
    "[goldsrc][visual-assets][operation][approved][sprite][repeated]")
{
    ScopedLocalResourceTestRoot root;
    const auto sprite_bytes = sprite_fixture::literal_single_sprite();
    root.write("valve", "maps/world.bsp", "synthetic world placeholder");
    root.write("valve", "sprites/test.spr", sprite_bytes);
    auto owned_environment = readiness::make_environment(root);
    std::shared_ptr<const local::LocalResourceEnvironment> environment{
        std::move(owned_environment)};
    const auto list = readiness::parse_resource_list({
        {2U, "maps/world.bsp", 1U, 0U, 0U},
        {2U, "sprites/test.spr", 2U, 0U, 0U},
    });
    const auto inventory = readiness::build_inventory(list, *environment);
    const auto server = readiness::parse_server_info("maps/world.bsp");
    auto built = readiness::build_manifest(
        list, inventory, server, *environment);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    const auto& manifest = *built.state;
    const auto* entry = manifest.find(goldsrc::ResourceType::model, 2U);
    REQUIRE(entry);
    auto plan = goldsrc::AssetDispatchPlanBuilder{}.build(manifest, *entry);
    INFO((plan.error ? plan.error->context : std::string{}));
    REQUIRE(plan);
    REQUIRE(plan.plan);
    goldsrc::ApprovedAssetSourceOpener source_opener;
    auto source_begun = source_opener.begin(*plan.plan, environment);
    INFO((source_begun.error ? source_begun.error->context : std::string{}));
    REQUIRE(source_begun);
    REQUIRE(source_begun.operation);
    auto source_operation = std::move(*source_begun.operation);
    for (std::size_t update = 0U; update < 64U; ++update) {
        if (source_operation.state() ==
            goldsrc::ApprovedAssetSourceOpenState::source_ready) {
            break;
        }
        source_operation.update(
            goldsrc::ApprovedAssetSourceOpenTimePoint{} +
            std::chrono::milliseconds{static_cast<std::int64_t>(update)});
    }
    REQUIRE(source_operation.state() ==
            goldsrc::ApprovedAssetSourceOpenState::source_ready);
    auto approved = source_operation.take_result();
    REQUIRE(approved);

    assets::AssetImporterRegistries registries;
    const auto registered =
        goldsrc::register_builtin_asset_importers(registries);
    INFO((registered.error ? registered.error->context : std::string{}));
    REQUIRE(registered);
    const auto files_before = snapshot_test_root(root.path());
    std::optional<std::vector<std::uint64_t>> expected_structure;
    std::optional<std::vector<assets::AssetSourceFingerprint>>
        expected_fingerprints;

    for (std::size_t repetition = 0U; repetition < 20U; ++repetition) {
        INFO(repetition);
        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            *approved, environment, registries);
        INFO((begun.error ? begun.error->context : std::string{}));
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        operation.update(visual::GoldSrcVisualAssetImportTimePoint{});
        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::asset_ready);
        auto imported = operation.take_result();
        REQUIRE(imported);
        CHECK(operation.result() == nullptr);
        CHECK_FALSE(operation.take_result().has_value());
        const auto* sprite =
            std::get_if<assets::SpriteAsset>(&imported->asset());
        REQUIRE(sprite);
        REQUIRE(sprite->source_data);
        CHECK(imported->wire_ordinal() == entry->wire_ordinal());
        CHECK(imported->resource_type() == goldsrc::ResourceType::model);
        CHECK(imported->resource_index() == 2U);
        CHECK(imported->selected_category() ==
              assets::AssetImporterCategory::sprite);
        CHECK(imported->selected_importer_id() ==
              "sprite:goldsrc-sprite-v2");
        CHECK(imported->dependency_statistics().source_count == 1U);
        CHECK(imported->dependency_statistics().total_source_bytes ==
              sprite_bytes.size());
        REQUIRE(imported->source_fingerprints().size() == 1U);
        CHECK(imported->compatibility_profile() ==
              approved->compatibility_profile());
        CHECK(imported->evidence_profile() == approved->evidence_profile());
        CHECK_FALSE(imported->studio_sources());
        const auto structure = sprite_structure_snapshot(*sprite);
        const std::vector<assets::AssetSourceFingerprint> fingerprints{
            imported->source_fingerprints().begin(),
            imported->source_fingerprints().end()};
        if (!expected_structure) {
            expected_structure = structure;
            expected_fingerprints = fingerprints;
        } else {
            CHECK(structure == *expected_structure);
            CHECK(fingerprints == *expected_fingerprints);
        }
    }
    CHECK(snapshot_test_root(root.path()) == files_before);
}

TEST_CASE(
    "General approved visual operation composes split Studio models transactionally",
    "[goldsrc][visual-assets][operation][approved][model][split]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/world.bsp", "synthetic world placeholder");
    root.write(
        "valve",
        "models/split.mdl",
        hlclient::tests::synthetic_split_texture_main());

    SECTION("approved model imports with owning exact-root provenance")
    {
        root.write(
            "valve",
            "models/splitT.mdl",
            hlclient::tests::synthetic_texture_companion());
        const auto environment = make_environment(root);
        const auto texture_source =
            open_local_source(environment, "models/splitT.mdl");
        auto approved =
            open_approved_model_source(environment, "models/split.mdl");
        assets::AssetImporterRegistries registries;
        const auto registered =
            goldsrc::register_builtin_asset_importers(registries);
        INFO((registered.error ? registered.error->context : std::string{}));
        REQUIRE(registered);

        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            approved, environment, registries);
        INFO((begun.error ? begun.error->context : std::string{}));
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);
        INFO((operation.error() ? operation.error()->context : std::string{}));
        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::asset_ready);
        auto imported = operation.take_result();
        REQUIRE(imported);
        CHECK(std::holds_alternative<assets::ModelAsset>(imported->asset()));
        CHECK(imported->wire_ordinal() == approved.wire_ordinal());
        CHECK(imported->resource_type() == goldsrc::ResourceType::model);
        CHECK(imported->resource_index() == 2U);
        CHECK(imported->selected_category() ==
              assets::AssetImporterCategory::model);
        CHECK(imported->selected_importer_id() ==
              "model:goldsrc-studio-mdl-v10");
        REQUIRE(imported->studio_sources());
        CHECK(imported->dependency_statistics().source_count == 2U);
        CHECK(imported->dependency_statistics().texture_companion_present);
        REQUIRE(imported->source_fingerprints().size() == 2U);
        const auto& sources = *imported->studio_sources();
        CHECK(sources.resolved_dependency_plan().main_root_id() ==
              approved.root_id());
        CHECK(sources.resolved_dependency_plan().main_identity() ==
              approved.expected_identity());
        CHECK(sources.main_source_identity().root_id() == approved.root_id());
        CHECK(sources.main_source_identity().virtual_resource_id() ==
              approved.virtual_resource_id());
        CHECK(sources.main_source_identity().stable_file_identity() ==
              approved.expected_identity());
        REQUIRE(sources.texture_source());
        REQUIRE(sources.texture_source_identity());
        CHECK(sources.texture_source_identity()->root_id() ==
              approved.root_id());
        CHECK(sources.texture_source_identity()->virtual_resource_id() ==
              texture_source.virtual_resource_id());
        CHECK(sources.texture_source_identity()->stable_file_identity() ==
              texture_source.expected_identity());
        CHECK(sources.texture_source()->virtual_path().generic_string() ==
              "models/splitT.mdl");
        CHECK(imported->compatibility_profile() ==
              approved.compatibility_profile());
        CHECK(imported->evidence_profile() == approved.evidence_profile());
    }

    SECTION("missing exact-root companion publishes no partial model")
    {
        const auto environment = make_environment(root);
        auto approved =
            open_approved_model_source(environment, "models/split.mdl");
        assets::AssetImporterRegistries registries;
        const auto registered =
            goldsrc::register_builtin_asset_importers(registries);
        REQUIRE(registered);
        auto begun = visual::GoldSrcVisualAssetImportOperation::begin(
            approved, environment, registries);
        REQUIRE(begun);
        REQUIRE(begun.operation);
        auto operation = std::move(*begun.operation);
        update_until_terminal(operation);
        REQUIRE(operation.state() ==
                visual::GoldSrcVisualAssetImportState::dependency_missing);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              visual::GoldSrcVisualAssetImportErrorCode::dependency_missing);
        CHECK(operation.error()->asset_code ==
              assets::AssetErrorCode::ExternalDependencyRequired);
        CHECK(operation.result() == nullptr);
        CHECK_FALSE(operation.take_result().has_value());
    }
}

} // namespace
