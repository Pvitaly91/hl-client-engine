#include <hlclient/games/halflife/server_messages.hpp>
#include <hlclient/games/halflife/sprite_hud.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {
namespace api = hlclient::game_api;
namespace client = hlclient::client;
namespace hf = hlclient::games::halflife;
using Vector = std::array<double, 3>;
constexpr std::array<std::string_view, 4> pain_roles{
    "pain_front", "pain_right", "pain_rear", "pain_left"};

// Shape/role metadata is project-owned. No Valve sprite pixels are embedded.
api::HudResourceSet damage_resources() {
    api::HudResourceSet result;
    result.revision = 1;
    api::HudResourceDescriptor base;
    base.name = "sprites/hud.txt";
    base.status = api::HudResourceStatus::ready;
    for (const auto tier : {320U, 640U, 1280U, 2560U}) {
        const auto half = tier == 320U;
        for (std::uint32_t frame = 0; frame < pain_roles.size(); ++frame) {
            api::HudResourceEntry entry;
            entry.role = pain_roles[frame];
            entry.resolution = tier;
            entry.sprite_name = "sprites/project_owned_pain.spr";
            entry.resource_id = 1000U + tier;
            entry.resource_revision = 7;
            entry.frame = frame;
            const auto width = frame % 2U ? 48U : 128U;
            const auto height = frame % 2U ? 128U : 48U;
            entry.frame_width = half ? width / 2U : width;
            entry.frame_height = half ? height / 2U : height;
            entry.source = {0, 0, entry.frame_width, entry.frame_height};
            entry.blend = api::HudSpriteBlend::additive;
            entry.status = api::HudResourceStatus::ready;
            base.entries.push_back(std::move(entry));
        }
        // Existing HUD scale policy derives its high-resolution ratio from
        // digits, not from the damage sprite's independently sized frames.
        api::HudResourceEntry digit;
        digit.role = "number_0";
        digit.resolution = tier;
        digit.resource_id = 5000U + tier;
        digit.resource_revision = 7;
        const auto factor = tier >= 2560U ? 3U : tier >= 1280U ? 2U : 1U;
        digit.frame_width = 20U * factor;
        digit.frame_height = 24U * factor;
        digit.source = {0, 0, digit.frame_width, digit.frame_height};
        digit.status = api::HudResourceStatus::ready;
        base.entries.push_back(std::move(digit));
    }
    result.descriptors.push_back(std::move(base));
    return result;
}

client::RuntimeObservationSource source(std::size_t ordinal,
    std::size_t begin = 16, std::size_t end = 112) {
    return {static_cast<std::uint64_t>(ordinal + 1000U), ordinal,
        static_cast<std::uint32_t>(ordinal), begin, end};
}
client::RuntimeLifeEvent damage(Vector origin, std::size_t ordinal = 1,
    std::uint8_t health = 1, std::uint8_t armor = 0) {
    client::RuntimeLifeEvent result;
    result.kind = client::RuntimeLifeEventKind::damage;
    result.source = source(ordinal);
    result.damage_origin = origin;
    result.damage_taken = health;
    result.armor_saved = armor;
    return result;
}
std::vector<api::HudSprite> pain(const api::HudState& hud) {
    std::vector<api::HudSprite> result;
    for (const auto& sprite : hud.draw.sprites)
        if (sprite.draw_order == 30U) result.push_back(sprite);
    return result;
}
const api::HudSprite& frame(const std::vector<api::HudSprite>& sprites, unsigned index) {
    const auto found = std::find_if(sprites.begin(), sprites.end(),
        [=](const auto& sprite) { return sprite.frame == index; });
    REQUIRE(found != sprites.end());
    return *found;
}
struct DamageHud {
    hf::HalfLifeSpriteHud presentation;
    client::RuntimeClientObservationState state;
    api::HudPresentationContext context;
    DamageHud() {
        state.generation = 1;
        state.lifecycle.state = client::LocalPlayerLifeState::alive;
        state.lifecycle.life_epoch = 1;
        state.receiving_client.emplace();
        state.receiving_client->health = 100;
        state.receiving_client->origin = {10., 20., 30.};
        state.receiving_client->owned_weapon_bits = 0U; // Pain does not require a suit.
        state.weapon_hud.health = 100;
        context.viewport_width = 640;
        context.viewport_height = 480;
        context.crosshair = false;
        context.view = api::HudViewContext{{10., 20., 30.}, {1., 0., 0.}, {0., -1., 0.}};
        presentation.bind(damage_resources());
    }
    Vector point(Vector offset) const {
        for (std::size_t i = 0; i < offset.size(); ++i)
            offset[i] += context.view->player_origin[i];
        return offset;
    }
    api::GameRecordState record(std::vector<client::RuntimeLifeEvent> events) const {
        api::GameRecordState result;
        result.weapon_hud = state.weapon_hud;
        result.lifecycle = state.lifecycle;
        result.life_events = std::move(events);
        return result;
    }
    void commit(client::RuntimeLifeEvent event) {
        presentation.committed(record({std::move(event)}));
    }
    api::HudState draw(double seconds = 0.) {
        api::HudState result;
        result.health = state.weapon_hud.health;
        presentation.compose(state, result, context, 90., seconds);
        REQUIRE(api::valid_hud_draw_commands(result.draw));
        return result;
    }
};
}

