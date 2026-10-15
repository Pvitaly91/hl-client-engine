#include "snark_sound_pop_test_fixture.hpp"
#include <hlclient/app/temporary_effect_presentation.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>

namespace {
namespace api=hlclient::game_api;
namespace f=hlclient::tests::snark_sound_pop_fixture;
struct Ready {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    Ready(bool sprites=true) {host.reset({1,1,1});host.set_movement_audio_time_origin(0);f::bind(host,54,sprites);f::entities(host);}
};
}
TEST_CASE("Snark exact committed sound creates a silent cosmetic pop at server position once",
    "[e27][snark-world][sound-pop][no-stock]") {
    Ready ready;auto& host=ready.host;auto event=f::pop();event.origin={30,-12,8};
    host.committed_sound(event);host.committed_sound(event);
    const auto batch=host.drain_temporary_effects(0);REQUIRE(batch.count==1);
    const auto& effect=batch.effects[0];CHECK(effect.origin.x==30);CHECK(effect.origin.y==-12);CHECK(effect.origin.z==8);
    CHECK(effect.identity.source==api::TemporaryEffectIdentity::Source::server_sound);
    CHECK(effect.identity.record==2);CHECK(effect.record_ordinal==2);CHECK(effect.diagnostic_tag==2);
    REQUIRE(effect.sprite);REQUIRE(effect.sprite_burst);CHECK_FALSE(effect.secondary_sprite);
    CHECK(effect.sprite->model_slot==321);CHECK(effect.sprite_burst->sprite.model_slot==322);
    CHECK(effect.sprite->tint[0]==160.F/255.F);CHECK(effect.sprite->tint[1]==130.F/255.F);
    CHECK(effect.sprite->tint[2]==20.F/255.F);CHECK(effect.sprite_burst->sprite.tint==effect.sprite->tint);
    CHECK(effect.sprite_burst->count==16);CHECK(effect.sprite_burst->sprite.animation==api::TemporarySpriteAnimation::fixed);
    CHECK_FALSE(effect.sound);CHECK_FALSE(effect.light);CHECK_FALSE(effect.particles);
    CHECK(batch.statistics.pop_sound_accepted==1);CHECK(batch.statistics.pop_duplicate==1);
    CHECK(batch.statistics.blood_received==0);CHECK(batch.statistics.explosion==0);
    auto owner_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& owner=*owner_storage;owner.reset(1);owner.consume(batch,0);owner.consume(batch,0);
    hlclient::renderer::RenderScene scene;owner.present(scene,.2);CHECK(owner.sprites().size()==17);
    host.cancel_uncommitted(); // Focus/weapon cancellation does not revoke a server cosmetic.
    owner.present(scene,.4);CHECK(owner.sprites().size()==17);
    CHECK(host.drain_audio().count==0);CHECK(host.drain_remote_audio().count==0);
    CHECK(host.drain_temporary_effects(.1).count==0);
    event.record=event.record_ordinal=3;host.committed_sound(event);
    CHECK(host.drain_temporary_effects(0).count==0); // Same observed life, different record.
    owner.present(scene,1.6);CHECK(owner.sprites().empty());
}
TEST_CASE("Snark sound pop requires exact mapping channel flags source and fresh committed generation",
    "[e27][snark-world][sound-pop][no-stock]") {
    for(unsigned invalid=0;invalid<13;++invalid) {
        Ready ready;auto event=f::pop();
        switch(invalid) {
        case 0:event.sample_slot=53;break; // Bite is not pop.
        case 1:event.channel=1;break;
        case 2:event.flags=32;break; // Stop.
        case 3:event.flags=64;break; // Change, not start.
        case 4:event.flags=16;break; // Sentence.
        case 5:event.entity=0;break;
        case 6:event.entity=1;break;
        case 7:event.entity=41;break;
        case 8:event.generation=2;break;
        case 9:event.record=0;break;
        case 10:event.received_at_seconds=-1;break;
        case 11:event.origin.z=std::numeric_limits<float>::quiet_NaN();break;
        case 12:event.record_ordinal=0;break;
        }
        ready.host.committed_sound(event);CHECK(ready.host.drain_temporary_effects(0).count==0);
    }
    for(const auto name:{"squeek/sqk_die1.wav","squeek/sqk_deploy1.wav","common/bodysplat.wav","other/sqk_blast1.wav"}) {
        api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        f::bind(host,55);host.bind_effect_resource({api::EffectResourceBinding::Kind::sound,54,name});
        f::entities(host);host.committed_sound(f::pop());CHECK(host.drain_temporary_effects(0).count==0);
    }
    Ready missing(false);missing.host.committed_sound(f::pop());auto batch=missing.host.drain_temporary_effects(0);
    CHECK(batch.count==0);CHECK(batch.statistics.pop_resource_missing==1);
    f::bind(missing.host);missing.host.committed_sound(f::pop(3));CHECK(missing.host.drain_temporary_effects(0).count==0);
}
TEST_CASE("Snark source removal same commit reuse hidden entities and reset have bounded sound ownership",
    "[e27][snark-world][sound-pop][no-stock]") {
    for(const bool removed:{false,true}) {
        Ready ready;
        if(removed)ready.host.remote_entities({},8,2);else f::entities(ready.host,2,40,0);
        CHECK(ready.host.drain_temporary_effects(15).count==0); // Removal and time alone do nothing.
        ready.host.committed_sound(f::pop());CHECK(ready.host.drain_temporary_effects(0).count==1);
        ready.host.committed_sound(f::pop(3));CHECK(ready.host.drain_temporary_effects(0).count==0);
        f::entities(ready.host,4);ready.host.committed_sound(f::pop(5));
        CHECK(ready.host.drain_temporary_effects(0).count==1); // Reused slot, new observed life.
    }
    for(const unsigned model:{0U,312U,7U}) {
        Ready ready;f::entities(ready.host,2,40,model);ready.host.committed_sound(f::pop(3));
        CHECK(ready.host.drain_temporary_effects(0).count==0); // Later removed / nest / player model.
    }
    Ready hidden;auto entity=f::creature();entity.effects=128;entity.render_amount=0;
    hidden.host.remote_entities(std::span{&entity,1},8,2);hidden.host.committed_sound(f::pop(3));
    CHECK(hidden.host.drain_temporary_effects(0).count==1); // No scene/frustum/audio device dependency.
    hidden.host.reset({2,2,1});hidden.host.committed_sound(f::pop(4));
    CHECK(hidden.host.drain_temporary_effects(0).count==0);
    hidden.host.teardown();hidden.host.committed_sound(f::pop(5));CHECK(hidden.host.drain_temporary_effects(0).count==0);
    Ready stale;stale.host.committed_sound(f::pop());auto batch=stale.host.drain_temporary_effects(.3);
    CHECK(batch.count==0);CHECK(batch.statistics.stale==1); // Never replay late cosmetic.
}
TEST_CASE("Snark sound and large yellow TE correlate one to one in either delivery order",
    "[e27][snark-world][sound-pop][no-stock]") {
    for(const bool sound_first:{false,true})for(const bool drain_between:{false,true}) {
        Ready ready;std::size_t count=0;
        if(sound_first)ready.host.committed_sound(f::pop());else ready.host.committed_temporary_effect(f::blood());
        if(drain_between)count+=ready.host.drain_temporary_effects(0).count;
        if(sound_first)ready.host.committed_temporary_effect(f::blood(3,.1));else ready.host.committed_sound(f::pop(2,40,.1));
        auto batch=ready.host.drain_temporary_effects(.1);count+=batch.count;
        CHECK(count==1);CHECK(batch.statistics.pop_te_correlated==1);
        // Another creature at exactly the same origin still gets its own pop.
        const std::array entities{f::creature(),f::creature(41)};ready.host.remote_entities(entities,8,4);
        ready.host.committed_sound(f::pop(5,41,.1));CHECK(ready.host.drain_temporary_effects(.1).count==1);
        auto next=f::blood(6,.1);ready.host.committed_temporary_effect(next);
        CHECK(ready.host.drain_temporary_effects(.1).count==0);
    }
    for(unsigned independent=0;independent<6;++independent) {
        Ready ready;ready.host.committed_sound(f::pop());REQUIRE(ready.host.drain_temporary_effects(0).count==1);
        auto blood=f::blood();
        if(independent==0)blood.palette_color=247;
        if(independent==1)blood.blood_scale=3;
        if(independent==2)blood.origin.x=3;
        if(independent==3)blood.received_at_seconds=.3;
        if(independent==4)blood.blood_sprite_indices={323,324};
        if(independent==5)blood.blood_scale=12;
        ready.host.committed_temporary_effect(blood);CHECK(ready.host.drain_temporary_effects(blood.received_at_seconds).count==1);
    }
}
TEST_CASE("Snark normalized real TE and validated sound fallback share spray drop geometry and verified palette",
    "[e27][snark-world][sound-pop][bloodsprite][no-stock]") {
    Ready real,inferred;auto sound=f::pop(2,40,0);auto blood=f::blood(2,0);
    blood.message_bit_offset=sound.message_bit_offset;
    real.host.committed_temporary_effect(blood);inferred.host.committed_sound(sound);
    const auto te=real.host.drain_temporary_effects(0),fallback=inferred.host.drain_temporary_effects(0);
    REQUIRE(te.count==1);REQUIRE(fallback.count==1);
    const auto& first=te.effects[0];const auto& second=fallback.effects[0];
    CHECK(first.identity.source==api::TemporaryEffectIdentity::Source::server_record);
    CHECK(second.identity.source==api::TemporaryEffectIdentity::Source::server_sound);
    CHECK(te.statistics.blood_received==1);CHECK(fallback.statistics.blood_received==0);
    REQUIRE(first.sprite);REQUIRE(second.sprite);REQUIRE(first.sprite_burst);REQUIRE(second.sprite_burst);
    CHECK_FALSE(first.secondary_sprite);CHECK_FALSE(second.secondary_sprite);
    CHECK(first.sprite->model_slot==second.sprite->model_slot);CHECK(first.sprite->scale==second.sprite->scale);
    CHECK(first.sprite->tint==second.sprite->tint);CHECK(first.sprite->frames_per_second==second.sprite->frames_per_second);
    CHECK(first.sprite_burst->sprite.model_slot==second.sprite_burst->sprite.model_slot);
    CHECK(first.sprite_burst->sprite.scale==second.sprite_burst->sprite.scale);
    CHECK(first.sprite_burst->sprite.tint==second.sprite_burst->sprite.tint);
    CHECK(first.sprite_burst->count==second.sprite_burst->count);CHECK(first.sprite_burst->seed==second.sprite_burst->seed);
    auto owner_a=std::make_unique<hlclient::app::TemporaryEffectPresentation>();
    auto owner_b=std::make_unique<hlclient::app::TemporaryEffectPresentation>();
    owner_a->reset(1);owner_b->reset(1);owner_a->consume(te,0);owner_b->consume(fallback,0);
    hlclient::renderer::RenderScene scene;owner_a->present(scene,.2);owner_b->present(scene,.2);
    REQUIRE(owner_a->sprites().size()==17);REQUIRE(owner_b->sprites().size()==17);
    for(std::size_t i=0;i<owner_a->sprites().size();++i) {
        const auto& a=owner_a->sprites()[i];const auto& b=owner_b->sprites()[i];
        CHECK(a.sprite.model_slot==b.sprite.model_slot);CHECK(a.sprite.scale==b.sprite.scale);
        CHECK(a.sprite.tint==b.sprite.tint);CHECK(a.sprite.frame_choice==b.sprite.frame_choice);
        CHECK(a.sprite.maximum_lifetime_seconds==b.sprite.maximum_lifetime_seconds);
        CHECK(a.velocity.x==b.velocity.x);CHECK(a.velocity.y==b.velocity.y);CHECK(a.velocity.z==b.velocity.z);
        CHECK(a.origin.x==b.origin.x);CHECK(a.origin.y==b.origin.y);CHECK(a.origin.z==b.origin.z);
        CHECK(a.age_seconds==b.age_seconds);
    }
}
TEST_CASE("Snark sound fallback respects disabled blood policy and typed missing palette without replay",
    "[e27][snark-world][sound-pop][bloodsprite][policy][no-stock]") {
    Ready disabled;disabled.host.configure_effect_policy({false});disabled.host.committed_sound(f::pop());
    auto batch=disabled.host.drain_temporary_effects(0);CHECK(batch.count==0);
    CHECK(batch.statistics.pop_policy_suppressed==1);CHECK(batch.statistics.pop_sound_accepted==0);
    // A real received TE retains its server authority even when startup policy
    // told the optional sound-only fallback to abstain.
    disabled.host.committed_temporary_effect(f::blood());batch=disabled.host.drain_temporary_effects(0);
    REQUIRE(batch.count==1);CHECK(batch.statistics.blood_sprites==1);
    Ready absent;absent.host.bind_effect_palette({});absent.host.committed_sound(f::pop());
    batch=absent.host.drain_temporary_effects(0);CHECK(batch.count==0);
    CHECK(batch.statistics.blood_palette_unavailable==1);CHECK(batch.statistics.pop_sound_accepted==0);
    absent.host.bind_effect_palette(hlclient::tests::effect_palette_fixture::metadata());
    absent.host.committed_sound(f::pop(3));CHECK(absent.host.drain_temporary_effects(0).count==0);
    absent.host.remote_entities({},8,4);f::entities(absent.host,5);
    absent.host.committed_sound(f::pop(6));CHECK(absent.host.drain_temporary_effects(0).count==1);
    Ready no_te_palette;no_te_palette.host.bind_effect_palette({});no_te_palette.host.committed_temporary_effect(f::blood());
    batch=no_te_palette.host.drain_temporary_effects(0);CHECK(batch.count==0);
    CHECK(batch.statistics.blood_palette_unavailable==1);CHECK(batch.statistics.blood_received==1);
    CHECK(batch.statistics.blood_sprites==0);
}
TEST_CASE("Snark sound pop outbox and observed creature receipts are bounded",
    "[e27][snark-world][sound-pop][no-stock]") {
    Ready ready;std::array<hlclient::client::RuntimePacketEntityObservation,140> entities;
    for(unsigned i=0;i<entities.size();++i)entities[i]=f::creature(40+i);
    ready.host.remote_entities(entities,8,2);
    for(unsigned i=0;i<entities.size();++i)ready.host.committed_sound(f::pop(3+i,40+i));
    const auto batch=ready.host.drain_temporary_effects(0);CHECK(batch.count==32);
    CHECK(batch.statistics.pop_sound_accepted==32);CHECK(batch.statistics.pop_capacity>0);
    CHECK(batch.statistics.pop_entity_rejected>0);
    ready.host.reset({2,2,1});CHECK(ready.host.drain_temporary_effects(0).count==0);
}
