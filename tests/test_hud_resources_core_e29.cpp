#include "hud_resources_test_fixture_e29.hpp"

using namespace hlclient::tests::hud_resources_e29;

TEST_CASE("E29 HUD manifest parses actual grammar without executing inert roles", "[e29][hud-resources][parser]") {
    const auto parsed = g::parse_hud_manifest(text_bytes(
        "3\r\n// count and comments\r\nunknown_role 2560 2560/sheet 0 0 2 2\n"
        "crosshair\t640 sheet 1 1 1 1 // crop\n crosshair 1280 sheet 0 0 2 2\n"));
    REQUIRE(parsed);
    REQUIRE(parsed.records.size() == 3U);
    CHECK(parsed.declared_count == 3U);
    CHECK(parsed.records[0].role == "unknown_role");
    CHECK(parsed.records[0].sprite_basename == "2560/sheet");
    CHECK(parsed.records[1].source == api::HudSourceRectangle{1U, 1U, 1U, 1U});
    CHECK(parsed.records[2].resolution == 1280U);
    CHECK_FALSE(parsed.exact_count_compatibility_applied);
    CHECK(parsed.source_sha256.size() == 64U);
    const auto no_count = g::parse_hud_manifest(text_bytes("crosshair 640 sheet 0 0 1 1\n"));
    REQUIRE(no_count);
    CHECK_FALSE(no_count.declared_count);
}

TEST_CASE("E29 HUD count mismatch needs complete exact audited compatibility", "[e29][hud-resources][parser]") {
    const std::string original = "2\ncrosshair 640 sheet 0 0 1 1\n";
    CHECK(g::parse_hud_manifest(text_bytes(original)).error == g::HudManifestError::count_mismatch);
    const auto digest = hlclient::hash::sha256(text_bytes(original)); REQUIRE(digest);
    api::HudManifestCountCompatibility compatibility{hlclient::hash::sha256_hex(*digest), 2U, 1U};
    const auto ready = g::parse_hud_manifest(text_bytes(original), compatibility);
    REQUIRE(ready); CHECK(ready.exact_count_compatibility_applied);
    auto mismatch = compatibility; mismatch.parsed_count = 2U;
    CHECK_FALSE(g::parse_hud_manifest(text_bytes(original), mismatch));
    mismatch = compatibility; mismatch.declared_count = 3U;
    CHECK_FALSE(g::parse_hud_manifest(text_bytes(original), mismatch));
    mismatch = compatibility; mismatch.exact_source_sha256[0] = mismatch.exact_source_sha256[0] == '0' ? '1' : '0';
    CHECK_FALSE(g::parse_hud_manifest(text_bytes(original), mismatch));
    CHECK_FALSE(g::parse_hud_manifest(text_bytes(original + "// changed\n"), compatibility));
}

TEST_CASE("E29 HUD manifest rejects malformed counts coordinates names and duplicates", "[e29][hud-resources][parser]") {
    const std::array<std::string_view, 15U> invalid{
        "x\n", "999999999999999\n", "513\n", "1\ncrosshair 640 ../sheet 0 0 1 1\n",
        "1\ncrosshair 640 C:/sheet 0 0 1 1\n", "1\ncrosshair 640 /sheet 0 0 1 1\n",
        "1\ncrosshair 640 sheet.spr 0 0 1 1\n", "1\ncrosshair 640 sheet -1 0 1 1\n",
        "1\ncrosshair 640 sheet 16384 0 1 1\n", "1\ncrosshair 640 sheet 0 0 0 1\n",
        "1\ncrosshair 0 sheet 0 0 1 1\n", "1\ncrosshair 640 sheet 0 0 1 1 extra\n",
        "2\ncrosshair 640 sheet 0 0 1 1\ncrosshair 640 sheet 1 1 1 1\n",
        "1\ncrosshair 640 sheet 0 0 4294967295 1\n", "1\ncrosshair 640 sheet 0 0 1 1\nmalformed"
    };
    for (const auto value : invalid) { CAPTURE(value); const auto parsed = g::parse_hud_manifest(text_bytes(value)); CHECK_FALSE(parsed); CHECK(parsed.records.empty()); }
    std::string non_ascii = "1\nx 640 sheet 0 0 1 1\n"; non_ascii[2] = static_cast<char>(0xff);
    CHECK(g::parse_hud_manifest(text_bytes(non_ascii)).error == g::HudManifestError::invalid_text);
    std::string long_token(64U, 'a');
    CHECK_FALSE(g::parse_hud_manifest(text_bytes("1\n" + long_token + " 640 sheet 0 0 1 1\n")));
    const std::vector<std::byte> oversized(g::kMaximumHudManifestBytes + 1U, std::byte{' '});
    CHECK(g::parse_hud_manifest(oversized).error == g::HudManifestError::size_limit);
}