TEST_CASE("Damage compass uses exact cardinal frames and per-frame source dimensions",
    "[hud-damage][hud][no-stock]") {
    constexpr std::array<Vector, 4> directions{{{100,0,0}, {0,-100,0}, {-100,0,0}, {0,100,0}}};
    constexpr std::array<std::array<float, 4>, 4> rectangles{{
        {256,96,128,48}, {416,176,48,128}, {256,336,128,48}, {176,176,48,128}}};
    for (std::size_t i = 0; i < directions.size(); ++i) {
        CAPTURE(i);
        DamageHud f;
        const auto original = f.state;
        f.commit(damage(f.point(directions[i])));
        const auto arrows = pain(f.draw());
        REQUIRE(arrows.size() == 1);
        const auto& arrow = frame(arrows, static_cast<unsigned>(i));
        CHECK(arrow.resource_id == 1640);
        CHECK(arrow.resource_revision == 7);
        CHECK(arrow.source.x == 0);
        CHECK(arrow.source.y == 0);
        CHECK(arrow.x == rectangles[i][0]); CHECK(arrow.y == rectangles[i][1]);
        CHECK(arrow.width == rectangles[i][2]); CHECK(arrow.height == rectangles[i][3]);
        CHECK(arrow.blend == api::HudSpriteBlend::additive);
        CHECK(arrow.color[0] == Catch::Approx(1));
        CHECK(arrow.color[1] == Catch::Approx(160. / 255.));
        CHECK(arrow.color[2] == 0); CHECK(arrow.color[3] == 1);
        CHECK(client::runtime_observation_canonical_hash(f.state) ==
            client::runtime_observation_canonical_hash(original));
        CHECK(f.state.receiving_client->health == original.receiving_client->health);
        CHECK(f.state.weapon_hud == original.weapon_hud);
    }
}

TEST_CASE("Damage compass uses 3D local view basis and keeps the event direction after turning",
    "[hud-damage][hud][no-stock]") {
    SECTION("diagonal lights front and right without normalizing in 2D") {
        DamageHud f;
        f.commit(damage(f.point({100,-100,0})));
        const auto arrows = pain(f.draw());
        REQUIRE(arrows.size() == 2);
        CHECK(frame(arrows,0).color[0] == Catch::Approx(std::sqrt(.5)).margin(.005));
        CHECK(frame(arrows,1).color[0] == Catch::Approx(std::sqrt(.5)).margin(.005));
        f.context.view->forward = {-1,0,0};
        f.context.view->right = {0,1,0};
        const auto turned = pain(f.draw());
        REQUIRE(turned.size() == 2);
        CHECK(frame(turned,0).color == frame(arrows,0).color);
        CHECK(frame(turned,1).color == frame(arrows,1).color);
    }
    SECTION("yaw90 uses positive X as right") {
        DamageHud f; f.context.view->forward = {0,1,0}; f.context.view->right = {1,0,0};
        f.commit(damage(f.point({100,0,0})));
        const auto arrows = pain(f.draw()); REQUIRE(arrows.size() == 1); CHECK(arrows[0].frame == 1);
    }
    SECTION("upward look considers vertical source in front") {
        DamageHud f; f.context.view->forward = {0,0,1};
        f.commit(damage(f.point({0,0,100})));
        const auto arrows = pain(f.draw()); REQUIRE(arrows.size() == 1); CHECK(arrows[0].frame == 0);
    }
    SECTION("rolled basis considers downward source on the right") {
        DamageHud f; f.context.view->right = {0,0,-1};
        f.commit(damage(f.point({0,0,-100})));
        const auto arrows = pain(f.draw()); REQUIRE(arrows.size() == 1); CHECK(arrows[0].frame == 1);
    }
    SECTION("vertical source with level view has no fabricated horizontal direction") {
        DamageHud f; f.commit(damage(f.point({0,0,100}))); CHECK(pain(f.draw()).empty());
    }
}

