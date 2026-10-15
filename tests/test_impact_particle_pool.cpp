#include <hlclient/renderer/transient_visuals.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {
namespace renderer=hlclient::renderer;
namespace api=hlclient::game_api;
using Catch::Approx;

api::LocalWeaponActionIdentity particle_action(unsigned sequence=1U) {
    api::LocalWeaponActionIdentity action;
    action.generation=1U;action.command_sequence=sequence;action.started_at_seconds=1.0;
    return action;
}
api::ImpactParticleProfile luminous_profile() {
    // Project-owned numeric fixture; no concrete game or installed asset dependency.
    api::ImpactParticleProfile p{16U,.28F,42,78,180,.60F,{1,.78F,.35F,.85F}};
    p.spread_scale=1.2F;p.emissive=true;p.contact_glow={2,64,.55F};p.contact_glow_seconds=.06F;
    return p;
}
}

TEST_CASE("Core-only neutral impact particles retain bounded causal numeric effects without game linkage",
          "[e11][core-api][particles]") {
    namespace renderer=hlclient::renderer;
    namespace api=hlclient::game_api;
    const api::ImpactParticleProfile profile{3U,0.2F,10.0F,20.0F,100.0F,0.4F,{0.5F,0.6F,0.7F,0.9F}};
    CHECK(profile.spread_scale==1.0F);CHECK_FALSE(profile.emissive);
    CHECK(profile.contact_glow.radius_units==0);CHECK(profile.contact_glow.light_radius_units==0);
    CHECK(profile.contact_glow.light_intensity==0);CHECK(profile.contact_glow_seconds==0);
    api::LocalWeaponActionIdentity action;action.generation=1U;action.command_sequence=7U;
    action.started_at_seconds=1.0;
    renderer::ImpactParticlePool first,second;
    REQUIRE(first.spawn(action,{1,2,3},{0,0,1},profile,1.0)==renderer::ParticleBurstStatus::spawned);
    REQUIRE(second.spawn(action,{1,2,3},{0,0,1},profile,1.0)==renderer::ParticleBurstStatus::spawned);
    first.update(1.1);second.update(1.02);second.update(1.05);second.update(1.1);
    REQUIRE(first.particles().size()==3U);
    CHECK(std::ranges::equal(first.particles(),second.particles()));
    CHECK(first.spawn(action,{1,2,3},{0,0,1},profile,1.0)==renderer::ParticleBurstStatus::duplicate);
    for(const auto& particle:first.particles()) {
        CHECK(std::isfinite(particle.center.x));CHECK(std::isfinite(particle.center.z));
        CHECK(particle.center.z>3.0F);CHECK(particle.color[3]>0.0F);
        CHECK_FALSE(particle.emissive);
    }
    renderer::RenderScene no_glow;first.append_glows(no_glow);
    CHECK(no_glow.world_flash_count==0);CHECK(no_glow.world_effect_light_count==0);
    first.reset();CHECK(first.particles().empty());
    ++action.generation;
    REQUIRE(first.spawn(action,{1,2,3},{0,0,1},profile,1.0)==renderer::ParticleBurstStatus::spawned);
    first.update(1.3);CHECK(first.particles().empty());
}

