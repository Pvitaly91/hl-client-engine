#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace collision = hlclient::collision;
namespace fixture = hlclient::tests;
namespace goldsrc_collision = hlclient::goldsrc::collision;

void require_typed_failure(const std::vector<std::byte>& bytes)
{
    const auto result = bsp::GoldSrcBspParser::parse(bytes);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.document.has_value());
    REQUIRE(result.error.has_value());
    CHECK(result.error->context.size() <= bsp::kGoldSrcBspMaximumDiagnosticContextBytes);
}

void require_typed_failure(
    const std::vector<std::byte>& bytes,
    const bsp::GoldSrcBspErrorCode expected)
{
    const auto result = bsp::GoldSrcBspParser::parse(bytes);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.document.has_value());
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK(result.error->context.size() <=
        bsp::kGoldSrcBspMaximumDiagnosticContextBytes);
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
literal_collision_package()
{
    const auto parsed = bsp::GoldSrcBspParser::parse(
        fixture::literal_collision_goldsrc_bsp_v30());
    REQUIRE(parsed);
    REQUIRE(parsed.document);
    const auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        *parsed.document);
    REQUIRE(built);
    REQUIRE(built.package);
    return built.package;
}

template <typename Result>
void require_query_failure(
    const Result& result,
    const collision::CollisionQueryErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
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
        fixture::SyntheticBspLumpId::clipnodes,
        fixture::SyntheticBspLumpId::marksurfaces,
        fixture::SyntheticBspLumpId::edges,
        fixture::SyntheticBspLumpId::surfedges,
        fixture::SyntheticBspLumpId::models,
    };
    for (const auto lump : required) {
        INFO(static_cast<std::size_t>(lump));
        auto offset_corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_collision_goldsrc_bsp_v30()};
        offset_corruptor.set_lump_offset(lump, -1);
        require_typed_failure(offset_corruptor.bytes());

        auto length_corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_collision_goldsrc_bsp_v30()};
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
                fixture::literal_collision_goldsrc_bsp_v30()};
            corruptor.write_u32(lump, 0U, pattern);
            require_typed_failure(corruptor.bytes());
        }
    }
}

TEST_CASE("Collision tree and model-root mutations fail transactionally",
    "[goldsrc-bsp][mutation-corpus][collision][references]")
{
    enum class Width : std::uint8_t { i16, i32 };
    struct Mutation {
        fixture::SyntheticBspLumpId lump;
        std::size_t relative_offset;
        std::int32_t value;
        Width width;
        bsp::GoldSrcBspErrorCode expected;
    };
    constexpr std::array mutations{
        Mutation{fixture::SyntheticBspLumpId::clipnodes,
            0U,
            1,
            Width::i32,
            bsp::GoldSrcBspErrorCode::invalid_clipnode_reference},
        Mutation{fixture::SyntheticBspLumpId::clipnodes,
            4U,
            1,
            Width::i16,
            bsp::GoldSrcBspErrorCode::invalid_clipnode_reference},
        Mutation{fixture::SyntheticBspLumpId::nodes,
            4U,
            -3,
            Width::i16,
            bsp::GoldSrcBspErrorCode::invalid_node_reference},
        Mutation{fixture::SyntheticBspLumpId::leaves,
            0U,
            0,
            Width::i32,
            bsp::GoldSrcBspErrorCode::invalid_leaf_reference},
        Mutation{fixture::SyntheticBspLumpId::models,
            36U,
            -1,
            Width::i32,
            bsp::GoldSrcBspErrorCode::invalid_model_reference},
        Mutation{fixture::SyntheticBspLumpId::models,
            40U,
            1,
            Width::i32,
            bsp::GoldSrcBspErrorCode::invalid_model_reference},
        Mutation{fixture::SyntheticBspLumpId::models,
            44U,
            1,
            Width::i32,
            bsp::GoldSrcBspErrorCode::invalid_model_reference},
        Mutation{fixture::SyntheticBspLumpId::models,
            48U,
            1,
            Width::i32,
            bsp::GoldSrcBspErrorCode::invalid_model_reference},
    };

    for (const auto& mutation : mutations) {
        INFO(static_cast<std::size_t>(mutation.lump));
        INFO(mutation.relative_offset);
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_collision_goldsrc_bsp_v30()};
        if (mutation.width == Width::i16) {
            corruptor.write_i16(mutation.lump,
                mutation.relative_offset,
                static_cast<std::int16_t>(mutation.value));
        } else {
            corruptor.write_i32(
                mutation.lump, mutation.relative_offset, mutation.value);
        }
        require_typed_failure(corruptor.bytes(), mutation.expected);
    }
}

TEST_CASE("Collision mutation corpus rejects node and clipnode cycles",
    "[goldsrc-bsp][mutation-corpus][collision][cycle]")
{
    SECTION("hull-zero node cycle")
    {
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_collision_goldsrc_bsp_v30()};
        corruptor.write_i16(fixture::SyntheticBspLumpId::nodes, 4U, 0);
        require_typed_failure(
            corruptor.bytes(), bsp::GoldSrcBspErrorCode::node_cycle);
    }

    SECTION("clip-hull cycle")
    {
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_collision_goldsrc_bsp_v30()};
        corruptor.write_i16(fixture::SyntheticBspLumpId::clipnodes, 4U, 0);
        require_typed_failure(
            corruptor.bytes(), bsp::GoldSrcBspErrorCode::clipnode_cycle);
    }
}