TEST_CASE("Damage compass preserves nearby origin and amount edge cases",
    "[hud-damage][hud][no-stock]") {
    for (const auto distance : {0.,49.875,50.,50.125}) {
        CAPTURE(distance);
        DamageHud f; f.commit(damage(f.point({distance,0,0})));
        CHECK(pain(f.draw()).size() == (distance <= 50. ? 4U : 1U));
    }
    for (const auto amount : {1U,8U,255U}) {
        for (const bool armor_only : {false,true}) {
            CAPTURE(amount,armor_only);
            DamageHud f;
            f.commit(damage(f.point({100,0,0}),1,
                armor_only ? 0 : static_cast<std::uint8_t>(amount),
                armor_only ? static_cast<std::uint8_t>(amount) : 0));
            const auto arrows = pain(f.draw()); REQUIRE(arrows.size() == 1);
            CHECK(arrows[0].color[0] == 1);
        }
    }
    DamageHud f;
    f.commit(damage(f.point({100,0,0}),1)); REQUIRE(pain(f.draw()).size() == 1);
    auto zero_amount = damage({0,0,0},2,0,0); zero_amount.damage_bits = 1U << 17U;
    f.commit(zero_amount); CHECK(pain(f.draw()).size() == 1); // Type/status only cannot clear it.
    f.commit(damage({0,0,0},3)); CHECK(pain(f.draw()).empty()); // Sentinel precedes near-source handling.
}

TEST_CASE("Damage compass draw threshold and max accumulation follow the original profile",
    "[hud-damage][hud][no-stock]") {
    for (const auto dot : {.299,.301,.399,.401,.6}) {
        CAPTURE(dot);
        DamageHud f;
        f.commit(damage(f.point({100.*dot,0,100.*std::sqrt(1.-dot*dot)})));
        const auto arrows = pain(f.draw());
        REQUIRE(arrows.size() == (dot > .4 ? 1U : 0U));
        if (!arrows.empty()) CHECK(arrows[0].color[0] == Catch::Approx(std::max(.5,dot)).margin(.005));
    }
    DamageHud f;
    f.commit(damage(f.point({100,0,0}),1)); REQUIRE(pain(f.draw()).size() == 1);
    f.commit(damage(f.point({60,0,80}),2));
    const auto saturated = pain(f.draw()); REQUIRE(saturated.size() == 1);
    CHECK(saturated[0].color[0] == 1); // A weaker second direction must not replace the first.
    f.commit(damage(f.point({0,100,0}),3));
    const auto combined = pain(f.draw()); REQUIRE(combined.size() == 2);
    CHECK(frame(combined,0).color[0] == 1); CHECK(frame(combined,3).color[0] == 1);
}

TEST_CASE("Damage compass decays after drawing and does not restart from duplicate records",
    "[hud-damage][hud][no-stock]") {
    DamageHud f;
    auto event = damage(f.point({100,0,0}),100);
    f.commit(event);
    auto arrows = pain(f.draw(10.)); REQUIRE(arrows.size() == 1); CHECK(arrows[0].color[0] == 1);
    arrows = pain(f.draw(10.125)); REQUIRE(arrows.size() == 1); CHECK(arrows[0].color[0] == 1);
    f.commit(event);
    auto stale = event; stale.source = source(99); f.commit(stale);
    arrows = pain(f.draw(10.25)); REQUIRE(arrows.size() == 1);
    CHECK(arrows[0].color[0] == Catch::Approx(.75).margin(.005));
    arrows = pain(f.draw(10.375)); REQUIRE(arrows.size() == 1);
    CHECK(arrows[0].color[0] == Catch::Approx(.5).margin(.005));
    CHECK(pain(f.draw(10.5)).empty());
    f.commit(event); CHECK(pain(f.draw(10.625)).empty());
    event.source = source(101); f.commit(event);
    arrows = pain(f.draw(10.75)); REQUIRE(arrows.size() == 1); CHECK(arrows[0].color[0] == 1);
}