TEST_CASE("Core-only contact glow is one bounded fading burst independent of particle emission",
          "[e15][e11][core-api][particles][glow]") {
    const auto profile=luminous_profile();const auto action=particle_action();
    renderer::ImpactParticlePool pool;
    REQUIRE(pool.spawn(action,{1,2,3},{0,0,1},profile,1.0)==renderer::ParticleBurstStatus::spawned);
    pool.update(1.0);REQUIRE(pool.particles().size()==16U);
    for(const auto& p:pool.particles()) CHECK(p.emissive);
    const renderer::RenderPointLight primary{{40,50,60},96,.8F,{1,.6F,.3F}};
    renderer::RenderScene scene;scene.transient_world_light=primary;
    pool.append_glows(scene);REQUIRE(scene.world_flash_count==1U);REQUIRE(scene.world_effect_light_count==1U);
    CHECK(scene.world_flashes[0].center.z==Approx(3.125));
    CHECK(scene.world_effect_lights[0].center.z==Approx(3.125));
    CHECK(scene.world_flashes[0].radius_units==2);CHECK(scene.world_effect_lights[0].radius_units==64);
    CHECK(scene.world_effect_lights[0].intensity==Approx(.55));
    REQUIRE(scene.transient_world_light);CHECK(scene.transient_world_light->center.x==40);
    CHECK(scene.transient_world_light->radius_units==96);CHECK(scene.transient_world_light->intensity==Approx(.8));
    CHECK(pool.spawn(action,{1,2,3},{0,0,1},profile,1.0)==renderer::ParticleBurstStatus::duplicate);
    CHECK(pool.statistics().spawned==1);CHECK(pool.statistics().duplicate==1);
    scene.world_flash_count=scene.world_effect_light_count=0;
    pool.update(1.03);pool.append_glows(scene);
    REQUIRE(scene.world_flash_count==1U);REQUIRE(scene.world_effect_light_count==1U);
    CHECK(scene.world_flashes[0].color[3]==Approx(profile.color[3]*.5F));
    CHECK(scene.world_effect_lights[0].intensity==Approx(profile.contact_glow.light_intensity*.5F));
    const auto halfway=std::vector<renderer::RenderWorldParticle>{pool.particles().begin(),pool.particles().end()};
    scene.world_flash_count=scene.world_effect_light_count=0;
    pool.update(1.01);pool.append_glows(scene); // rendering cannot rewind an existing flash
    CHECK(std::ranges::equal(pool.particles(),halfway));
    REQUIRE(scene.world_effect_light_count==1U);
    CHECK(scene.world_effect_lights[0].intensity==Approx(profile.contact_glow.light_intensity*.5F));
    scene.world_flash_count=scene.world_effect_light_count=0;
    pool.update(1.0+profile.contact_glow_seconds);pool.append_glows(scene);
    CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
    CHECK(pool.particles().size()==16U); // short contact flash must not end longer spark trajectories
    pool.update(1.0+profile.lifetime_seconds);CHECK(pool.particles().empty());
    pool.reset();pool.update(1.0);pool.append_glows(scene);
    CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
    REQUIRE(pool.spawn(action,{1,2,3},{0,0,1},profile,1.0)==renderer::ParticleBurstStatus::spawned);
    pool.update(1.0);pool.append_glows(scene);
    REQUIRE(scene.world_effect_light_count==1U);CHECK(scene.world_effect_lights[0].intensity==Approx(.55));
}

TEST_CASE("Core-only wider particle spread preserves causal speed and absolute time determinism",
          "[e15][core-api][particles][glow]") {
    auto narrow=luminous_profile();narrow.minimum_speed_units=narrow.maximum_speed_units=50;
    narrow.gravity_units=0;narrow.spread_scale=1.0F;
    auto wide=narrow;wide.spread_scale=1.2F;
    const auto action=particle_action();renderer::ImpactParticlePool baseline,first,second;
    REQUIRE(baseline.spawn(action,{1,2,3},{0,0,1},narrow,1.0)==renderer::ParticleBurstStatus::spawned);
    REQUIRE(first.spawn(action,{1,2,3},{0,0,1},wide,1.0)==renderer::ParticleBurstStatus::spawned);
    REQUIRE(second.spawn(action,{1,2,3},{0,0,1},wide,1.0)==renderer::ParticleBurstStatus::spawned);
    baseline.update(1.05);first.update(1.05);second.update(1.01);second.update(1.03);second.update(1.05);
    REQUIRE(first.particles().size()==16U);REQUIRE(baseline.particles().size()==16U);
    CHECK(std::ranges::equal(first.particles(),second.particles()));
    double baseline_radius=0,wide_radius=0;
    for(std::size_t i=0;i<first.particles().size();++i) {
        const auto& p=first.particles()[i];const auto& b=baseline.particles()[i];
        const auto radial=std::hypot(p.center.x-1.0F,p.center.y-2.0F);
        const auto previous=std::hypot(b.center.x-1.0F,b.center.y-2.0F);
        CHECK(radial>=previous);baseline_radius+=previous;wide_radius+=radial;
        CHECK(p.center.z>3.125F);CHECK(std::isfinite(p.center.z));
        const auto displacement=std::hypot(p.center.x-1.0F,p.center.y-2.0F,p.center.z-3.125F);
        CHECK(displacement==Approx(2.5).margin(.0001)); // speed remains 50, not increased by spread
    }
    CHECK(wide_radius>baseline_radius);
    first.update(1.05);CHECK(std::ranges::equal(first.particles(),second.particles()));
}

