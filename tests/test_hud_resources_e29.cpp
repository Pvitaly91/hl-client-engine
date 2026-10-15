#include <hlclient/games/halflife/hud_resource_profile.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include "hud_resources_test_fixture_e29.hpp"
#include <cmath>
#include <iostream>

using namespace hlclient::tests::hud_resources_e29;
namespace {
std::map<std::string, api::LocalSoundReference> compiled_audio_dependencies(api::IGameClientModule& module) {
    std::map<std::string, api::LocalSoundReference> dependencies;
    const auto add = [&](const api::LocalSoundReference& reference) {
        REQUIRE(g::valid_local_sound_reference(reference));
        dependencies.emplace(std::string{reference.name()}, reference);
    };
    for (const auto& reference : module.movement_sound_preparation()) add(reference);
    for (const auto& reference : module.temporary_sound_preparation()) add(reference);
    for (const auto& reference : module.hud_sound_preparation()) add(reference);
    if (const auto profile = module.local_impact_assets()) {
        add(profile->sound); add(profile->shell_contact_sound);
        for (std::size_t i = 0U; i < profile->supplemental_sound_count; ++i) add(profile->supplemental_sounds[i]);
        for (std::size_t i = 0U; i < profile->material_sound_count; ++i) add(profile->material_sounds[i]);
    }
    if (const auto profile = module.local_crowbar_impact_assets()) {
        for (const auto& reference : profile->strike_sounds) add(reference);
        add(profile->concrete_contact_sound);
    }
    constexpr std::array<std::string_view, 18U> event_names{
        "events/glock1.sc", "events/glock2.sc", "events/crowbar.sc", "events/mp5.sc", "events/mp52.sc",
        "events/shotgun1.sc", "events/shotgun2.sc", "events/python.sc", "events/crossbow1.sc", "events/crossbow2.sc",
        "events/rpg.sc", "events/firehornet.sc", "events/gauss.sc", "events/gaussspin.sc",
        "events/egon_fire.sc", "events/egon_stop.sc", "events/tripfire.sc", "events/snarkfire.sc"};
    std::array<api::ScriptedEventBinding, event_names.size()> events{};
    for (std::size_t i = 0U; i < events.size(); ++i) {
        events[i].index = static_cast<std::uint16_t>(i + 1U);
        std::copy(event_names[i].begin(), event_names[i].end(), events[i].name.begin());
    }
    module.configure_scripted_events(events);
    const auto remote = module.drain_remote_audio();
    for (std::size_t i = 0U; i < remote.prepare_count; ++i) add(remote.prepare[i]);
    // The same bounded production outbox may split startup preloads over ticks.
    // No commands, hits, playback, or player observations are generated here.
    for (unsigned tick = 0U; tick < 4U; ++tick) {
        const auto local = module.drain_audio();
        for (std::size_t i = 0U; i < local.prepare_count; ++i) add(local.prepare[i]);
    }
    return dependencies;
}
}

TEST_CASE("E29 Half-Life resource profile contains fourteen catalogue descriptors and pinned aliases", "[e29][hud-resources][halflife]") {
    namespace hl = hlclient::games::halflife;
    const auto requests = hl::halflife_hud_resource_preparation();
    REQUIRE(requests.size() == 15U);
    CHECK(requests.front().name == "sprites/hud.txt");
    REQUIRE(requests.front().count_compatibility);
    CHECK(requests.front().count_compatibility->declared_count == 257U);
    CHECK(requests.front().count_compatibility->parsed_count == 247U);
    CHECK(hl::halflife_hud_descriptor_name("weapon_glock") == "sprites/weapon_9mmhandgun.txt");
    CHECK(hl::halflife_hud_descriptor_name("weapon_python") == "sprites/weapon_357.txt");
    CHECK(hl::halflife_hud_descriptor_name("weapon_mp5") == "sprites/weapon_9mmAR.txt");
    CHECK(hl::halflife_hud_descriptor_name("weapon_unknown").empty());
    CHECK(hl::halflife_hud_descriptor_name("models/v_9mmhandgun.mdl").empty());
}

