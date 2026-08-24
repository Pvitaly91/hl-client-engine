#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;

[[nodiscard]] bsp::GoldSrcBspParseResult parse(const fixture::SyntheticBspBuilder& builder)
{
    return bsp::GoldSrcBspParser::parse(builder.build());
}

TEST_CASE("Texture directory reports external, embedded, and missing metadata states",
    "[goldsrc-bsp][textures][metadata]")
{
    SECTION("external reference")
    {
        const auto result = bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30());
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
        REQUIRE(result.document->world_asset.materials.size() == 1U);
        const auto& material = result.document->world_asset.materials[0];
        CHECK(material.texture_name == "TEST_QUAD");
        CHECK(material.width == 64U);
        CHECK(material.height == 64U);
        CHECK(material.texture_storage == assets::WorldTextureStorage::external_reference);
        CHECK(result.document->world_asset.statistics.external_texture_reference_count == 1U);
    }

    SECTION("embedded metadata without pixel decoding")
    {
        fixture::SyntheticBspBuilder builder;
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{
            fixture::synthetic_embedded_texture()};
        builder.set_texture_directory(textures);
        const auto result = parse(builder);
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
        const auto& material = result.document->world_asset.materials[0];
        CHECK(material.texture_name == "EMBEDDED");
        CHECK(material.width == 16U);
        CHECK(material.height == 16U);
        CHECK(material.texture_storage == assets::WorldTextureStorage::embedded);
        CHECK(result.document->world_asset.statistics.embedded_texture_reference_count == 1U);
        CHECK(result.document->world_asset.vertices.size() == 4U);
    }

    SECTION("directory offset minus one is a missing reference")
    {
        fixture::SyntheticBspBuilder builder;
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{
            std::nullopt};
        builder.set_texture_directory(textures);
        const auto result = parse(builder);
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
        const auto& material = result.document->world_asset.materials[0];
        CHECK_FALSE(material.texture_name.has_value());
        CHECK_FALSE(material.width.has_value());
        CHECK_FALSE(material.height.has_value());
        CHECK(material.texture_storage == assets::WorldTextureStorage::missing);
        CHECK(result.document->world_asset.statistics.missing_texture_reference_count == 1U);
    }
}

TEST_CASE("Texture names are bounded to the exact 16-byte source field",
    "[goldsrc-bsp][textures][name]")
{
    SECTION("first NUL terminates the name")
    {
        fixture::SyntheticBspBuilder builder;
        auto texture = fixture::synthetic_external_texture("SHORT");
        texture.name[6] = 'X';
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{texture};
        builder.set_texture_directory(textures);
        const auto result = parse(builder);
        REQUIRE(result);
        CHECK(result.document->world_asset.materials[0].texture_name == "SHORT");
    }

    SECTION("a full field needs no NUL terminator")
    {
        fixture::SyntheticBspBuilder builder;
        auto texture = fixture::synthetic_external_texture("ABCDEFGHIJKLMNOP");
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{texture};
        builder.set_texture_directory(textures);
        const auto result = parse(builder);
        REQUIRE(result);
        CHECK(result.document->world_asset.materials[0].texture_name ==
            "ABCDEFGHIJKLMNOP");
    }
}

TEST_CASE("Texture directory grammar fails closed", "[goldsrc-bsp][textures][mutation]")
{
    SECTION("zero textures leaves the face texinfo reference invalid")
    {
        fixture::SyntheticBspBuilder builder;
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 0U> textures{};
        builder.set_texture_directory(textures);
        const auto result = parse(builder);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texinfo_reference);
    }

    SECTION("negative offset other than minus one")
    {
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        const auto bytes = std::move(corruptor)
                               .write_i32(fixture::SyntheticBspLumpId::textures, 4U, -2)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_directory);
    }

    SECTION("offset table truncated")
    {
        fixture::SyntheticBspBuilder builder;
        auto& lump = builder.lump(fixture::SyntheticBspLumpId::textures);
        lump.resize(8U);
        fixture::synthetic_write_i32le(lump, 0U, 2);
        const auto result = parse(builder);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_directory);
    }

    SECTION("miptex header truncated")
    {
        fixture::SyntheticBspBuilder builder;
        auto& lump = builder.lump(fixture::SyntheticBspLumpId::textures);
        lump.resize(47U);
        const auto result = parse(builder);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_metadata);
    }

    SECTION("miptex offset outside the lump")
    {
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        const auto bytes = std::move(corruptor)
                               .write_i32(fixture::SyntheticBspLumpId::textures, 4U, 48)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_metadata);
    }
}