TEST_CASE("Damage compass retains native tier pixels independently of automatic and user HUD scale",
    "[hud-damage][hud][no-stock]") {
    struct Viewport { std::uint32_t width, height, tier; };
    constexpr std::array<Viewport,4> viewports{{
        {320,240,320}, {640,480,640}, {1920,1080,1280}, {3840,2160,2560}}};
    for (const auto viewport : viewports) {
        for (const bool automatic : {false,true}) {
            for (const auto scale : {.5F,1.F,2.F}) {
                CAPTURE(viewport.width,viewport.height,viewport.tier,automatic,scale);
                DamageHud f;
                f.context.viewport_width = viewport.width;
                f.context.viewport_height = viewport.height;
                f.context.hud_scale_automatic = automatic; f.context.hud_scale = scale;
                f.commit(damage(f.point({0,0,0}))); // A nearby source displays all four frames.
                const auto hud = f.draw(); const auto arrows = pain(hud);
                REQUIRE(arrows.size() == 4);
                CHECK(hud.hud_user_scale == Catch::Approx(automatic ? 1.F : scale));
                for (unsigned index = 0; index < 4; ++index) {
                    const auto& arrow = frame(arrows,index);
                    const float divisor = viewport.tier == 320 ? 2.F : 1.F;
                    const float w = (index%2 ? 48.F : 128.F)/divisor;
                    const float h = (index%2 ? 128.F : 48.F)/divisor;
                    const float center_x = static_cast<float>(viewport.width)/2;
                    const float center_y = static_cast<float>(viewport.height)/2;
                    const float x = index == 1 ? center_x+2*w : index == 3 ? center_x-3*w : center_x-w/2;
                    const float y = index == 0 ? center_y-3*h : index == 2 ? center_y+2*h : center_y-h/2;
                    CHECK(arrow.resource_id == 1000U+viewport.tier);
                    CHECK(arrow.width == w); CHECK(arrow.height == h);
                    CHECK(arrow.x == x); CHECK(arrow.y == y);
                }
            }
        }
    }
    DamageHud high;
    high.context.viewport_width = 1920; high.context.viewport_height = 1080;
    high.commit(damage(high.point({100,0,0})));
    const auto hud = high.draw(); const auto arrows = pain(hud); REQUIRE(arrows.size() == 1);
    CHECK(hud.hud_resource_tier == 1280);
    CHECK(hud.hud_effective_scale == Catch::Approx(.75));
    CHECK(arrows[0].resource_id == 2280);
    CHECK(arrows[0].width == Catch::Approx(128)); CHECK(arrows[0].height == Catch::Approx(48));
    CHECK(arrows[0].x == Catch::Approx(896)); CHECK(arrows[0].y == Catch::Approx(396));
    DamageHud small; small.context.viewport_width = 320; small.context.viewport_height = 240;
    small.commit(damage(small.point({-100,0,0})));
    const auto low = pain(small.draw()); REQUIRE(low.size() == 1);
    CHECK(low[0].resource_id == 1320); CHECK(low[0].frame == 2);
    CHECK(low[0].width == 64); CHECK(low[0].height == 24);
    CHECK(low[0].x == 128); CHECK(low[0].y == 168);
}

TEST_CASE("Damage compass fade remains bounded across draw rates and backwards clocks",
    "[hud-damage][hud][no-stock]") {
    for (const auto hz : {30U,60U,144U}) {
        CAPTURE(hz);
        DamageHud f; f.commit(damage(f.point({100,0,0})));
        REQUIRE(pain(f.draw(0)).size() == 1);
        float previous = 1;
        for (unsigned index = 1; index <= hz; ++index) {
            const auto arrows = pain(f.draw(static_cast<double>(index)/hz));
            if (!arrows.empty()) {
                REQUIRE(arrows.size() == 1);
                CHECK(arrows[0].color[0] <= previous);
                CHECK(arrows[0].color[0] >= .49F);
                previous = arrows[0].color[0];
            }
            if (static_cast<double>(index)/hz > .3+2./hz) CHECK(arrows.empty());
        }
    }
    DamageHud f; f.commit(damage(f.point({100,0,0})));
    REQUIRE(pain(f.draw(10)).size() == 1); REQUIRE(pain(f.draw(10.1)).size() == 1);
    CHECK(pain(f.draw(9)).empty());
    const auto resumed = pain(f.draw(10.1)); REQUIRE(resumed.size() == 1);
    CHECK(resumed[0].color[0] == Catch::Approx(.8).margin(.005));
}

