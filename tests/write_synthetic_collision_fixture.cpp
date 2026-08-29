#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

void populate_embedded_palette(std::vector<std::byte>& bsp_bytes)
{
    const auto texture_lump = static_cast<std::size_t>(
        hlclient::tests::synthetic_read_i32le(
            bsp_bytes,
            hlclient::tests::synthetic_lump_descriptor_offset(
                hlclient::tests::SyntheticBspLumpId::textures)));
    const auto record_relative = static_cast<std::size_t>(
        hlclient::tests::synthetic_read_i32le(
            bsp_bytes, texture_lump + 4U));
    const auto record = texture_lump + record_relative;
    constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
    const auto count_offset = record + 40U + pixel_byte_count;
    hlclient::tests::synthetic_write_u16le(
        bsp_bytes, count_offset, 256U);
    for (std::size_t index = 0U; index < 256U; ++index) {
        bsp_bytes[count_offset + 2U + (index * 3U)] =
            static_cast<std::byte>(index);
        bsp_bytes[count_offset + 2U + (index * 3U) + 1U] =
            static_cast<std::byte>(255U - index);
        bsp_bytes[count_offset + 2U + (index * 3U) + 2U] =
            static_cast<std::byte>(index ^ 0x5AU);
    }
}

[[nodiscard]] std::vector<std::byte> collision_fixture_bytes()
{
    hlclient::tests::SyntheticBspBuilder builder;

    std::array<hlclient::tests::SyntheticBspPlane, 4U> planes{};
    planes[0U].distance = 0.0F;
    planes[1U].distance = 36.0F;
    planes[2U].distance = 32.0F;
    planes[3U].distance = 18.0F;
    builder.set_planes(planes);

    constexpr std::array clipnodes{
        hlclient::tests::SyntheticBspClipnode{1, {-1, -2}},
        hlclient::tests::SyntheticBspClipnode{2, {-1, -2}},
        hlclient::tests::SyntheticBspClipnode{3, {-1, -2}},
    };
    builder.set_clipnodes(clipnodes);

    hlclient::tests::SyntheticBspNode world_node;
    // BSP node children index leaves (-1 is leaf zero, -2 is leaf one),
    // whereas clipnode children are literal contents codes. Keep the same
    // empty-above/solid-below half-space for the point and player hulls.
    world_node.children = {-2, -1};
    builder.set_nodes(std::span{&world_node, 1U});

    hlclient::tests::SyntheticBspModel world_model;
    world_model.headnodes = {0, 0, 1, 2};
    builder.set_models(std::span{&world_model, 1U});

    constexpr std::string_view entities =
        "{\n"
        "\"classname\" \"worldspawn\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"info_player_start\"\n"
        "\"origin\" \"32 32 64\"\n"
        "\"angle\" \"0\"\n"
        "}\n";
    auto& entity_lump =
        builder.lump(hlclient::tests::SyntheticBspLumpId::entities);
    entity_lump.clear();
    entity_lump.reserve(entities.size() + 1U);
    for (const auto character : entities) {
        entity_lump.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    entity_lump.push_back(std::byte{0U});

    return builder.build();
}

[[nodiscard]] std::vector<std::byte> movement_fixture_bytes()
{
    hlclient::tests::SyntheticBspBuilder builder;

    constexpr std::array planes{
        // Retain plane zero for the builder's harmless render quad.
        hlclient::tests::SyntheticBspPlane{{0.0F, 0.0F, 1.0F}, 0.0F, 2},
        hlclient::tests::SyntheticBspPlane{{0.0F, 0.0F, 1.0F}, 36.0F, 2},
        hlclient::tests::SyntheticBspPlane{{0.0F, 0.0F, 1.0F}, 32.0F, 2},
        hlclient::tests::SyntheticBspPlane{{0.0F, 0.0F, 1.0F}, 18.0F, 2},
        hlclient::tests::SyntheticBspPlane{{1.0F, 0.0F, 0.0F}, 192.0F, 0},
        hlclient::tests::SyntheticBspPlane{{0.0F, 1.0F, 0.0F}, 192.0F, 1},
        hlclient::tests::SyntheticBspPlane{{1.0F, 0.0F, 0.0F}, 176.0F, 0},
        hlclient::tests::SyntheticBspPlane{{0.0F, 1.0F, 0.0F}, 176.0F, 1},
        hlclient::tests::SyntheticBspPlane{{1.0F, 0.0F, 0.0F}, 160.0F, 0},
        hlclient::tests::SyntheticBspPlane{{0.0F, 1.0F, 0.0F}, 160.0F, 1},
        hlclient::tests::SyntheticBspPlane{{1.0F, 0.0F, 0.0F}, 176.0F, 0},
        hlclient::tests::SyntheticBspPlane{{0.0F, 1.0F, 0.0F}, 176.0F, 1},
    };
    builder.set_planes(planes);

    constexpr std::array render_wall{
        hlclient::tests::SyntheticBspVector3{192.0F, -2'048.0F, -512.0F},
        hlclient::tests::SyntheticBspVector3{192.0F, 2'048.0F, -512.0F},
        hlclient::tests::SyntheticBspVector3{192.0F, 2'048.0F, 512.0F},
        hlclient::tests::SyntheticBspVector3{192.0F, -2'048.0F, 512.0F},
    };
    builder.set_convex_polygon(render_wall);

    std::array<hlclient::tests::SyntheticBspNode, 3U> nodes{};
    nodes[0U].plane_index = 4;
    nodes[0U].children = {-1, 1};
    nodes[1U].plane_index = 5;
    nodes[1U].children = {-1, 2};
    nodes[1U].face_count = 0U;
    nodes[2U].plane_index = 0;
    nodes[2U].children = {-2, -1};
    nodes[2U].face_count = 0U;
    builder.set_nodes(nodes);

    // Each hull is empty above its floor and behind both positive-axis room
    // walls. The +X/+Y walls meet in a literal, deterministic inside corner.
    constexpr std::array clipnodes{
        hlclient::tests::SyntheticBspClipnode{6, {-2, 1}},
        hlclient::tests::SyntheticBspClipnode{7, {-2, 2}},
        hlclient::tests::SyntheticBspClipnode{1, {-1, -2}},
        hlclient::tests::SyntheticBspClipnode{8, {-2, 4}},
        hlclient::tests::SyntheticBspClipnode{9, {-2, 5}},
        hlclient::tests::SyntheticBspClipnode{2, {-1, -2}},
        hlclient::tests::SyntheticBspClipnode{10, {-2, 7}},
        hlclient::tests::SyntheticBspClipnode{11, {-2, 8}},
        hlclient::tests::SyntheticBspClipnode{3, {-1, -2}},
    };
    builder.set_clipnodes(clipnodes);

    std::array faces{
        hlclient::tests::SyntheticBspFace{},
        hlclient::tests::SyntheticBspFace{},
    };
    faces[0U].plane_index = 4;
    faces[1U].plane_index = 4;
    builder.set_faces(faces);

    std::array models{
        hlclient::tests::SyntheticBspModel{},
        hlclient::tests::SyntheticBspModel{},
    };
    models[0U].minimum = {-64.0F, -2'048.0F, -512.0F};
    models[0U].maximum = {192.0F, 2'048.0F, 512.0F};
    models[0U].headnodes = {0, 0, 3, 6};
    models[0U].first_face = 0;
    models[0U].face_count = 1;
    models[1U].minimum = {191.0F, -2'049.0F, -513.0F};
    models[1U].maximum = {193.0F, 2'049.0F, 513.0F};
    models[1U].headnodes = {0, 0, 3, 6};
    models[1U].visibility_leaf_count = 0;
    models[1U].first_face = 1;
    models[1U].face_count = 1;
    builder.set_models(models);

    auto embedded = hlclient::tests::synthetic_embedded_texture(
        "PREDICT", 16U, 16U);
    constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
    embedded.trailing_byte_count =
        pixel_byte_count + 2U + (256U * 3U);
    const std::array<std::optional<hlclient::tests::SyntheticBspMipTexture>, 1U>
        textures{embedded};
    builder.set_texture_directory(textures);

    constexpr std::string_view entities =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"func_wall\"\n"
        "\"model\" \"*1\"\n\"origin\" \"-1 0 0\"\n}\n"
        "{\n\"classname\" \"info_player_start\"\n"
        "\"origin\" \"0 0 36\"\n\"angles\" \"-20 0 0\"\n}\n";
    auto& entity_lump =
        builder.lump(hlclient::tests::SyntheticBspLumpId::entities);
    entity_lump.clear();
    entity_lump.reserve(entities.size() + 1U);
    for (const auto character : entities) {
        entity_lump.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    entity_lump.push_back(std::byte{0U});
    auto bytes = builder.build();
    populate_embedded_palette(bytes);
    return bytes;
}

[[nodiscard]] bool write_fixture(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

} // namespace

int main(const int argc, const char* const* const argv)
{
    if (argc != 2 || argv == nullptr || argv[1] == nullptr) {
        std::cerr << "usage: hlclient_collision_fixture_writer <root>\n";
        return 2;
    }

    const std::filesystem::path root{argv[1]};
    const auto maps = root / "valve" / "maps";
    std::error_code error;
    if (!std::filesystem::create_directories(maps, error) && error) {
        std::cerr << "unable to create the synthetic fixture root\n";
        return 1;
    }

    const auto collision_bytes = collision_fixture_bytes();
    const auto movement_bytes = movement_fixture_bytes();
    if (!write_fixture(maps / "test_collision.bsp", collision_bytes) ||
        !write_fixture(maps / "test_movement.bsp", movement_bytes)) {
        std::cerr << "unable to write the synthetic fixture\n";
        return 1;
    }
    return 0;
}
