#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/assets/asset_source.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;

[[nodiscard]] assets::AssetSource make_source(
    std::filesystem::path virtual_path,
    std::vector<std::byte> bytes)
{
    assets::AssetSourceMetadata metadata;
    metadata.content_size = bytes.size();
    auto created = assets::AssetSource::create(
        std::move(virtual_path), std::move(bytes), std::move(metadata));
    if (!created) {
        throw std::runtime_error{"Unable to create synthetic BSP AssetSource"};
    }
    return std::move(*created.source);
}

TEST_CASE("The real importer probes v30 structure rather than extension alone",
    "[goldsrc-bsp][importer][probe]")
{
    const bsp::GoldSrcBspWorldImporter importer;

    SECTION("valid v30 BSP path")
    {
        const auto source = make_source(
            "maps/test_map.bsp", fixture::literal_minimal_goldsrc_bsp_v30());
        CHECK(importer.probe(assets::make_asset_probe(source)) > assets::kAssetProbeNoMatch);
    }

    SECTION("valid v30 structure without bsp extension")
    {
        const auto source = make_source(
            "maps/test_map.bin", fixture::literal_minimal_goldsrc_bsp_v30());
        CHECK(importer.probe(assets::make_asset_probe(source)) > assets::kAssetProbeNoMatch);
    }

    SECTION("bsp extension with version 29")
    {
        auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
        fixture::synthetic_write_i32le(bytes, 0U, 29);
        const auto source = make_source("maps/test_map.bsp", std::move(bytes));
        CHECK(importer.probe(assets::make_asset_probe(source)) == assets::kAssetProbeNoMatch);
    }

    SECTION("random bsp file")
    {
        const auto source = make_source("maps/random.bsp",
            {std::byte{0x52}, std::byte{0x41}, std::byte{0x4E}, std::byte{0x44},
                std::byte{0x4F}, std::byte{0x4D}});
        CHECK(importer.probe(assets::make_asset_probe(source)) == assets::kAssetProbeNoMatch);
    }
}

TEST_CASE("Recognized truncated or malformed v30 data remains an importer candidate",
    "[goldsrc-bsp][importer][probe][malformed]")
{
    const bsp::GoldSrcBspWorldImporter importer;

    SECTION("version-only prefix")
    {
        std::vector<std::byte> bytes(4U);
        fixture::synthetic_write_i32le(bytes, 0U, 30);
        const auto source = make_source("maps/truncated.bsp", std::move(bytes));
        CHECK(importer.probe(assets::make_asset_probe(source)) > assets::kAssetProbeNoMatch);
        const auto imported = importer.import(source);
        REQUIRE_FALSE(imported);
        CHECK(imported.error().code == assets::AssetErrorCode::MalformedData);
    }

    SECTION("negative lump directory range")
    {
        auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                         .set_lump_offset(fixture::SyntheticBspLumpId::planes, -1)
                         .take();
        const auto source = make_source("maps/malformed.bsp", std::move(bytes));
        CHECK(importer.probe(assets::make_asset_probe(source)) > assets::kAssetProbeNoMatch);
        const auto imported = importer.import(source);
        REQUIRE_FALSE(imported);
        CHECK(imported.error().code == assets::AssetErrorCode::MalformedData);
        CHECK_FALSE(imported.error().context.empty());
        CHECK(imported.error().context.size() <=
            bsp::kGoldSrcBspMaximumDiagnosticContextBytes);
    }
}

TEST_CASE("Importer identity and probe are stable and side-effect free",
    "[goldsrc-bsp][importer][identity]")
{
    STATIC_REQUIRE(bsp::kGoldSrcBspWorldImporterId == std::string_view{"goldsrc-bsp-v30"});
    const bsp::GoldSrcBspWorldImporter importer;
    CHECK(importer.id() == bsp::kGoldSrcBspWorldImporterId);
    CHECK(bsp::kGoldSrcBspWorldImporterPriority == 300);

    const auto source = make_source(
        "maps/probe_only.bsp", fixture::literal_minimal_goldsrc_bsp_v30());
    const std::vector<std::byte> snapshot(source.bytes().begin(), source.bytes().end());
    const auto first = importer.probe(assets::make_asset_probe(source));
    const auto second = importer.probe(assets::make_asset_probe(source));
    CHECK(first == second);
    CHECK(first > assets::kAssetProbeNoMatch);
    CHECK(std::vector<std::byte>(source.bytes().begin(), source.bytes().end()) == snapshot);

    const auto imported = importer.import(source);
    REQUIRE(imported);
    CHECK(imported.value().surfaces.size() == 1U);
}

TEST_CASE("Import returns an owning WorldAsset with only approved virtual identity",
    "[goldsrc-bsp][importer][world]")
{
    assets::WorldAsset retained;
    {
        const bsp::GoldSrcBspWorldImporter importer;
        auto source = make_source(
            "maps/synthetic/test_map.bsp", fixture::literal_minimal_goldsrc_bsp_v30());
        auto imported = importer.import(source);
        INFO((imported ? std::string{} : imported.error().context));
        REQUIRE(imported);
        retained = std::move(imported).value();
    }

    CHECK(retained.identity.source_name == "maps/synthetic/test_map.bsp");
    CHECK(retained.identity.source_name.find(':') == std::string::npos);
    CHECK(retained.identity.source_name.find('\\') == std::string::npos);
    CHECK(retained.source_profile == assets::WorldGeometrySourceProfile::goldsrc_bsp_v30);
    CHECK(retained.vertices.size() == 4U);
    CHECK(retained.indices.size() == 6U);
    CHECK(retained.surfaces.size() == 1U);
    CHECK(retained.materials.size() == 1U);
}

TEST_CASE("Production registration installs exactly one real world importer",
    "[goldsrc-bsp][importer][registration]")
{
    assets::AssetImporterRegistries registries;
    const auto registered = bsp::register_builtin_asset_importers(registries);
    INFO((registered.error ? registered.error->context : std::string{}));
    REQUIRE(registered);
    CHECK(registries.worlds.size() == 1U);
    CHECK(registries.models.size() == 0U);
    CHECK(registries.sprites.size() == 0U);
    CHECK(registries.images.size() == 0U);
    CHECK(registries.audio.size() == 0U);

    const auto source = make_source(
        "maps/registered.bsp", fixture::literal_minimal_goldsrc_bsp_v30());
    const auto probe = registries.worlds.probe(source);
    REQUIRE(probe.selected());
    REQUIRE(probe.top_candidates.size() == 1U);
    CHECK(probe.top_candidates[0].importer_id == bsp::kGoldSrcBspWorldImporterId);
    CHECK(probe.top_candidates[0].priority == bsp::kGoldSrcBspWorldImporterPriority);

    const auto imported = registries.worlds.import(source);
    REQUIRE(imported);
    CHECK(imported.value().statistics.emitted_triangle_count == 2U);
}

} // namespace
