#include <hlclient/goldsrc/visual_assets/goldsrc_studio_companion_names.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_bundle_import.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace local = hlclient::local_resources;
namespace visual = hlclient::goldsrc::visual_assets;
namespace assets = hlclient::assets;

static_assert(std::is_same_v<
    decltype(std::declval<visual::GoldSrcStudioModelBundleImportResult&>()
                 .model()),
    const assets::ModelAsset&>);
static_assert(std::is_same_v<
    decltype(std::declval<visual::GoldSrcStudioModelBundleImportResult&>()
                 .sources()),
    const visual::GoldSrcStudioModelSourceBundle&>);
static_assert(std::is_same_v<
    decltype(std::declval<visual::GoldSrcVisualAssetImportResult&>().asset()),
    const assets::ImportedAsset&>);
static_assert(std::is_const_v<
    typename decltype(assets::ModelAsset{}.skeletal_data)::element_type>);

[[nodiscard]] local::LocalVirtualResourceName classified(
    const std::string_view value)
{
    auto result = local::LocalVirtualResourceName::create(value);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    REQUIRE(result.name);
    return std::move(*result.name);
}

TEST_CASE(
    "Studio companion names use only the approved same-directory main stem",
    "[goldsrc][studio][dependencies][names]")
{
    const auto main = classified("models/characters/foo.mdl");

    const auto texture =
        visual::derive_goldsrc_studio_texture_companion_name(main);
    INFO((texture.error ? texture.error->context : std::string{}));
    REQUIRE(texture);
    REQUIRE(texture.name);
    CHECK(texture.name->value() == "models/characters/fooT.mdl");
    CHECK(texture.name->component_count() == main.component_count());

    for (std::uint8_t ordinal = 1U; ordinal <= 15U; ++ordinal) {
        const auto group =
            visual::derive_goldsrc_studio_sequence_group_companion_name(
                main, ordinal);
        INFO((group.error ? group.error->context : std::string{}));
        REQUIRE(group);
        REQUIRE(group.name);
        const std::string expected =
            ordinal < 10U
                ? "models/characters/foo0" + std::to_string(ordinal) +
                      ".mdl"
                : "models/characters/foo" + std::to_string(ordinal) +
                      ".mdl";
        CHECK(group.name->value() == expected);
        CHECK(group.name->component_count() == main.component_count());
    }
}

TEST_CASE(
    "Studio companion derivation preserves stem spelling and treats mdl as an extension profile",
    "[goldsrc][studio][dependencies][names]")
{
    const auto main = classified("MODELS/Barney.MDL");
    const auto texture =
        visual::derive_goldsrc_studio_texture_companion_name(main);
    REQUIRE(texture);
    REQUIRE(texture.name);
    CHECK(texture.name->value() == "MODELS/BarneyT.mdl");

    const auto group =
        visual::derive_goldsrc_studio_sequence_group_companion_name(main, 7U);
    REQUIRE(group);
    REQUIRE(group.name);
    CHECK(group.name->value() == "MODELS/Barney07.mdl");
}

TEST_CASE(
    "Studio companion derivation rejects unsupported names and group ordinals",
    "[goldsrc][studio][dependencies][names][negative]")
{
    const auto wrong_extension = classified("models/foo.spr");
    const auto texture =
        visual::derive_goldsrc_studio_texture_companion_name(wrong_extension);
    REQUIRE_FALSE(texture);
    REQUIRE(texture.error);
    CHECK(texture.error->code ==
          visual::GoldSrcStudioCompanionNameErrorCode::
              unsupported_main_extension);

    const auto main = classified("models/foo.mdl");
    for (const std::uint8_t ordinal :
         std::array<std::uint8_t, 3U>{0U, 16U, 255U}) {
        const auto group =
            visual::derive_goldsrc_studio_sequence_group_companion_name(
                main, ordinal);
        REQUIRE_FALSE(group);
        REQUIRE(group.error);
        CHECK(group.error->code ==
              visual::GoldSrcStudioCompanionNameErrorCode::
                  sequence_group_out_of_range);
    }
}

TEST_CASE(
    "Studio companion result diagnostics never echo virtual names",
    "[goldsrc][studio][dependencies][names][privacy]")
{
    constexpr std::string_view sensitive = "models/private_model_name.spr";
    const auto wrong_extension = classified(sensitive);
    const auto result =
        visual::derive_goldsrc_studio_texture_companion_name(wrong_extension);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->context.find(sensitive) == std::string::npos);
    CHECK(result.error->context.find("private_model_name") ==
          std::string::npos);
}

} // namespace