TEST_CASE("E29 HUD parser bounds entries and unique sprite dependencies", "[e29][hud-resources][parser]") {
    std::string maximum = "512\n";
    for (std::size_t i = 0U; i < 512U; ++i) maximum += "role" + std::to_string(i) + " 640 sheet 0 0 1 1\n";
    REQUIRE(g::parse_hud_manifest(text_bytes(maximum)));
    maximum.replace(0U, 3U, "513");
    CHECK_FALSE(g::parse_hud_manifest(text_bytes(maximum)));
    std::string dependencies = "257\n";
    for (std::size_t i = 0U; i < 257U; ++i)
        dependencies += "role" + std::to_string(i) + " 640 sheet" + std::to_string(i) + " 0 0 1 1\n";
    CHECK(g::parse_hud_manifest(text_bytes(dependencies)).error == g::HudManifestError::dependency_limit);
    CHECK(g::valid_hud_descriptor_name("sprites/weapon_9mmAR.txt"));
    for (const auto path : {"sprites/../hud.txt", "C:/hud.txt", "sprites/a/b.txt", "sprites/hud.txt;exec", "sprites/.txt"})
        CHECK_FALSE(g::valid_hud_descriptor_name(path));
}

TEST_CASE("E29 HUD AssetManager composition preserves crop palette blend and shared immutable revisions", "[e29][hud-resources][assets]") {
    FixtureSource source;
    source.add("sprites/hud.txt", "2\nnumber_0 640 sheet 0 0 1 1\nnumber_0 1280 sheet 1 1 1 1\n");
    source.add("sprites/weapon_fixture.txt", "2\ncrosshair 640 mask 0 0 2 2\nweapon_s 640 sheet 0 0 2 2\n");
    source.files["sprites/sheet.spr"] = fixture::literal_single_sprite(1);
    source.files["sprites/mask.spr"] = fixture::literal_single_sprite(3);
    const std::array<api::HudManifestRequest, 2U> requests{{{"sprites/hud.txt", {}}, {"sprites/weapon_fixture.txt", {}}}};
    const auto prepared = g::prepare_hud_assets(source, requests);
    REQUIRE(prepared.resources.descriptors.size() == 2U);
    REQUIRE(prepared.sprites.size() == 2U);
    CHECK(prepared.ready_entries == 4U);
    CHECK(prepared.rejected_entries == 0U);
    CHECK(prepared.resources.revision != 0U);
    const auto& base = prepared.resources.descriptors[0];
    CHECK(base.status == api::HudResourceStatus::ready);
    CHECK(base.entries[0].resource_id == base.entries[1].resource_id);
    CHECK(base.entries[0].resource_revision == base.entries[1].resource_revision);
    CHECK(base.entries[0].blend == api::HudSpriteBlend::additive);
    CHECK(base.entries[1].source == api::HudSourceRectangle{1U, 1U, 1U, 1U});
    CHECK(source.reads["sprites/sheet.spr"] == 1U);
    CHECK(source.reads["sprites/mask.spr"] == 1U);
    const auto& cross = prepared.resources.descriptors[1].entries[0];
    CHECK(cross.blend == api::HudSpriteBlend::alpha_test);
    CHECK(cross.asset_sha256.size() == 64U);
    const auto mask = std::find_if(prepared.sprites.begin(), prepared.sprites.end(), [&](const auto& value) { return value.resource_id == cross.resource_id; });
    REQUIRE(mask != prepared.sprites.end());
    CHECK(mask->asset->source_identity().revision == cross.resource_revision);
    CHECK(mask->asset->frames()[0].rgba8[11] == std::byte{0});
    CHECK(mask->asset->frames()[0].rgba8[3] == std::byte{255});
    source.files["sprites/mask.spr"].back() = std::byte{4};
    const auto revised = g::prepare_hud_assets(source, requests);
    CHECK(revised.resources.revision != prepared.resources.revision);
    CHECK(revised.resources.descriptors[1].entries[0].resource_revision != cross.resource_revision);
    CHECK(mask->asset->frames()[0].rgba8[12] == std::byte{2});
}