TEST_CASE("Core-only dense bursts saturate fixed output and glow capacities without displacing primary light",
          "[e15][core-api][particles][bounds][glow]") {
    renderer::ImpactParticlePool pool;const auto profile=luminous_profile();
    for(unsigned sequence=1;sequence<=40;++sequence)
        REQUIRE(pool.spawn(particle_action(sequence),{static_cast<float>(sequence),2,3},{0,0,1},profile,1.0)==renderer::ParticleBurstStatus::spawned);
    CHECK(pool.statistics().spawned==40U);CHECK(pool.statistics().evicted==8U);
    pool.update(1.0);REQUIRE(pool.particles().size()==256U);CHECK(pool.statistics().output_limited>0);
    const std::vector<renderer::RenderWorldParticle> before{pool.particles().begin(),pool.particles().end()};
    for(const auto& p:before) {CHECK(p.center.x>=9);CHECK(p.center.x<=40);CHECK(p.emissive);}
    pool.update(1.0);CHECK(std::ranges::equal(pool.particles(),before));
    renderer::RenderScene scene;scene.transient_world_light=renderer::RenderPointLight{{9,8,7},96,.8F,{1,.6F,.3F}};
    pool.append_glows(scene);CHECK(scene.world_flash_count==32U);CHECK(scene.world_effect_light_count==8U);
    const auto primary=*scene.transient_world_light;
    pool.append_glows(scene); // full frame arrays stay bounded, even if caller accidentally appends twice
    CHECK(scene.world_flash_count==32U);CHECK(scene.world_effect_light_count==8U);
    REQUIRE(scene.transient_world_light);CHECK(scene.transient_world_light->center.x==primary.center.x);
    CHECK(scene.transient_world_light->intensity==primary.intensity);
    pool.reset();CHECK(pool.particles().empty());CHECK(pool.statistics().output_limited==0);
    renderer::RenderScene empty;pool.append_glows(empty);CHECK(empty.world_flash_count==0);CHECK(empty.world_effect_light_count==0);
}

TEST_CASE("Core-only particle glow and spread reject invalid profiles before allocating effects",
          "[e15][core-api][particles][bounds][glow]") {
    const auto valid=luminous_profile();const auto action=particle_action();
    std::vector<api::ImpactParticleProfile> invalid;
    const auto append=[&](const auto& change) {auto p=valid;change(p);invalid.push_back(p);};
    append([](auto& p){p.count=17;});
    append([](auto& p){p.spread_scale=-.01F;});append([](auto& p){p.spread_scale=2.01F;});
    append([](auto& p){p.spread_scale=std::numeric_limits<float>::quiet_NaN();});
    append([](auto& p){p.contact_glow.radius_units=-1;});append([](auto& p){p.contact_glow.radius_units=17;});
    append([](auto& p){p.contact_glow.light_radius_units=257;});
    append([](auto& p){p.contact_glow.light_intensity=2.01F;});
    append([](auto& p){p.contact_glow.light_radius_units=0;});
    append([](auto& p){p.contact_glow.light_intensity=0;});
    append([](auto& p){p.contact_glow.light_intensity=std::numeric_limits<float>::quiet_NaN();});
    append([](auto& p){p.contact_glow_seconds=-.01F;});append([](auto& p){p.contact_glow_seconds=0;});
    append([](auto& p){p.contact_glow_seconds=p.lifetime_seconds+.01F;});
    append([](auto& p){p.contact_glow_seconds=std::numeric_limits<float>::quiet_NaN();});
    for(const auto& p:invalid) {
        renderer::ImpactParticlePool pool;
        CHECK(pool.spawn(action,{1,2,3},{0,0,1},p,1.0)==renderer::ParticleBurstStatus::invalid);
        CHECK(pool.statistics().invalid==1U);CHECK(pool.statistics().spawned==0U);
        pool.update(1.0);CHECK(pool.particles().empty());
        renderer::RenderScene scene;pool.append_glows(scene);
        CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
    }
    renderer::ImpactParticlePool pool;
    REQUIRE(pool.spawn(action,{1,2,3},{0,0,1},valid,1.0)==renderer::ParticleBurstStatus::spawned);
    auto stale=action;++stale.generation;
    CHECK(pool.spawn(stale,{1,2,3},{0,0,1},valid,1.0)==renderer::ParticleBurstStatus::stale_generation);
    auto late=action;late.command_sequence=2;
    CHECK(pool.spawn(late,{1,2,3},{0,0,1},valid,1.3)==renderer::ParticleBurstStatus::expired);
    pool.update(1.0);REQUIRE(pool.particles().size()==16U);
    renderer::RenderScene scene;pool.append_glows(scene);CHECK(scene.world_flash_count==1);CHECK(scene.world_effect_light_count==1);
}
