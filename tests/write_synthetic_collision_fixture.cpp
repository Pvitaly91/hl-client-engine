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

    const auto bytes = collision_fixture_bytes();
    if (!write_fixture(maps / "test_collision.bsp", bytes) ||
        !write_fixture(maps / "test_movement.bsp", bytes)) {
        std::cerr << "unable to write the synthetic fixture\n";
        return 1;
    }
    return 0;
}