TEST_CASE("E29 missing invalid crop and malformed sprite fail without foreign resource substitution", "[e29][hud-resources][assets]") {
    FixtureSource source;
    source.add("sprites/hud.txt", "3\ncrosshair 640 missing 0 0 1 1\nweapon 640 sheet 1 1 2 2\nammo 640 broken 0 0 1 1\n");
    source.files["sprites/sheet.spr"] = fixture::literal_single_sprite(1);
    source.files["sprites/broken.spr"] = {std::byte{0}};
    const std::array<api::HudManifestRequest, 2U> requests{{{"sprites/hud.txt", {}}, {"sprites/absent.txt", {}}}};
    const auto prepared = g::prepare_hud_assets(source, requests);
    REQUIRE(prepared.resources.descriptors.size() == 2U);
    const auto& entries = prepared.resources.descriptors[0].entries;
    REQUIRE(entries.size() == 3U);
    CHECK(entries[0].status == api::HudResourceStatus::missing_file);
    CHECK(entries[1].status == api::HudResourceStatus::invalid_rectangle);
    CHECK(entries[2].status == api::HudResourceStatus::decode_failed);
    CHECK(prepared.resources.descriptors[1].status == api::HudResourceStatus::missing_file);
    for (const auto& entry : entries) { CHECK(entry.resource_id == 0U); CHECK(entry.resource_revision == 0U); }
    CHECK(prepared.ready_entries == 0U);
    CHECK(prepared.rejected_entries == 3U);
    CHECK_FALSE(g::valid_hud_source_rectangle({0U, 0U, 1U, 1U}, 0U, 1U));
    CHECK_FALSE(g::valid_hud_source_rectangle({0xffffffffU, 0U, 2U, 1U}, 2U, 2U));
}

TEST_CASE("E29 approved local HUD preparation stays on selected world root without server sprite slots", "[e29][hud-resources][approved]") {
    hlclient::tests::ScopedLocalResourceTestRoot root;
    root.create_game("sample");
    root.write("sample", "maps/test_map.bsp", "map");
    root.write("sample", "sprites/hud.txt", "1\ncrosshair 640 selected 0 0 2 2\n");
    root.write("sample", "sprites/selected.spr", fixture::literal_single_sprite(3));
    root.write("valve", "sprites/hud.txt", "1\ncrosshair 640 foreign 0 0 2 2\n");
    root.write("valve", "sprites/foreign.spr", fixture::literal_single_sprite(1));
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{
        hlclient::tests::readiness_fixture::make_environment(root, "sample")};
    const std::array<api::HudManifestRequest, 1U> requests{{{"sprites/hud.txt", {}}}};
    g::ApprovedHudAssets assets{resources_for(environment, "maps/test_map.bsp"), requests};
    const auto prepared = wait_prepared(assets);
    REQUIRE(prepared);
    REQUIRE(prepared->ready_entries == 1U);
    CHECK(prepared->resources.descriptors[0].entries[0].sprite_name == "sprites/selected.spr");
    CHECK(prepared->resources.descriptors[0].entries[0].blend == api::HudSpriteBlend::alpha_test);
    CHECK(assets.snapshot() == prepared);
    CHECK(prepared->sprites.size() == 1U);
}

TEST_CASE("E29 bounded descriptor authorization avoids invalid reads and stop cancels preparation", "[e29][hud-resources][assets]") {
    FixtureSource source;
    source.add("sprites/hud.txt", "0\n");
    const std::array<api::HudManifestRequest, 2U> rejected{{{"../hud.txt", {}}, {"sprites/a/b.txt", {}}}};
    const auto invalid = g::prepare_hud_assets(source, rejected);
    REQUIRE(invalid.resources.descriptors.size() == 2U);
    CHECK(source.reads.empty());
    CHECK(invalid.resources.descriptors[0].status == api::HudResourceStatus::not_authorized);
    std::stop_source stop; stop.request_stop();
    const std::array<api::HudManifestRequest, 1U> requests{{{"sprites/hud.txt", {}}}};
    const auto cancelled = g::prepare_hud_assets(source, requests, stop.get_token());
    CHECK(cancelled.resources.descriptors[0].status == api::HudResourceStatus::not_yet_resolved);
    CHECK(source.reads.empty());
    const std::vector<api::HudManifestRequest> too_many(16U, requests[0]);
    CHECK(g::prepare_hud_assets(source, too_many).resources.descriptors.empty());
    g::ApprovedHudAssets rejected_worker{{}, too_many};
    const auto rejected_snapshot = rejected_worker.snapshot();
    REQUIRE(rejected_snapshot);
    REQUIRE(rejected_snapshot->resources.descriptors.size() == 15U);
    CHECK(rejected_snapshot->resources.revision != 0U);
    CHECK(rejected_snapshot->resources.descriptors[0].status == api::HudResourceStatus::not_authorized);
    const std::array<api::HudManifestRequest, 1U> oversized_name{{{std::string(1U * 1024U * 1024U, 'a'), {}}}};
    g::ApprovedHudAssets oversized_worker{{}, oversized_name};
    const auto oversized_snapshot = oversized_worker.snapshot();
    REQUIRE(oversized_snapshot);
    REQUIRE(oversized_snapshot->resources.descriptors.size() == 1U);
    CHECK(oversized_snapshot->resources.descriptors[0].name.empty());
    CHECK(oversized_snapshot->resources.descriptors[0].status == api::HudResourceStatus::not_authorized);
}