TEST_CASE("Texture dimensions and mip offset patterns are validated",
    "[goldsrc-bsp][textures][mips]")
{
    const auto check_invalid = [](const std::size_t offset, const std::uint32_t value) {
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        const auto bytes = std::move(corruptor)
                               .write_u32(fixture::SyntheticBspLumpId::textures, offset, value)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_metadata);
    };

    SECTION("zero width") { check_invalid(24U, 0U); }
    SECTION("zero height") { check_invalid(28U, 0U); }
    SECTION("width outside stock 16-texel granularity")
    {
        check_invalid(24U, 63U);
    }
    SECTION("height outside stock 16-texel granularity")
    {
        check_invalid(28U, 65U);
    }
    SECTION("dimension multiplication or profile overflow")
    {
        check_invalid(24U, std::numeric_limits<std::uint32_t>::max());
    }

    SECTION("all zero offsets are an external reference")
    {
        REQUIRE(bsp::GoldSrcBspParser::parse(fixture::literal_minimal_goldsrc_bsp_v30()));
    }

    SECTION("all four bounded offsets are embedded metadata")
    {
        fixture::SyntheticBspBuilder builder;
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{
            fixture::synthetic_embedded_texture()};
        builder.set_texture_directory(textures);
        REQUIRE(parse(builder));
    }

    SECTION("mixed zero and non-zero offsets")
    {
        fixture::SyntheticBspBuilder builder;
        auto texture = fixture::synthetic_embedded_texture();
        texture.mip_offsets[2] = 0U;
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{texture};
        builder.set_texture_directory(textures);
        const auto result = parse(builder);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_metadata);
    }

    SECTION("mip offset outside the texture lump")
    {
        fixture::SyntheticBspBuilder builder;
        auto texture = fixture::synthetic_embedded_texture();
        texture.mip_offsets[3] = 999'999U;
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{texture};
        builder.set_texture_directory(textures);
        const auto result = parse(builder);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_metadata);
    }

    SECTION("mip start inside the lump still needs its complete level extent")
    {
        fixture::SyntheticBspBuilder builder;
        auto texture = fixture::synthetic_embedded_texture();
        // The miptex header begins at texture-lump offset 8. Offset 379 points
        // to the final retained byte, but the 2x2 fourth mip needs four bytes.
        texture.mip_offsets[3] = 379U;
        const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{texture};
        builder.set_texture_directory(textures);
        const auto result = parse(builder);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_metadata);
    }
}

TEST_CASE("Duplicate miptex offsets retain distinct directory ordinals",
    "[goldsrc-bsp][textures][duplicates]")
{
    fixture::SyntheticBspBuilder builder;
    const std::array<std::optional<fixture::SyntheticBspMipTexture>, 2U> textures{
        fixture::synthetic_external_texture("SHARED"),
        fixture::synthetic_external_texture("IGNORED")};
    auto texinfo = fixture::SyntheticBspTexinfo{};
    texinfo.miptex_index = 1;
    auto bytes = builder.set_texture_directory(textures)
                     .set_texinfo(std::span{&texinfo, 1U})
                     .build();
    auto corruptor = fixture::SyntheticBspCorruptor{std::move(bytes)};
    const auto texture_lump = corruptor.lump_offset(fixture::SyntheticBspLumpId::textures);
    const auto first_offset = fixture::synthetic_read_i32le(corruptor.bytes(), texture_lump + 4U);
    const auto duplicated = std::move(corruptor)
                                .write_i32(fixture::SyntheticBspLumpId::textures, 8U, first_offset)
                                .take();
    const auto result = bsp::GoldSrcBspParser::parse(duplicated);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    CHECK(result.document->lump_element_counts[bsp::goldsrc_bsp_lump_index(
              bsp::GoldSrcBspLumpId::textures)] == 2U);
    CHECK(result.document->world_asset.materials[0].texture_name == "SHARED");
}

} // namespace