TEST_CASE("E29 compiled startup and remote effect dependencies leave bounded room for four menu samples", "[e29][hud-menu][audio-capacity]") {
    auto module = hlclient::games::halflife::make_half_life_client_module();
    module->reset({1U, 1U, 1U});
    const auto dependencies = compiled_audio_dependencies(*module);
    CHECK(dependencies.size() <= g::kMaximumApprovedLocalSoundEntries);
    REQUIRE(module->hud_sound_preparation().size() == 4U);
    for (const auto& reference : module->hud_sound_preparation()) {
        CHECK(reference.source == api::SoundReferenceSource::module_profile_dependency);
        CHECK(dependencies.contains(std::string{reference.name()}));
    }
    std::cout << "[e29-audio-capacity] compiled_startup_and_remote_samples=" << dependencies.size()
              << "; local_cache_limit=" << g::kMaximumApprovedLocalSoundEntries
              << "; scope=compiled_profiles_excludes_runtime_studio_event_dependencies\n";
}

TEST_CASE("E29 actual menu WAV dependencies decode through approved production loader without playback", "[e29][hud-resources][actual-assets][menu-pcm]") {
    const auto basedir = environment_value("HLCLIENT_E29_ASSET_ROOT");
    if (basedir.empty()) SKIP("Set HLCLIENT_E29_ASSET_ROOT for explicit read-only approved menu PCM decode");
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(basedir, "valve"); REQUIRE(roots);
    auto made = hlclient::local_resources::LocalResourceEnvironment::create(std::move(*roots.roots)); REQUIRE(made);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{std::move(made.environment)};
    g::ApprovedSoundAssets sounds{resources_for(environment, "maps/crossfire.bsp")};
    auto module = hlclient::games::halflife::make_half_life_client_module();
    module->reset({1U, 1U, 1U});
    const auto dependencies = compiled_audio_dependencies(*module);
    REQUIRE(dependencies.size() <= g::kMaximumApprovedLocalSoundEntries);
    // Exercise existing cache allocation with the whole audited compiled set;
    // no Audio::Output/backend is created and no sample is played.
    for (const auto& [name, reference] : dependencies) {
        (void)name; CHECK(sounds.request_local(reference).status != g::SoundAssetStatus::limit);
    }
    constexpr std::array<std::string_view, 4U> expected{
        "common/wpn_hudon.wav", "common/wpn_moveselect.wav", "common/wpn_select.wav", "common/wpn_hudoff.wav"};
    const auto menu = module->hud_sound_preparation(); REQUIRE(menu.size() == expected.size());
    for (std::size_t i = 0U; i < menu.size(); ++i) {
        CAPTURE(i);
        CHECK(menu[i].name() == expected[i]);
        CHECK(menu[i].source == api::SoundReferenceSource::module_profile_dependency);
        auto result = sounds.request_local(menu[i]);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (result.status == g::SoundAssetStatus::pending && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1}); result = sounds.request_local(menu[i]);
        }
        REQUIRE(result.status == g::SoundAssetStatus::ready); REQUIRE(result.asset);
        CHECK(result.asset->sample_rate > 0U); CHECK(result.asset->sample_rate <= 192'000U);
        CHECK((result.asset->channel_count == 1U || result.asset->channel_count == 2U));
        REQUIRE_FALSE(result.asset->interleaved_samples.empty());
        CHECK(std::all_of(result.asset->interleaved_samples.begin(), result.asset->interleaved_samples.end(), [](float sample) { return std::isfinite(sample); }));
        CHECK(std::any_of(result.asset->interleaved_samples.begin(), result.asset->interleaved_samples.end(), [](float sample) { return sample != 0.0F; }));
        double energy = 0.0;
        for (const auto sample : result.asset->interleaved_samples) energy += static_cast<double>(sample) * sample;
        CHECK(std::isfinite(energy)); CHECK(energy > 0.0);
        CHECK(sounds.request_local(menu[i]).asset == result.asset);
    }
}