TEST_CASE("Damage compass respects health tint hiding and missing view without requiring the suit",
    "[hud-damage][hud][no-stock]") {
    for (const auto health : {100U,26U,25U,15U}) {
        CAPTURE(health);
        DamageHud f; f.state.weapon_hud.health = static_cast<std::uint8_t>(health);
        f.commit(damage(f.point({100,0,0})));
        const auto arrows = pain(f.draw()); REQUIRE(arrows.size() == 1);
        CHECK(arrows[0].color[0] == Catch::Approx(health > 25 ? 1. : 250./255.));
        CHECK(arrows[0].color[1] == Catch::Approx(health > 25 ? 160./255. : 0.));
    }
    for (const auto hidden : {0U,4U,8U}) {
        CAPTURE(hidden);
        DamageHud f; f.state.weapon_hud.hide_flags = static_cast<std::uint8_t>(hidden);
        f.context.hud_draw = hidden != 0; // Exercise hud_draw false separately from hide bits.
        f.commit(damage(f.point({100,0,0})));
        CHECK(pain(f.draw()).empty());
    }
    SECTION("hidden health freezes decay while HUD frame clock continues") {
        DamageHud f; f.commit(damage(f.point({100,0,0})));
        REQUIRE(pain(f.draw(0)).size() == 1);
        f.state.weapon_hud.hide_flags = 8;
        CHECK(pain(f.draw(1)).empty()); CHECK(pain(f.draw(2)).empty());
        f.state.weapon_hud.hide_flags = 0;
        const auto arrows = pain(f.draw(2)); REQUIRE(arrows.size() == 1); CHECK(arrows[0].color[0] == 1);
    }
    SECTION("missing view never invents a source direction") {
        DamageHud f; f.commit(damage(f.point({100,0,0})));
        const auto view = f.context.view; f.context.view.reset();
        CHECK(pain(f.draw()).empty());
        f.context.view = view; CHECK(pain(f.draw()).empty()); // Unavailable old hit is not delayed to a new pose.
    }
    SECTION("zero-origin positive damage clears an existing compass even without view") {
        DamageHud f; f.commit(damage(f.point({100,0,0}),1)); REQUIRE(pain(f.draw()).size() == 1);
        f.context.view.reset(); f.commit(damage({0,0,0},2)); CHECK(pain(f.draw()).empty());
    }
    SECTION("invalid view never submits nonfinite primitives") {
        DamageHud f; f.commit(damage(f.point({100,0,0})));
        f.context.view->forward[0] = std::numeric_limits<double>::quiet_NaN();
        CHECK(pain(f.draw()).empty());
    }
}

TEST_CASE("Damage compass uses source cursor ordering and clears at life HUD and generation boundaries",
    "[hud-damage][hud][no-stock]") {
    SECTION("same record independent messages and duplicate cursors") {
        DamageHud f;
        auto first = damage(f.point({100,0,0}),10);
        auto second = damage(f.point({0,-100,0}),10); second.source.start_bit_offset = 112; second.source.end_bit_offset = 208;
        f.presentation.committed(f.record({first,second}));
        const auto arrows = pain(f.draw()); REQUIRE(arrows.size() == 2);
        CHECK(frame(arrows,0).color[0] == 1); CHECK(frame(arrows,1).color[0] == 1);
        second.damage_origin = {0,0,0}; f.commit(second);
        CHECK(pain(f.draw()).size() == 2); // Same source cannot erase already committed direction.
    }
    SECTION("reset floor rejects damage preceding reset within the same record") {
        DamageHud f;
        f.state.weapon_hud.hud_reset_source = source(10,112,120);
        auto old = damage(f.point({100,0,0}),10);
        auto fresh = damage(f.point({0,-100,0}),10); fresh.source = source(10,120,216);
        f.presentation.committed(f.record({old,fresh}));
        const auto arrows = pain(f.draw()); REQUIRE(arrows.size() == 1); CHECK(arrows[0].frame == 1);
    }
    SECTION("death and respawn cannot revive old damage") {
        DamageHud f; auto old = damage(f.point({100,0,0}),10); f.commit(old);
        REQUIRE(pain(f.draw()).size() == 1);
        f.state.lifecycle.state = client::LocalPlayerLifeState::dead;
        f.state.lifecycle.last_death_source = source(11); ++f.state.lifecycle.deaths;
        CHECK(pain(f.draw(.01)).empty());
        f.state.lifecycle.state = client::LocalPlayerLifeState::alive; ++f.state.lifecycle.life_epoch;
        f.commit(old); CHECK(pain(f.draw(.02)).empty());
        f.commit(damage(f.point({0,-100,0}),12));
        const auto arrows = pain(f.draw(.03)); REQUIRE(arrows.size() == 1); CHECK(arrows[0].frame == 1);
    }
    SECTION("generation replacement and explicit module reset clear active arrows") {
        DamageHud f; f.commit(damage(f.point({100,0,0})));
        REQUIRE(pain(f.draw()).size() == 1);
        ++f.state.generation; CHECK(pain(f.draw(.1)).empty());
        f.presentation.reset(); f.presentation.bind(damage_resources());
        CHECK(pain(f.draw(.2)).empty());
        f.commit(damage(f.point({0,100,0}),1));
        const auto arrows = pain(f.draw(.3)); REQUIRE(arrows.size() == 1); CHECK(arrows[0].frame == 3);
    }
}

