#include "hud_resources_test_fixture_e29.hpp"

using namespace hlclient::tests::hud_resources_e29;

namespace {
std::vector<std::byte> supplemental_frame_fixture(std::int32_t format = 1) {
    auto bytes = fixture::literal_header_and_palette(4, format, 2, 0, 3, 3);
    fixture::append_single_entry(bytes, {-1, 2, 2, 2,
        {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}});
    fixture::append_single_entry(bytes, {7, -3, 1, 3,
        {std::byte{5}, std::byte{6}, std::byte{7}}});
    fixture::append_single_entry(bytes, {-9, 1, 3, 1,
        {std::byte{8}, std::byte{9}, std::byte{10}}});
    fixture::append_single_entry(bytes, {0, 0, 1, 1, {std::byte{11}}});
    return bytes;
}
api::HudManifestRequest supplemental_request() {
    return {"sprites/hud.txt", {}, {{"pain_front", 640U, "sprites/pain.spr", 0U},
        {"pain_right", 640U, "sprites/pain.spr", 1U},
        {"pain_rear", 640U, "sprites/pain.spr", 2U},
        {"pain_left", 640U, "sprites/pain.spr", 3U}}};
}
}

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

TEST_CASE("HUD supplemental sprites retain each requested frame geometry format and shared source", "[e29][hud-resources][assets][hud-supplemental]") {
    constexpr std::array<api::HudSpriteBlend, 4U> blends{api::HudSpriteBlend::opaque,
        api::HudSpriteBlend::additive, api::HudSpriteBlend::straight_alpha, api::HudSpriteBlend::alpha_test};
    constexpr std::array<std::uint32_t, 4U> widths{2U, 1U, 3U, 1U}, heights{2U, 3U, 1U, 1U};
    for (std::int32_t format = 0; format < 4; ++format) {
        CAPTURE(format);
        FixtureSource source;
        source.add("sprites/hud.txt", "1\nnumber_0 640 pain 1 1 1 1\n");
        source.files["sprites/pain.spr"] = supplemental_frame_fixture(format);
        std::array requests{supplemental_request()};
        const auto prepared = g::prepare_hud_assets(source, requests);
        REQUIRE(prepared.resources.descriptors.size() == 1U);
        const auto& descriptor = prepared.resources.descriptors.front();
        REQUIRE(descriptor.status == api::HudResourceStatus::ready);
        CHECK(descriptor.declared_count == 1U); CHECK(descriptor.parsed_count == 1U);
        CHECK_FALSE(descriptor.exact_count_compatibility_applied);
        REQUIRE(descriptor.entries.size() == 5U);
        CHECK(descriptor.entries[0].source == api::HudSourceRectangle{1U, 1U, 1U, 1U});
        CHECK(prepared.ready_entries == 5U); CHECK(prepared.rejected_entries == 0U);
        REQUIRE(prepared.sprites.size() == 1U);
        CHECK(source.reads["sprites/pain.spr"] == 1U);
        CHECK(prepared.source_bytes == source.files["sprites/hud.txt"].size() + source.files["sprites/pain.spr"].size());
        CHECK(prepared.retained_decoded_bytes == (4U + 3U + 3U + 1U) * 4U);
        for (std::size_t i = 0U; i < 4U; ++i) {
            const auto& entry = descriptor.entries[i + 1U];
            REQUIRE(entry.status == api::HudResourceStatus::ready);
            CHECK(entry.frame == i); CHECK(entry.frame_width == widths[i]); CHECK(entry.frame_height == heights[i]);
            CHECK(entry.source == api::HudSourceRectangle{0U, 0U, widths[i], heights[i]});
            CHECK(entry.blend == blends[static_cast<std::size_t>(format)]);
            CHECK(entry.resource_id == descriptor.entries[0].resource_id);
            CHECK(entry.resource_revision == descriptor.entries[0].resource_revision);
            CHECK(entry.asset_sha256 == descriptor.entries[0].asset_sha256);
            CHECK(prepared.sprites[0].asset->frames()[entry.frame].geometry.width == widths[i]);
        }
        const auto original_revision = prepared.resources.revision;
        requests[0].supplemental_sprites[0].frame = 1U;
        const auto different_frame = g::prepare_hud_assets(source, requests);
        CHECK(different_frame.resources.revision != original_revision);
        CHECK(different_frame.resources.descriptors[0].entries[1].resource_revision == descriptor.entries[1].resource_revision);
        requests[0] = supplemental_request(); requests[0].supplemental_sprites[0].resolution = 1280U;
        CHECK(g::prepare_hud_assets(source, requests).resources.revision != original_revision);
        requests[0] = supplemental_request(); requests[0].supplemental_sprites[0].role = "another_role";
        CHECK(g::prepare_hud_assets(source, requests).resources.revision != original_revision);
    }
}

