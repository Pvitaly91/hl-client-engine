#include "local_resource_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"

#include <hlclient/assets/asset_source.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;
namespace goldsrc = hlclient::goldsrc;
namespace local = hlclient::local_resources;
using hlclient::tests::ScopedLocalResourceTestRoot;
using namespace std::chrono_literals;

enum class SyntheticTextureStorage {
    missing,
    external,
    embedded,
};

struct ImportedSyntheticWorld {
    assets::AssetSource source;
    assets::WorldAsset world;
};

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view text)
{
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::vector<std::byte> make_bsp_bytes(
    const SyntheticTextureStorage storage,
    const std::string_view texture_name,
    const std::string_view wad_declaration = {},
    const bool malformed_palette = false)
{
    fixture::SyntheticBspBuilder builder;
    std::string entity{"{\n\"classname\" \"worldspawn\"\n"};
    if (!wad_declaration.empty()) {
        entity += "\"_wad\" \"";
        entity.append(wad_declaration);
        entity += "\"\n";
    }
    entity += "}\n";
    builder.lump(fixture::SyntheticBspLumpId::entities) = bytes_of(entity);

    if (storage == SyntheticTextureStorage::missing) {
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U>
            textures{std::nullopt};
        builder.set_texture_directory(textures);
    } else if (storage == SyntheticTextureStorage::external) {
        auto texture = fixture::synthetic_external_texture(texture_name);
        texture.width = 16U;
        texture.height = 16U;
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U>
            textures{texture};
        builder.set_texture_directory(textures);
    } else {
        auto texture = fixture::synthetic_embedded_texture(
            texture_name, 16U, 16U);
        constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
        texture.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U>
            textures{texture};
        builder.set_texture_directory(textures);
    }

    auto bytes = builder.build();
    if (storage == SyntheticTextureStorage::embedded) {
        const auto texture_lump = static_cast<std::size_t>(
            fixture::synthetic_read_i32le(bytes,
                fixture::synthetic_lump_descriptor_offset(
                    fixture::SyntheticBspLumpId::textures)));
        const auto record_relative = static_cast<std::size_t>(
            fixture::synthetic_read_i32le(bytes, texture_lump + 4U));
        const auto record = texture_lump + record_relative;
        constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
        const auto palette_count = record + 40U + pixel_byte_count;
        fixture::synthetic_write_u16le(bytes,
            palette_count,
            malformed_palette ? 255U : 256U);
        for (std::size_t index = 0U; index < 256U; ++index) {
            bytes[palette_count + 2U + (index * 3U)] =
                static_cast<std::byte>(index);
            bytes[palette_count + 2U + (index * 3U) + 1U] =
                static_cast<std::byte>(255U - index);
            bytes[palette_count + 2U + (index * 3U) + 2U] =
                static_cast<std::byte>(index ^ 0x5AU);
        }
    }
    return bytes;
}

[[nodiscard]] ImportedSyntheticWorld import_world(
    std::vector<std::byte> bytes)
{
    assets::AssetSourceMetadata metadata;
    metadata.content_size = bytes.size();
    auto created = assets::AssetSource::create(
        std::filesystem::path{"maps/texture_test.bsp"},
        std::move(bytes),
        std::move(metadata));
    if (!created || !created.source) {
        throw std::runtime_error{"Unable to retain synthetic BSP source"};
    }
    auto source = std::move(*created.source);
    const bsp::GoldSrcBspWorldImporter importer;
    auto imported = importer.import(source);
    if (!imported) {
        throw std::runtime_error{imported.error().context};
    }
    return ImportedSyntheticWorld{
        std::move(source), std::move(imported).value()};
}

[[nodiscard]] std::shared_ptr<const local::LocalResourceEnvironment>
make_environment(
    ScopedLocalResourceTestRoot& temporary,
    const std::string_view game = "valve")
{
    auto roots =
        local::LocalResourceSearchRoots::create(temporary.path(), game);
    if (!roots || !roots.roots) {
        throw std::runtime_error{"Unable to create synthetic search roots"};
    }
    local::LocalResourceResolverLimits resolver_limits;
    resolver_limits.maximum_file_size =
        local::kHardMaximumLocalResourceFileSize;
    auto environment = local::LocalResourceEnvironment::create(
        std::move(*roots.roots), resolver_limits);
    if (!environment || !environment.environment) {
        throw std::runtime_error{"Unable to create synthetic local environment"};
    }
    return std::shared_ptr<const local::LocalResourceEnvironment>{
        std::move(environment.environment)};
}

[[nodiscard]] goldsrc::WorldTextureImportOperation begin_operation(
    const ImportedSyntheticWorld& imported,
    std::shared_ptr<const local::LocalResourceEnvironment> environment,
    goldsrc::GoldSrcWorldTextureImportLimits limits = {})
{
    auto started = goldsrc::WorldTextureImportOperation::begin(
        imported.world,
        imported.source.bytes(),
        std::move(environment),
        std::move(limits));
    if (!started || !started.operation) {
        throw std::runtime_error{started.error
                ? started.error->context
                : "Unable to begin synthetic texture operation"};
    }
    return std::move(*started.operation);
}

void run_to_terminal(goldsrc::WorldTextureImportOperation& operation)
{
    auto now = goldsrc::WorldTextureImportTimePoint{};
    for (std::size_t update = 0U;
         update < 8'192U && !operation.terminal(); ++update) {
        operation.update(now);
        now += 1ms;
    }
    REQUIRE(operation.terminal());
}

TEST_CASE("World texture resolution decodes only used embedded BSP textures",
    "[world-textures][resolution][embedded]")
{
    ScopedLocalResourceTestRoot temporary;
    auto imported = import_world(make_bsp_bytes(
        SyntheticTextureStorage::embedded, "EMBEDDED"));
    auto operation = begin_operation(imported, make_environment(temporary));
    run_to_terminal(operation);

    REQUIRE(operation.state() == goldsrc::WorldTextureImportState::textures_ready);
    REQUIRE(operation.result() != nullptr);
    const auto& texture_set = *operation.result();
    CHECK(texture_set.complete_for_world_materials());
    REQUIRE(texture_set.texture_count() == 1U);
    REQUIRE(texture_set.binding_count() == imported.world.materials.size());
    CHECK(texture_set.bindings()[0].status ==
        assets::WorldMaterialTextureBindingStatus::resolved_embedded);
    CHECK(texture_set.bindings()[0].texture_asset_index == 0U);
    CHECK(texture_set.textures()[0].source_kind ==
        assets::WorldTextureSourceKind::embedded_bsp);
    CHECK(texture_set.textures()[0].mip_levels.size() == 4U);
    REQUIRE(texture_set.textures()[0].mip_levels[0].rgba_pixels.size() ==
        16U * 16U * 4U);
    CHECK(texture_set.textures()[0].mip_levels[0].rgba_pixels[0] ==
        std::byte{0U});
    CHECK(texture_set.textures()[0].mip_levels[0].rgba_pixels[1] ==
        std::byte{255U});
    CHECK(texture_set.textures()[0].mip_levels[0].rgba_pixels[2] ==
        std::byte{0x5AU});
    CHECK(texture_set.textures()[0].mip_levels[0].rgba_pixels[3] ==
        std::byte{255U});
    CHECK(operation.progress().wad_source_open_attempts == 0U);
    CHECK(operation.progress().wad_sources_open == 0U);

    auto taken = operation.take_result();
    REQUIRE(taken.has_value());
    CHECK(operation.result() == nullptr);
    CHECK_FALSE(operation.take_result().has_value());
}

TEST_CASE("External resolution opens declared WAD through the local environment",
    "[world-textures][resolution][wad3]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto wad = fixture::synthetic_valid_wad3("WAD_TEXTURE");
    temporary.write("valve", "safe.wad", wad.bytes);
    auto imported = import_world(make_bsp_bytes(
        SyntheticTextureStorage::external,
        "WAD_TEXTURE",
        R"(Z:\compiler\private\safe.wad;)"));
    auto operation = begin_operation(imported, make_environment(temporary));
    run_to_terminal(operation);

    REQUIRE(operation.state() == goldsrc::WorldTextureImportState::textures_ready);
    REQUIRE(operation.result() != nullptr);
    const auto& set = *operation.result();
    REQUIRE(set.texture_count() == 1U);
    CHECK(set.bindings()[0].status ==
        assets::WorldMaterialTextureBindingStatus::resolved_wad3);
    CHECK(set.textures()[0].source_kind ==
        assets::WorldTextureSourceKind::external_wad3);
    CHECK(set.textures()[0].source_archive_ordinal == 0U);
    REQUIRE(set.archive_metadata().size() == 1U);
    CHECK(set.archive_metadata()[0].status ==
        assets::WorldTextureArchiveStatus::resolved);
    CHECK(set.archive_metadata()[0].basename_byte_count ==
        std::string_view{"safe.wad"}.size());
    CHECK(set.archive_metadata()[0].source_root_ordinal == 0U);
    CHECK(operation.progress().wad_source_open_attempts == 1U);
    CHECK(operation.progress().wad_sources_open == 0U);
}

TEST_CASE("Declared WAD order is deterministic and stops after the first match",
    "[world-textures][resolution][order]")
{
    ScopedLocalResourceTestRoot temporary;
    auto first = fixture::SyntheticWad3Entry{};
    first.name = "ORDERED";
    first.payload = fixture::synthetic_goldsrc_miptex("ORDERED", 16U, 16U, 7U);
    auto second = fixture::SyntheticWad3Entry{};
    second.name = "ORDERED";
    second.payload = fixture::synthetic_goldsrc_miptex("ORDERED", 16U, 16U, 19U);
    temporary.write("valve", "first.wad", fixture::synthetic_wad3({first}).bytes);
    temporary.write("valve", "second.wad", fixture::synthetic_wad3({second}).bytes);

    auto imported = import_world(make_bsp_bytes(
        SyntheticTextureStorage::external,
        "ORDERED",
        R"(C:\compiler\first.wad;D:\compiler\second.wad;)"));
    auto operation = begin_operation(imported, make_environment(temporary));
    run_to_terminal(operation);

    REQUIRE(operation.result() != nullptr);
    REQUIRE(operation.result()->texture_count() == 1U);
    const auto& pixel =
        operation.result()->textures()[0].mip_levels[0].rgba_pixels;
    REQUIRE(pixel.size() >= 4U);
    CHECK(pixel[0] == std::byte{7U});
    CHECK(pixel[1] == std::byte{248U});
    CHECK(pixel[2] == std::byte{7U ^ 0x5AU});
    CHECK(operation.progress().wad_source_open_attempts == 1U);
    REQUIRE(operation.result()->archive_metadata().size() == 2U);
    CHECK(operation.result()->archive_metadata()[0].status ==
        assets::WorldTextureArchiveStatus::resolved);
    CHECK(operation.result()->archive_metadata()[1].status ==
        assets::WorldTextureArchiveStatus::not_required);
}

TEST_CASE("Game root wins over valve fallback for an approved WAD basename",
    "[world-textures][resolution][roots]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.create_game("mod");
    auto game = fixture::SyntheticWad3Entry{};
    game.name = "ROOTED";
    game.payload = fixture::synthetic_goldsrc_miptex("ROOTED", 16U, 16U, 3U);
    auto fallback = fixture::SyntheticWad3Entry{};
    fallback.name = "ROOTED";
    fallback.payload =
        fixture::synthetic_goldsrc_miptex("ROOTED", 16U, 16U, 9U);
    temporary.write("mod", "roots.wad", fixture::synthetic_wad3({game}).bytes);
    temporary.write("valve", "roots.wad", fixture::synthetic_wad3({fallback}).bytes);

    auto imported = import_world(make_bsp_bytes(
        SyntheticTextureStorage::external, "ROOTED", "roots.wad"));
    auto operation = begin_operation(imported, make_environment(temporary, "mod"));
    run_to_terminal(operation);

    REQUIRE(operation.result() != nullptr);
    CHECK(operation.result()->textures()[0].mip_levels[0].rgba_pixels[0] ==
        std::byte{3U});
    CHECK(operation.result()->archive_metadata()[0].source_root_ordinal == 0U);
}

TEST_CASE("Missing archives and textures publish typed incomplete sets",
    "[world-textures][resolution][incomplete]")
{
    ScopedLocalResourceTestRoot temporary;

    SECTION("missing WAD")
    {
        auto imported = import_world(make_bsp_bytes(
            SyntheticTextureStorage::external, "ABSENT", "absent.wad"));
        auto operation = begin_operation(imported, make_environment(temporary));
        run_to_terminal(operation);
        REQUIRE(operation.state() ==
            goldsrc::WorldTextureImportState::textures_incomplete);
        REQUIRE(operation.result() != nullptr);
        CHECK_FALSE(operation.result()->complete_for_world_materials());
        CHECK(operation.result()->bindings()[0].status ==
            assets::WorldMaterialTextureBindingStatus::
                external_wad_archive_missing);
        CHECK(operation.progress().wad_source_open_attempts == 0U);
    }

    SECTION("texture absent from valid WAD")
    {
        temporary.write("valve", "valid.wad",
            fixture::synthetic_valid_wad3("OTHER").bytes);
        auto imported = import_world(make_bsp_bytes(
            SyntheticTextureStorage::external, "ABSENT", "valid.wad"));
        auto operation = begin_operation(imported, make_environment(temporary));
        run_to_terminal(operation);
        REQUIRE(operation.result() != nullptr);
        CHECK(operation.result()->bindings()[0].status ==
            assets::WorldMaterialTextureBindingStatus::
                external_texture_not_found);
        CHECK(operation.progress().wad_source_open_attempts == 1U);
    }

    SECTION("missing BSP directory entry never guesses a WAD texture")
    {
        temporary.write("valve", "guessed.wad",
            fixture::synthetic_valid_wad3("TEST_QUAD").bytes);
        auto imported = import_world(make_bsp_bytes(
            SyntheticTextureStorage::missing, "TEST_QUAD", "guessed.wad"));
        auto operation = begin_operation(imported, make_environment(temporary));
        run_to_terminal(operation);
        REQUIRE(operation.result() != nullptr);
        CHECK(operation.result()->bindings()[0].status ==
            assets::WorldMaterialTextureBindingStatus::
                missing_bsp_texture_reference);
        CHECK(operation.progress().wad_source_open_attempts == 0U);
    }
}

TEST_CASE("Dimension mismatch is typed and does not search later archives",
    "[world-textures][resolution][dimensions]")
{
    ScopedLocalResourceTestRoot temporary;
    auto mismatched = fixture::SyntheticWad3Entry{};
    mismatched.name = "SIZED";
    mismatched.payload =
        fixture::synthetic_goldsrc_miptex("SIZED", 32U, 16U);
    temporary.write("valve", "mismatch.wad",
        fixture::synthetic_wad3({mismatched}).bytes);
    temporary.write("valve", "later.wad",
        fixture::synthetic_valid_wad3("SIZED").bytes);
    auto imported = import_world(make_bsp_bytes(
        SyntheticTextureStorage::external,
        "SIZED",
        "mismatch.wad;later.wad"));
    auto operation = begin_operation(imported, make_environment(temporary));
    run_to_terminal(operation);

    REQUIRE(operation.result() != nullptr);
    CHECK(operation.result()->bindings()[0].status ==
        assets::WorldMaterialTextureBindingStatus::
            external_texture_dimension_mismatch);
    CHECK(operation.result()->texture_count() == 0U);
    CHECK(operation.progress().wad_source_open_attempts == 1U);
}

TEST_CASE("Embedded materials never cause declared WAD opening",
    "[world-textures][resolution][no-wad-search]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "unused.wad", std::string_view{"not a WAD"});
    auto imported = import_world(make_bsp_bytes(
        SyntheticTextureStorage::embedded, "EMBEDDED", "unused.wad"));
    auto operation = begin_operation(imported, make_environment(temporary));
    run_to_terminal(operation);

    REQUIRE(operation.state() == goldsrc::WorldTextureImportState::textures_ready);
    REQUIRE(operation.result() != nullptr);
    CHECK(operation.progress().wad_source_open_attempts == 0U);
    REQUIRE(operation.result()->archive_metadata().size() == 1U);
    CHECK(operation.result()->archive_metadata()[0].status ==
        assets::WorldTextureArchiveStatus::not_required);
}

TEST_CASE("Malformed texture bytes fail without partial set publication",
    "[world-textures][resolution][fatal]")
{
    ScopedLocalResourceTestRoot temporary;
    auto imported = import_world(make_bsp_bytes(
        SyntheticTextureStorage::embedded, "BROKEN", {}, true));
    auto operation = begin_operation(imported, make_environment(temporary));
    run_to_terminal(operation);

    CHECK(operation.state() == goldsrc::WorldTextureImportState::failed);
    CHECK(operation.result() == nullptr);
    REQUIRE(operation.error() != nullptr);
    CHECK(operation.error()->code ==
        goldsrc::WorldTextureImportErrorCode::
            bsp_texture_source_parse_failed);
}

TEST_CASE("Texture operation enforces update budget cancellation and timeout",
    "[world-textures][resolution][incremental]")
{
    ScopedLocalResourceTestRoot temporary;
    auto imported = import_world(make_bsp_bytes(
        SyntheticTextureStorage::embedded, "INCREMENTAL"));

    SECTION("pixel conversion budget")
    {
        goldsrc::GoldSrcWorldTextureImportLimits limits;
        limits.maximum_pixel_conversion_bytes_per_update = 4U;
        auto operation = begin_operation(
            imported, make_environment(temporary), limits);
        auto now = goldsrc::WorldTextureImportTimePoint{};
        std::size_t previous = 0U;
        for (std::size_t update = 0U;
             update < 8'192U && !operation.terminal(); ++update) {
            operation.update(now);
            const auto current = operation.progress().pixel_conversion_bytes;
            CHECK(current - previous <= 4U);
            previous = current;
            now += 1ms;
        }
        REQUIRE(operation.state() ==
            goldsrc::WorldTextureImportState::textures_ready);
    }

    SECTION("cancellation is terminal and idempotent")
    {
        auto operation = begin_operation(imported, make_environment(temporary));
        operation.cancel();
        operation.cancel();
        operation.update(goldsrc::WorldTextureImportTimePoint{});
        CHECK(operation.state() == goldsrc::WorldTextureImportState::cancelled);
        CHECK(operation.result() == nullptr);
        REQUIRE(operation.error() != nullptr);
        CHECK(operation.error()->code ==
            goldsrc::WorldTextureImportErrorCode::cancelled);
    }

    SECTION("manual-clock timeout")
    {
        goldsrc::GoldSrcWorldTextureImportLimits limits;
        limits.timeout = 1ms;
        auto operation = begin_operation(
            imported, make_environment(temporary), limits);
        const auto start = goldsrc::WorldTextureImportTimePoint{};
        operation.update(start);
        operation.update(start + 1ms);
        CHECK(operation.state() == goldsrc::WorldTextureImportState::timed_out);
        CHECK(operation.result() == nullptr);
    }
}

} // namespace
