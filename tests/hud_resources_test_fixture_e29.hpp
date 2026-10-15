#pragma once
#include <hlclient/goldsrc/hud_assets.hpp>
#include <hlclient/goldsrc/hud_manifest.hpp>
#include <hlclient/hash/sha256.hpp>
#include "goldsrc_sprite_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <set>
#include <string>
#include <thread>

namespace hlclient::tests::hud_resources_e29 {
namespace g = hlclient::goldsrc;
namespace api = hlclient::game_api;
namespace fs = hlclient::filesystem;
namespace fixture = hlclient::tests::sprite_fixture;
inline std::string environment_value(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0U;
    if (::_dupenv_s(&value, &length, name) != 0) {
        std::free(value);
        return {};
    }
    const std::unique_ptr<char, decltype(&std::free)> owned{value, &std::free};
    return value ? std::string{value} : std::string{};
#else
    const auto* value = std::getenv(name);
    return value ? std::string{value} : std::string{};
#endif
}
inline std::span<const std::byte> text_bytes(std::string_view value) {
    return std::as_bytes(std::span{value.data(), value.size()});
}
class FixtureSource final : public fs::IFileSystem {
public:
    std::map<std::string, std::vector<std::byte>> files;
    mutable std::map<std::string, std::size_t> reads;
    void add(std::string name, std::string_view text) {
        const auto bytes = text_bytes(text); files.emplace(std::move(name), std::vector<std::byte>{bytes.begin(), bytes.end()});
    }
    fs::FileReadResult read_file(const std::filesystem::path& path) const override {
        const auto name = path.generic_string(); ++reads[name];
        const auto found = files.find(name);
        if (found == files.end()) return {std::nullopt, fs::FileReadError{fs::FileReadErrorCode::NotFound, path, "Fixture missing"}};
        return {fs::FileContents{path, found->second, std::nullopt}, std::nullopt};
    }
};
inline std::shared_ptr<const g::HudPreparedAssets> wait_prepared(g::ApprovedHudAssets& assets) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto value = assets.snapshot()) return value;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return {};
}
inline g::PreparedSoundResources resources_for(std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment,
    std::string_view map) {
    namespace readiness = hlclient::tests::readiness_fixture;
    const auto list = readiness::parse_resource_list({{2U, std::string{map}, 1U, 0U, 0U}});
    const auto inventory = readiness::build_inventory(list, *environment);
    const auto server = readiness::parse_server_info(map);
    auto manifest = readiness::build_manifest(list, inventory, server, *environment);
    REQUIRE(manifest);
    REQUIRE(manifest.state->world_geometry_ready());
    return {std::make_shared<const g::PrecacheManifestState>(std::move(*manifest.state)), std::move(environment)};
}
}
