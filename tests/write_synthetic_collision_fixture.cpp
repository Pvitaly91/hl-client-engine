#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

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

    hlclient::tests::SyntheticBspModel world_model;
    world_model.minimum = {-64.0F, -64.0F, 0.0F};
    world_model.maximum = {192.0F, 192.0F, 128.0F};
    world_model.headnodes = {0, 0, 3, 6};
    builder.set_models(std::span{&world_model, 1U});

    constexpr std::string_view entities =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"info_player_start\"\n"
        "\"origin\" \"0 0 36\"\n\"angle\" \"0\"\n}\n";
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