TEST_CASE("E29 actual approved installed HUD resources produce immutable full coverage", "[e29][hud-resources][actual-assets]") {
    const auto basedir = environment_value("HLCLIENT_E29_ASSET_ROOT");
    if (basedir.empty()) SKIP("Set HLCLIENT_E29_ASSET_ROOT for explicit read-only approved asset preparation");
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(basedir, "valve"); REQUIRE(roots);
    auto made = hlclient::local_resources::LocalResourceEnvironment::create(std::move(*roots.roots)); REQUIRE(made);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{std::move(made.environment)};
    const auto requests = hlclient::games::halflife::halflife_hud_resource_preparation();
    g::ApprovedHudAssets assets{resources_for(environment, "maps/crossfire.bsp"), requests};
    const auto prepared = wait_prepared(assets);
    REQUIRE(prepared);
    REQUIRE(prepared->resources.descriptors.size() == 15U);
    CHECK(prepared->ready_entries == 483U);
    CHECK(prepared->rejected_entries == 0U);
    CHECK(prepared->sprites.size() == 224U);
    CHECK(prepared->source_bytes == 9'555'520U + 20'195U);
    CHECK(prepared->retained_decoded_bytes <= g::kMaximumHudDecodedBytes);
    CHECK(prepared->resources.descriptors.front().declared_count == 257U);
    CHECK(prepared->resources.descriptors.front().parsed_count == 247U);
    CHECK(prepared->resources.descriptors.front().exact_count_compatibility_applied);
    for (const auto& descriptor : prepared->resources.descriptors) {
        REQUIRE(descriptor.status == api::HudResourceStatus::ready);
        for (const auto& entry : descriptor.entries) {
            CHECK(entry.status == api::HudResourceStatus::ready);
            CHECK(g::valid_hud_source_rectangle(entry.source, entry.frame_width, entry.frame_height));
            CHECK(entry.asset_sha256.size() == 64U);
        }
    }
    if (const auto output = environment_value("HLCLIENT_E29_COVERAGE_OUTPUT"); !output.empty()) {
        std::ofstream file{output, std::ios::binary | std::ios::trunc}; REQUIRE(file);
        file << "{\n\"schema\":\"hlclient.hud-resource-coverage.e29.v1\",\n\"attribution\":\"approved_read_only_preparation\",\n"
             << "\"sdk_reference_revision\":\"b1b5cf5892918535619b2937bb927e46cb097ba1\",\n"
             << "\"source_bytes\":" << prepared->source_bytes << ",\n\"retained_decoded_bytes\":" << prepared->retained_decoded_bytes
             << ",\n\"dependencies\":" << prepared->sprites.size() << ",\n\"descriptors\":[\n";
        bool first_descriptor = true;
        std::size_t descriptor_index = 0U;
        constexpr std::array<unsigned, 15U> weapon_ids{0U, 1U, 2U, 3U, 4U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U};
        for (const auto& descriptor : prepared->resources.descriptors) {
            if (!first_descriptor) file << ",\n"; first_descriptor = false;
            file << "{\"name\":\"" << descriptor.name << "\",\"weapon_id\":" << weapon_ids[descriptor_index++]
                 << ",\"status\":\"" << api::to_string(descriptor.status)
                 << "\",\"sha256\":\"" << descriptor.source_sha256 << "\",\"declared_count\":" << descriptor.declared_count.value_or(0U)
                 << ",\"parsed_count\":" << descriptor.parsed_count << ",\"exact_count_compatibility_applied\":"
                 << (descriptor.exact_count_compatibility_applied ? "true" : "false");
            std::set<std::uint32_t> tiers;
            for (const auto& entry : descriptor.entries) tiers.insert(entry.resolution);
            file << ",\"resolution_tiers\":[";
            bool first_tier = true;
            for (const auto tier : tiers) { if (!first_tier) file << ','; first_tier = false; file << tier; }
            file << "]";
            if (descriptor.name != "sprites/hud.txt") {
                file << ",\"absent_by_asset_contract\":[";
                bool first_absence = true;
                for (const auto role : {"crosshair", "autoaim", "zoom", "zoom_autoaim", "weapon", "weapon_s", "ammo", "ammo2"}) {
                    if (std::any_of(descriptor.entries.begin(), descriptor.entries.end(), [&](const auto& entry) { return entry.role == role; })) continue;
                    if (!first_absence) file << ','; first_absence = false; file << '"' << role << '"';
                }
                file << "]";
            }
            file << ",\"entries\":[";
            bool first_entry = true;
            for (const auto& entry : descriptor.entries) {
                if (!first_entry) file << ','; first_entry = false;
                file << "{\"role\":\"" << entry.role << "\",\"resolution\":" << entry.resolution
                     << ",\"sprite\":\"" << entry.sprite_name << "\",\"sha256\":\"" << entry.asset_sha256
                     << "\",\"resource_id\":" << entry.resource_id << ",\"revision\":" << entry.resource_revision
                     << ",\"frame\":" << entry.frame << ",\"source\":[" << entry.source.x << ',' << entry.source.y << ','
                     << entry.source.width << ',' << entry.source.height << "],\"frame_size\":[" << entry.frame_width << ',' << entry.frame_height
                     << "],\"blend\":\"" << (entry.blend == api::HudSpriteBlend::alpha_test ? "alpha_test" : "additive")
                     << "\",\"status\":\"" << api::to_string(entry.status) << "\"}";
            }
            file << "]}";
        }
        file << "\n]}\n"; REQUIRE(file.good());
    }
}
