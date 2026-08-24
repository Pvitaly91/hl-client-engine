#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;

void require_typed_failure(const std::vector<std::byte>& bytes)
{
    const auto result = bsp::GoldSrcBspParser::parse(bytes);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.document.has_value());
    REQUIRE(result.error.has_value());
    CHECK(result.error->context.size() <= bsp::kGoldSrcBspMaximumDiagnosticContextBytes);
}

TEST_CASE("Deterministic mutations of every version byte fail without publication",
    "[goldsrc-bsp][mutation-corpus][version]")
{
    for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        INFO(byte_index);
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        corruptor.xor_byte(byte_index, 0x01U);
        const auto bytes = corruptor.take();
        require_typed_failure(bytes);
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::unsupported_version);
    }
}

TEST_CASE("Every lump descriptor truncation boundary fails boundedly",
    "[goldsrc-bsp][mutation-corpus][directory]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_bsp_v30();
    for (std::size_t index = 0U; index < fixture::kSyntheticBspLumpCount; ++index) {
        for (const auto boundary : {4U + (index * 8U), 11U + (index * 8U)}) {
            INFO(index);
            INFO(boundary);
            const std::vector<std::byte> truncated(baseline.begin(),
                baseline.begin() + static_cast<std::ptrdiff_t>(boundary));
            require_typed_failure(truncated);
        }
    }
}

TEST_CASE("Every required lump rejects corrupted offset and length fields",
    "[goldsrc-bsp][mutation-corpus][lumps]")
{
    constexpr std::array required{
        fixture::SyntheticBspLumpId::planes,
        fixture::SyntheticBspLumpId::textures,
        fixture::SyntheticBspLumpId::vertices,
        fixture::SyntheticBspLumpId::nodes,
        fixture::SyntheticBspLumpId::texinfo,
        fixture::SyntheticBspLumpId::faces,
        fixture::SyntheticBspLumpId::leaves,
        fixture::SyntheticBspLumpId::marksurfaces,
        fixture::SyntheticBspLumpId::edges,
        fixture::SyntheticBspLumpId::surfedges,
        fixture::SyntheticBspLumpId::models,
    };
    for (const auto lump : required) {
        INFO(static_cast<std::size_t>(lump));
        auto offset_corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        offset_corruptor.set_lump_offset(lump, -1);
        require_typed_failure(offset_corruptor.bytes());

        auto length_corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        length_corruptor.set_lump_length(lump, std::numeric_limits<std::int32_t>::max());
        require_typed_failure(length_corruptor.bytes());
    }
}

TEST_CASE("Reference and face-loop mutations always return typed errors",
    "[goldsrc-bsp][mutation-corpus][references]")
{
    struct Mutation {
        fixture::SyntheticBspLumpId lump;
        std::size_t relative_offset;
        std::int32_t value;
        bool is_i16;
    };
    constexpr std::array mutations{
        Mutation{fixture::SyntheticBspLumpId::faces, 0U, 1, true},
        Mutation{fixture::SyntheticBspLumpId::faces, 2U, 2, true},
        Mutation{fixture::SyntheticBspLumpId::faces, 4U, 4, false},
        Mutation{fixture::SyntheticBspLumpId::faces, 8U, 32'767, true},
        Mutation{fixture::SyntheticBspLumpId::faces, 10U, 1, true},
        Mutation{fixture::SyntheticBspLumpId::surfedges, 0U,
            std::numeric_limits<std::int32_t>::min(), false},
        Mutation{fixture::SyntheticBspLumpId::surfedges, 0U, 99, false},
        Mutation{fixture::SyntheticBspLumpId::edges, 4U, 99, true},
        Mutation{fixture::SyntheticBspLumpId::texinfo, 32U, 1, false},
        Mutation{fixture::SyntheticBspLumpId::nodes, 0U, 1, false},
        Mutation{fixture::SyntheticBspLumpId::leaves, 48U, 1, true},
        Mutation{fixture::SyntheticBspLumpId::marksurfaces, 0U, 1, true},
        Mutation{fixture::SyntheticBspLumpId::models, 36U, 99, false},
    };
    for (const auto& mutation : mutations) {
        INFO(static_cast<std::size_t>(mutation.lump));
        INFO(mutation.relative_offset);
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        if (mutation.is_i16) {
            corruptor.write_i16(mutation.lump, mutation.relative_offset,
                static_cast<std::int16_t>(mutation.value));
        } else {
            corruptor.write_i32(mutation.lump, mutation.relative_offset, mutation.value);
        }
        require_typed_failure(corruptor.bytes());
    }
}

TEST_CASE("NaN and infinity bit patterns never reach geometry output",
    "[goldsrc-bsp][mutation-corpus][floats]")
{
    constexpr std::array float_lumps{
        fixture::SyntheticBspLumpId::planes,
        fixture::SyntheticBspLumpId::vertices,
        fixture::SyntheticBspLumpId::texinfo,
        fixture::SyntheticBspLumpId::models,
    };
    constexpr std::array bit_patterns{
        0x7FC00000U,
        0x7F800000U,
        0xFF800000U,
    };
    for (const auto lump : float_lumps) {
        for (const auto pattern : bit_patterns) {
            INFO(static_cast<std::size_t>(lump));
            INFO(pattern);
            auto corruptor = fixture::SyntheticBspCorruptor{
                fixture::literal_minimal_goldsrc_bsp_v30()};
            corruptor.write_u32(lump, 0U, pattern);
            require_typed_failure(corruptor.bytes());
        }
    }
}

TEST_CASE("Texture directory and mip offsets form a deterministic malformed corpus",
    "[goldsrc-bsp][mutation-corpus][textures]")
{
    struct Mutation { std::size_t offset; std::int32_t value; };
    constexpr std::array mutations{
        Mutation{0U, -1},
        Mutation{0U, std::numeric_limits<std::int32_t>::max()},
        Mutation{4U, -2},
        Mutation{4U, 48},
        Mutation{32U, 1},
        Mutation{36U, 1},
        Mutation{40U, 1},
        Mutation{44U, 1},
    };
    for (const auto& mutation : mutations) {
        INFO(mutation.offset);
        INFO(mutation.value);
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        corruptor.write_i32(
            fixture::SyntheticBspLumpId::textures, mutation.offset, mutation.value);
        require_typed_failure(corruptor.bytes());
    }
}

} // namespace