TEST_CASE("Damage compass burst queue retains at most the latest sixteen presentation origins",
    "[hud-damage][hud][no-stock]") {
    DamageHud f;
    std::vector<client::RuntimeLifeEvent> burst;
    burst.push_back(damage(f.point({100,0,0}),1));
    for (std::size_t ordinal = 2; ordinal <= 17; ++ordinal)
        burst.push_back(damage(f.point({0,0,100}),ordinal));
    f.presentation.committed(f.record(std::move(burst)));
    CHECK(pain(f.draw()).empty()); // Old front origin was evicted; vertical hits cannot invent arrows.
    f.commit(damage(f.point({0,-100,0}),18));
    const auto fresh = pain(f.draw()); REQUIRE(fresh.size() == 1); CHECK(fresh[0].frame == 1);
}

TEST_CASE("Damage compass missing original sprite roles never creates synthetic replacement arrows",
    "[hud-damage][hud][no-stock]") {
    for (const bool missing : {false,true}) {
        CAPTURE(missing);
        DamageHud f; auto resources = damage_resources();
        for (auto& descriptor : resources.descriptors)
            for (auto& entry : descriptor.entries)
                if (entry.role == "pain_front") {
                    if (missing) entry.status = api::HudResourceStatus::missing_file;
                    else entry.source.width = entry.frame_width + 1U;
                }
        f.presentation.bind(resources); f.commit(damage(f.point({100,0,0})));
        const auto hud = f.draw(); CHECK(pain(hud).empty()); CHECK(hud.draw.rectangles.empty());
    }
}

TEST_CASE("Strict committed server Damage activates presentation without changing authoritative counters",
    "[hud-damage][hud][user-message][no-stock]") {
    DamageHud f;
    // Armor-only source (110,20,30), signed one-eighth-unit wire coordinates.
    const std::array<std::byte,12> body{std::byte{5},std::byte{0},std::byte{0},std::byte{0},
        std::byte{0},std::byte{0},std::byte{0x70},std::byte{0x03},std::byte{0xa0},std::byte{0},
        std::byte{0xf0},std::byte{0}};
    api::GameMessageView message{api::GameMessageKind::user_message,"Damage",body,source(10),64};
    auto prior = f.record({});
    const auto before_hash = client::runtime_observation_canonical_hash(f.state);
    auto staged = hf::stage_record({&f.state,f.state,{&message,1},1,8,{},0.},&prior);
    REQUIRE(staged); REQUIRE(staged.state->life_events.size() == 1);
    CHECK(staged.state->life_events[0].armor_saved == 5);
    CHECK(staged.state->life_events[0].damage_taken == 0);
    CHECK(staged.state->life_events[0].damage_origin == Vector{110,20,30});
    CHECK(pain(f.draw()).empty()); // Merely staging a record is presentation-inert.
    f.presentation.committed(*staged.state);
    const auto arrows = pain(f.draw()); REQUIRE(arrows.size() == 1); CHECK(arrows[0].frame == 0);
    CHECK(f.state.weapon_hud.health == 100); CHECK(f.state.receiving_client->health == 100);
    CHECK(client::runtime_observation_canonical_hash(f.state) == before_hash);
    for (std::size_t count = 0; count < body.size(); ++count) {
        message.body = std::span{body}.first(count);
        CHECK_FALSE(hf::stage_record({&f.state,f.state,{&message,1},1,8,{},0.},&prior));
    }
    CHECK(pain(f.draw()).size() == 1); // Rejected bodies cannot mutate already committed HUD state.
}