TEST_CASE("Every truncation of the literal collision BSP fails without publication",
    "[goldsrc-bsp][mutation-corpus][collision][truncation]")
{
    const auto baseline = fixture::literal_collision_goldsrc_bsp_v30();
    for (std::size_t retained = 0U; retained < baseline.size(); ++retained) {
        INFO(retained);
        const std::vector<std::byte> truncated(
            baseline.begin(),
            baseline.begin() + static_cast<std::ptrdiff_t>(retained));
        require_typed_failure(truncated);
    }
}

TEST_CASE("Collision model-count mutation respects the exact parser limit",
    "[goldsrc-bsp][mutation-corpus][collision][model-count]")
{
    auto bytes = fixture::literal_collision_goldsrc_bsp_v30();
    constexpr std::size_t model_wire_size = 64U;
    const auto model_offset = static_cast<std::size_t>(
        fixture::synthetic_read_i32le(bytes,
            fixture::synthetic_lump_descriptor_offset(
                fixture::SyntheticBspLumpId::models)));
    std::array<std::byte, model_wire_size> duplicate{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(model_offset),
        model_wire_size,
        duplicate.begin());
    bytes.insert(bytes.end(), duplicate.begin(), duplicate.end());
    fixture::synthetic_write_i32le(bytes,
        fixture::synthetic_lump_descriptor_offset(
            fixture::SyntheticBspLumpId::models) + 4U,
        static_cast<std::int32_t>(2U * model_wire_size));

    auto limits = bsp::GoldSrcBspImportLimits{};
    limits.maximum_models = 1U;
    const auto result = bsp::GoldSrcBspParser::parse(bytes, limits);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.document);
    REQUIRE(result.error);
    CHECK(result.error->code == bsp::GoldSrcBspErrorCode::count_limit_exceeded);
    CHECK(result.error->lump_id == bsp::GoldSrcBspLumpId::models);
}

TEST_CASE("Collision query mutations reject nonfinite points and trace endpoints",
    "[collision][mutation-corpus][query][finite]")
{
    collision::CollisionWorldQuery query{literal_collision_package()};
    collision::CollisionQueryScratch scratch;
    constexpr std::array nonfinite{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };

    for (const auto value : nonfinite) {
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            INFO(value);
            INFO(axis);
            collision::CollisionPointContentsRequest request;
            if (axis == 0U) {
                request.point.x = value;
            } else if (axis == 1U) {
                request.point.y = value;
            } else {
                request.point.z = value;
            }
            require_query_failure(query.point_contents(request, scratch),
                collision::CollisionQueryErrorCode::invalid_point);

            for (const bool mutate_start : {false, true}) {
                collision::CollisionTraceRequest trace;
                trace.start = {0.0F, 0.0F, 1.0F};
                trace.end = {0.0F, 0.0F, -1.0F};
                auto& endpoint = mutate_start ? trace.start : trace.end;
                if (axis == 0U) {
                    endpoint.x = value;
                } else if (axis == 1U) {
                    endpoint.y = value;
                } else {
                    endpoint.z = value;
                }
                require_query_failure(query.trace_line(trace, scratch),
                    collision::CollisionQueryErrorCode::invalid_segment);
            }
        }
    }
}

TEST_CASE("Every collision query-limit field fails below and above its hard bound",
    "[collision][mutation-corpus][query][limits]")
{
    collision::CollisionWorldQuery query{literal_collision_package()};
    collision::CollisionQueryScratch scratch;
    std::array<collision::CollisionQueryLimits, 8U> mutations{};
    mutations[0U].maximum_traversal_steps = 0U;
    mutations[1U].maximum_traversal_steps =
        collision::kCollisionHardMaximumTraversalSteps + 1U;
    mutations[2U].maximum_stack_entries = 0U;
    mutations[3U].maximum_stack_entries =
        collision::kCollisionHardMaximumStackEntries + 1U;
    mutations[4U].maximum_fraction_splits = 0U;
    mutations[5U].maximum_fraction_splits =
        collision::kCollisionHardMaximumFractionSplits + 1U;
    mutations[6U].maximum_query_scratch_bytes = 0U;
    mutations[7U].maximum_query_scratch_bytes =
        collision::kCollisionHardMaximumQueryScratchBytes + 1U;

    for (std::size_t index = 0U; index < mutations.size(); ++index) {
        INFO(index);
        if ((index % 2U) == 0U) {
            collision::CollisionPointContentsRequest request;
            request.limits = mutations[index];
            require_query_failure(query.point_contents(request, scratch),
                collision::CollisionQueryErrorCode::invalid_configuration);
        } else {
            collision::CollisionTraceRequest request;
            request.start = {0.0F, 0.0F, 1.0F};
            request.end = {0.0F, 0.0F, -1.0F};
            request.limits = mutations[index];
            require_query_failure(query.trace_line(request, scratch),
                collision::CollisionQueryErrorCode::invalid_configuration);
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