TEST_CASE("HUD supplemental authorization rejects unsafe names roles tiers frames and duplicates before reads", "[e29][hud-resources][assets][hud-supplemental]") {
    std::vector<api::HudSpriteRequest> invalid;
    for (const auto name : {"../pain.spr", "C:/pain.spr", "sprites/../pain.spr", "sprites//pain.spr",
        "sprites/a/../pain.spr", "sprites/pain.spr/", "sprites/pain.spr;exec", "sprites/.spr",
        "sprites/pain.txt", "models/pain.spr", "sprites/a\\pain.spr"})
        invalid.push_back({"pain_front", 640U, name, 0U});
    invalid.push_back({"pain_front", 640U, "sprites/" + std::string(64U, 'a') + ".spr", 0U});
    invalid.push_back({"pain_front", 640U, std::string{"sprites/pain\0x.spr", 18U}, 0U});
    for (const auto role : {"", "bad role", "bad/role", "bad;role", "bad.role"})
        invalid.push_back({role, 640U, "sprites/pain.spr", 0U});
    invalid.push_back({std::string(64U, 'r'), 640U, "sprites/pain.spr", 0U});
    invalid.push_back({"pain_front", 0U, "sprites/pain.spr", 0U});
    invalid.push_back({"pain_front", 65'536U, "sprites/pain.spr", 0U});
    invalid.push_back({"pain_front", 640U, "sprites/pain.spr", 64U});
    for (const auto& sprite : invalid) {
        FixtureSource source; source.add("sprites/hud.txt", "0\n");
        const std::array<api::HudManifestRequest, 1U> requests{{{"sprites/hud.txt", {}, {sprite}}}};
        const auto prepared = g::prepare_hud_assets(source, requests);
        REQUIRE(prepared.resources.descriptors.size() == 1U);
        CHECK(prepared.resources.descriptors[0].status == api::HudResourceStatus::not_authorized);
        CHECK(prepared.resources.descriptors[0].entries.empty()); CHECK(source.reads.empty());
    }
    FixtureSource source; source.add("sprites/hud.txt", "0\n");
    std::array requests{supplemental_request()};
    requests[0].supplemental_sprites.push_back(requests[0].supplemental_sprites.front());
    requests[0].supplemental_sprites.back().frame = 1U;
    CHECK(g::prepare_hud_assets(source, requests).resources.descriptors[0].status == api::HudResourceStatus::not_authorized);
    CHECK(source.reads.empty());
    requests[0] = supplemental_request(); requests[0].supplemental_sprites.resize(513U);
    CHECK(g::prepare_hud_assets(source, requests).resources.descriptors[0].status == api::HudResourceStatus::resource_limit);
    CHECK(source.reads.empty());
    g::ApprovedHudAssets rejected_worker{{}, requests};
    REQUIRE(rejected_worker.snapshot());
    CHECK(rejected_worker.snapshot()->resources.descriptors[0].status == api::HudResourceStatus::not_authorized);
}

TEST_CASE("HUD supplemental whole frames never bypass missing or invalid base manifests", "[e29][hud-resources][assets][hud-supplemental]") {
    const std::array requests{supplemental_request()};
    for (const auto manifest : {"", "2\nnumber_0 640 pain 0 0 1 1\n", "1\nnumber_0 640 pain 0 0 0 1\n"}) {
        FixtureSource source;
        if (manifest[0]) source.add("sprites/hud.txt", manifest);
        source.files["sprites/pain.spr"] = supplemental_frame_fixture();
        const auto prepared = g::prepare_hud_assets(source, requests);
        REQUIRE(prepared.resources.descriptors.size() == 1U);
        CHECK(prepared.resources.descriptors[0].status == (manifest[0] ?
            api::HudResourceStatus::invalid_manifest : api::HudResourceStatus::missing_file));
        CHECK(prepared.resources.descriptors[0].entries.empty());
        CHECK_FALSE(source.reads.contains("sprites/pain.spr")); CHECK(prepared.sprites.empty());
    }
    FixtureSource source;
    source.add("sprites/hud.txt", "1\npain_front 640 pain 0 0 1 1\n");
    CHECK(g::prepare_hud_assets(source, requests).resources.descriptors[0].status == api::HudResourceStatus::not_authorized);
    CHECK_FALSE(source.reads.contains("sprites/pain.spr"));
    // A syntactically valid but out-of-bounds manifest crop remains rejected;
    // it cannot acquire the whole-frame privilege of adjacent explicit requests.
    source.files.erase("sprites/hud.txt");
    source.add("sprites/hud.txt", "1\nnumber_0 640 pain 1 1 2 2\n");
    source.files["sprites/pain.spr"] = supplemental_frame_fixture();
    const auto prepared = g::prepare_hud_assets(source, requests);
    REQUIRE(prepared.resources.descriptors[0].entries.size() == 5U);
    CHECK(prepared.resources.descriptors[0].entries[0].status == api::HudResourceStatus::invalid_rectangle);
    CHECK(prepared.resources.descriptors[0].entries[0].source == api::HudSourceRectangle{1U, 1U, 2U, 2U});
    CHECK(prepared.ready_entries == 4U); CHECK(prepared.rejected_entries == 1U);
    source.reads.clear(); std::stop_source stopped; stopped.request_stop();
    CHECK(g::prepare_hud_assets(source, requests, stopped.get_token()).resources.descriptors[0].status == api::HudResourceStatus::not_yet_resolved);
    CHECK(source.reads.empty());
}

TEST_CASE("HUD supplemental requests reject absent frames malformed formats and oversized source", "[e29][hud-resources][assets][hud-supplemental]") {
    FixtureSource source; source.add("sprites/hud.txt", "0\n");
    std::array requests{supplemental_request()};
    source.files["sprites/pain.spr"] = fixture::literal_two_single_sprite();
    const auto prepared = g::prepare_hud_assets(source, requests);
    REQUIRE(prepared.resources.descriptors[0].entries.size() == 4U);
    CHECK(prepared.ready_entries == 2U); CHECK(prepared.rejected_entries == 2U);
    for (const auto index : {2U, 3U}) {
        const auto& entry = prepared.resources.descriptors[0].entries[index];
        CHECK(entry.status == api::HudResourceStatus::invalid_rectangle);
        CHECK(entry.resource_id == 0U); CHECK(entry.resource_revision == 0U);
        CHECK(entry.frame_width == 0U); CHECK(entry.frame_height == 0U);
    }
    source.files["sprites/pain.spr"] = supplemental_frame_fixture(99);
    const auto malformed = g::prepare_hud_assets(source, requests);
    CHECK(malformed.ready_entries == 0U); CHECK(malformed.rejected_entries == 4U);
    for (const auto& entry : malformed.resources.descriptors[0].entries) {
        CHECK(entry.status == api::HudResourceStatus::decode_failed); CHECK(entry.resource_id == 0U);
    }
    source.files["sprites/pain.spr"].resize(g::kMaximumHudSpriteSourceBytes + 1U);
    const auto oversized = g::prepare_hud_assets(source, requests);
    CHECK(oversized.source_bytes == source.files["sprites/hud.txt"].size());
    CHECK(oversized.sprites.empty()); CHECK(oversized.retained_decoded_bytes == 0U);
    for (const auto& entry : oversized.resources.descriptors[0].entries)
        CHECK(entry.status == api::HudResourceStatus::resource_limit);
}

TEST_CASE("HUD supplemental entries share global entry and dependency budgets with manifests", "[e29][hud-resources][assets][hud-supplemental]") {
    FixtureSource source;
    source.add("sprites/hud.txt", "1\nnumber_0 640 pain 0 0 1 1\n");
    source.files["sprites/pain.spr"] = supplemental_frame_fixture();
    std::array<api::HudManifestRequest, 1U> requests{{{"sprites/hud.txt", {}, {}}}};
    for (std::size_t i = 0U; i < 511U; ++i)
        requests[0].supplemental_sprites.push_back({"extra" + std::to_string(i), 640U, "sprites/pain.spr", 0U});
    const auto maximum = g::prepare_hud_assets(source, requests);
    CHECK(maximum.ready_entries == 512U); CHECK(maximum.sprites.size() == 1U);
    source.reads.clear();
    requests[0].supplemental_sprites.push_back({"extra511", 640U, "sprites/pain.spr", 0U});
    const auto overflow = g::prepare_hud_assets(source, requests);
    CHECK(overflow.resources.descriptors[0].status == api::HudResourceStatus::resource_limit);
    CHECK(overflow.resources.descriptors[0].entries.empty()); CHECK_FALSE(source.reads.contains("sprites/pain.spr"));
    const std::array<api::HudManifestRequest, 2U> aggregate{{requests[0], {"sprites/extra.txt", {},
        {{"more", 640U, "sprites/pain.spr", 0U}}}}};
    source.reads.clear();
    CHECK(g::prepare_hud_assets(source, aggregate).resources.descriptors[0].status == api::HudResourceStatus::resource_limit);
    CHECK(source.reads.empty());

    FixtureSource many; many.add("sprites/hud.txt", "0\n");
    requests[0].supplemental_sprites.clear();
    for (std::size_t i = 0U; i < 257U; ++i) {
        const auto name = "sprites/sheet" + std::to_string(i) + ".spr";
        requests[0].supplemental_sprites.push_back({"extra" + std::to_string(i), 640U, name, 0U});
        many.files[name] = fixture::literal_single_sprite(1);
    }
    const auto dependencies = g::prepare_hud_assets(many, requests);
    CHECK(dependencies.sprites.size() == api::kMaximumHudResourceDependencies);
    CHECK(dependencies.ready_entries == 256U); CHECK(dependencies.rejected_entries == 1U);
    CHECK(dependencies.resources.descriptors[0].entries.back().status == api::HudResourceStatus::resource_limit);
    CHECK_FALSE(many.reads.contains("sprites/sheet256.spr"));
}

TEST_CASE("HUD supplemental approved assets stay within the selected world root", "[e29][hud-resources][approved][hud-supplemental]") {
    hlclient::tests::ScopedLocalResourceTestRoot root; root.create_game("sample");
    root.write("sample", "maps/test_map.bsp", "map"); root.write("sample", "sprites/hud.txt", "0\n");
    root.write("sample", "sprites/pain.spr", supplemental_frame_fixture());
    root.write("valve", "sprites/foreign.spr", supplemental_frame_fixture());
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{
        hlclient::tests::readiness_fixture::make_environment(root, "sample")};
    std::array requests{supplemental_request()};
    requests[0].supplemental_sprites.push_back({"foreign", 640U, "sprites/foreign.spr", 0U});
    g::ApprovedHudAssets assets{resources_for(environment, "maps/test_map.bsp"), requests};
    const auto prepared = wait_prepared(assets); REQUIRE(prepared);
    REQUIRE(prepared->resources.descriptors[0].entries.size() == 5U);
    CHECK(prepared->ready_entries == 4U); CHECK(prepared->rejected_entries == 1U);
    CHECK(prepared->sprites.size() == 1U);
    CHECK(prepared->resources.descriptors[0].entries.back().status == api::HudResourceStatus::missing_file);
}
