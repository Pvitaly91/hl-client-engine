#include <hlclient/assets/wav_importer.hpp>
#include <hlclient/audio/output.hpp>
#include <hlclient/goldsrc/server_audio.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <thread>
#include "local_resource_readiness_test_fixture.hpp"
namespace {
namespace a=hlclient::audio;
namespace g=hlclient::goldsrc;
namespace assets=hlclient::assets;
using namespace std::chrono_literals;
std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> b; for(auto v:values) b.push_back(static_cast<std::byte>(v)); return b;
}
void le(std::vector<std::byte>& b,std::uint32_t v,int n=4) {for(int i=0;i<n;++i) b.push_back(static_cast<std::byte>((v>>(i*8))&255));}
void tag(std::vector<std::byte>& b,std::string_view s) {for(char c:s) b.push_back(static_cast<std::byte>(c));}
void chunk(std::vector<std::byte>& b,std::string_view s,std::vector<std::byte> data) {tag(b,s); le(b,static_cast<std::uint32_t>(data.size())); b.insert(b.end(),data.begin(),data.end()); if(data.size()%2) b.push_back(std::byte{});}
std::vector<std::byte> wav(bool wide=false,bool stereo=false,bool looping=false) {
    std::vector<std::byte> body; tag(body,"WAVE");
    chunk(body,"JUNK",bytes({42})); // independently non-44 byte grammar
    auto fmt=bytes({1,0,stereo?2U:1U,0}); le(fmt,48000); le(fmt,48000U*(wide?2U:1U)*(stereo?2U:1U));
    le(fmt,(wide?2U:1U)*(stereo?2U:1U),2); le(fmt,wide?16:8,2); chunk(body,"fmt ",fmt);
    if(looping) {
        std::vector<std::byte> cue; le(cue,1); le(cue,7); le(cue,0); tag(cue,"data"); le(cue,0); le(cue,0); le(cue,1); chunk(body,"cue ",cue);
        std::vector<std::byte> list; tag(list,"adtl");
        std::vector<std::byte> ltxt; le(ltxt,7); le(ltxt,2); tag(ltxt,"mark"); chunk(list,"ltxt",ltxt); chunk(body,"LIST",list);
    }
    chunk(body,"data",wide?bytes({0,128,0,0,0,64,255,127}):bytes({0,128,192,255}));
    std::vector<std::byte> file; tag(file,"RIFF"); le(file,static_cast<std::uint32_t>(body.size())); file.insert(file.end(),body.begin(),body.end()); return file;
}
assets::AudioAssetResult decode(std::vector<std::byte> b) {
    auto source=assets::AssetSource::create("sound/project.wav",std::move(b)); REQUIRE(source); return assets::WavImporter{}.import(*source.source);
}
std::shared_ptr<const assets::AudioAsset> sample(bool loop=true) {
    auto d=decode(wav(false,false,loop)); REQUIRE(d); return std::make_shared<const assets::AudioAsset>(std::move(d).value());
}
g::OwnedServicePayload payload(std::vector<std::byte> b) {
    g::OwnedServicePayload p; p.bytes=std::move(b); p.source_sequence=91; p.source_acknowledgement=73;
    p.source_reliable=true; p.reassembled=true; p.decompressed=true; p.direction=g::NetchanDirection::server_to_client; return p;
}
g::RuntimeControlSingleDecodeResult control(const g::OwnedServicePayload& p) {
    return g::RuntimeControlDecoder{}.decode_one({p,*g::StockRuntimeSourceCursor::create(0,0,p.bytes.size()),1,1,{}},0);
}
g::CommittedSound event(std::size_t ordinal=1,std::uint8_t channel=3,std::uint16_t flags=0) {
    g::CommittedSound e; e.generation=1; e.record=100+ordinal; e.ordinal=ordinal; e.cursor=40; e.sound.channel=channel;
    e.sound.entity_reference=19; e.sound.sound_reference=300; e.sound.field_mask=flags; e.opcode=g::RuntimeControlOpcode::svc_sound; return e;
}
class FixtureAssets : public g::SoundAssets {
public:
    g::SoundAssetResult result{g::SoundAssetStatus::ready,sample()};
    std::uint16_t index{};
    g::SoundAssetResult request(std::uint16_t i) noexcept override {index=i; return result;}
};
// Independent project-owned encoding of the already-supported E1 contract.
// No inferred footsteps: changing snapshots without this message emits nothing.
std::vector<std::byte> remote_sound(unsigned entity,int y,unsigned flags=2,unsigned attenuation=64) {
    auto b=bytes({6}); std::size_t bit=8;
    const auto put=[&](unsigned v,unsigned count) {
        for(unsigned i=0;i<count;++i,++bit) {
            if(bit/8>=b.size()) b.push_back(std::byte{});
            if(v&(1U<<i)) b[bit/8]|=static_cast<std::byte>(1U<<(bit%8));
        }
    };
    put(flags,9); if(flags&1) put(255,8); if(flags&2) put(attenuation,8);
    put(3,3); put(entity,11); put(5,8); // independent sparse sound slot
    put(0,1); put(y!=0,1); put(0,1);
    if(y) {put(1,1);put(0,1);put(y<0,1);put(static_cast<unsigned>(std::abs(y)),12);}
    return b;
}
}

TEST_CASE("E9 encoded committed remote event crosses real module approved loader and isolated PCM", "[e9][audio][runtime-replay]") {
    namespace api=hlclient::game_api; namespace f=hlclient::tests::readiness_fixture;
    hlclient::tests::ScopedLocalResourceTestRoot root;
    root.write("valve","maps/test.bsp","project owned metadata");
    root.write("valve","sound/player/pl_step1.wav",wav());
    const auto resources=f::parse_resource_list({{2,"maps/test.bsp",1,0xffffffU,0},{0,"player/pl_step1.wav",5,0xffffffU,0}});
    auto env=std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>{f::make_environment(root)};
    auto inventory=f::build_inventory(resources,*env);
    auto manifest=f::build_manifest(resources,inventory,f::parse_server_info("maps/test.bsp"),*env); REQUIRE(manifest);
    g::ApprovedSoundAssets loader{{std::make_shared<const g::PrecacheManifestState>(std::move(*manifest.state)),env}};
    auto loaded=loader.request(5); const auto deadline=std::chrono::steady_clock::now()+2s;
    while(loaded.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
        std::this_thread::sleep_for(1ms); loaded=loader.request(5);
    }
    REQUIRE(loaded.status==g::SoundAssetStatus::ready); REQUIRE(loaded.asset);
    // reference/off select local generation, not server-event delivery.
    for(bool local_generation:{false,true}) for(int y:{-100,100,500}) for(bool rotate:{false,true}) {
        auto fixture=g::make_runtime_replay_fixture({}); REQUIRE(fixture);
        auto queue=std::make_shared<g::CommittedSoundQueue>();
        auto host=std::make_shared<api::GameClientHost>(hlclient::games::halflife::make_half_life_client_module());
        fixture.fixture->initialization.game_client=host;
        fixture.fixture->initialization.sound_events=queue;
        hlclient::client::ClientWorldState world;
        auto session=g::RuntimeReplaySession::initialize(fixture.fixture->initialization,world); REQUIRE(session);
        if(local_generation) {api::MovementAudioObservation silent; host->observe_movement_audio(silent,false);}
        auto record=fixture.fixture->records.front();
        const auto encoded=remote_sound(19,y);
        record.payload.bytes.insert(record.payload.bytes.end(),encoded.begin(),encoded.end());
        record.received_at=g::SoundTime{};
        auto malformed=record; malformed.payload.bytes.push_back(std::byte{255});
        CHECK_FALSE(session.session->apply_record(malformed));
        g::CommittedSound sound; CHECK_FALSE(queue->pop(sound));
        REQUIRE(session.session->apply_record(record)); REQUIRE(queue->pop(sound)); CHECK_FALSE(queue->pop(sound));
        CHECK(sound.sound.entity_reference==19); CHECK(sound.sound.channel==3); CHECK(sound.sound.origin[1]==y);
        CHECK_FALSE(session.session->apply_record(record)); CHECK_FALSE(queue->pop(sound));
        a::OfflineOutput output; g::ServerAudio server{output};
        const a::Listener listener{{},rotate ? a::Vec3{0,1,0} : a::Vec3{0,-1,0},.2F,false};
        server.present({},listener,{},7); // local identity is not fixed slot1
        server.consume(sound,{}); server.update(&loader,{});
        // A later render snapshot on the opposite side must not drag a one-shot.
        const std::array<g::SoundSource,1> opposite{{{19,{0,static_cast<float>(-y),0}}}};
        server.present(opposite,listener,{},7);
        std::array<float,8> pcm{}; output.mixer.render(pcm);
        REQUIRE(output.mixer.statistics().started==1); CHECK(server.statistics().started==1);
        const bool right=(y<0)!=rotate;
        CHECK(pcm[right?0:1]==0);
        CHECK(pcm[right?1:0]==Catch::Approx(-.4F*(1.0F-std::abs(y)/1000.0F)));
        CHECK(std::all_of(pcm.begin(),pcm.end(),[](float v){return std::isfinite(v);}));
        server.consume(sound,{}); server.update(&loader,{}); CHECK(output.mixer.statistics().started==1);
        // Snapshot/presentation updates alone never create new starts.
        server.present({},listener,{},7); CHECK(output.mixer.statistics().started==1);
    }
}

TEST_CASE("E9 channels origins slot boundary and typed failure lifecycle stay bounded", "[e9][audio]") {
    FixtureAssets loader; loader.result.asset=sample(false);
    a::OfflineOutput output; g::ServerAudio server{output};
    server.present({},{{},{0,-1,0},.2F,false},{},7);
    auto e=event(); e.sound.origin={0,100,0}; server.consume(e,{}); server.update(&loader,{});
    auto second=e; second.ordinal=2; second.sound.entity_reference=20; server.consume(second,{}); server.update(&loader,{});
    CHECK(output.mixer.active()==2);
    e.ordinal=3; server.consume(e,{}); server.update(&loader,{});
    CHECK(output.mixer.active()==2); CHECK(output.mixer.statistics().started==3); // legitimate replacement
    e.ordinal=4; e.sound.entity_reference=0; server.consume(e,{}); server.update(&loader,{}); CHECK(output.mixer.active()==3);
    e.ordinal=5; e.sound.origin[0]=std::numeric_limits<float>::quiet_NaN(); server.consume(e,{});
    CHECK(server.statistics().invalid_origin==1); CHECK(output.mixer.statistics().started==4);
    server.reset(2); CHECK(output.mixer.active()==0);
    server.consume(second,{}); CHECK(output.mixer.active()==0); // stale generation
    // An explicit committed slot update invalidates old looping attachment;
    // missing render entities alone do not mean disconnect.
    loader.result.asset=sample(); e=event();e.generation=2;e.sound.origin={0,100,0};
    server.consume(e,{});server.update(&loader,{});
    auto boundary=e;boundary.ordinal=2;boundary.opcode=g::RuntimeControlOpcode::svc_updateuserinfo;
    server.consume(boundary,{});
    const std::array<g::SoundSource,1> reused{{{19,{0,-100,0}}}};
    server.present(reused,{{},{0,-1,0},.2F,false},{},7);
    std::array<float,8> pcm{};output.mixer.render(pcm); CHECK(pcm[0]!=0);CHECK(pcm[1]==0);
    CHECK(server.statistics().attachment_invalidated==1);
    for(const auto status:{g::SoundAssetStatus::missing,g::SoundAssetStatus::not_authorized,
            g::SoundAssetStatus::open_failed,g::SoundAssetStatus::decode_failed}) {
        a::OfflineOutput isolated;g::ServerAudio rejected{isolated};FixtureAssets failed;
        failed.result={status,{}};rejected.consume(event(),{});rejected.update(&failed,{});
        CHECK(isolated.mixer.statistics().started==0);CHECK(rejected.first_error()!="none");
    }
    FixtureAssets pending;pending.result={g::SoundAssetStatus::pending,{}};
    a::OfflineOutput late;g::ServerAudio deferred{late};deferred.consume(event(),{});
    deferred.update(&pending,{}); deferred.update(&pending,{}); CHECK(deferred.statistics().pending==1);
    pending.result={g::SoundAssetStatus::ready,sample(false)};deferred.update(&pending,g::SoundTime{}+251ms);
    CHECK(late.mixer.statistics().started==0);CHECK(deferred.statistics().expired==1);
}
TEST_CASE("PCM RIFF importer validates formats chunks and authored loop boundaries","[audio][asset]") {
    assets::AssetImporterRegistries registry;
    REQUIRE(g::register_builtin_asset_importers(registry));
    auto registered_source=assets::AssetSource::create("sound/registered.wav",wav()); REQUIRE(registered_source);
    REQUIRE(registry.audio.probe(*registered_source.source).selected());
    REQUIRE(registry.audio.import(*registered_source.source));
    for(bool wide:{false,true}) for(bool stereo:{false,true}) {
        auto d=decode(wav(wide,stereo)); REQUIRE(d); CHECK(d.value().sample_rate==48000);
        CHECK(d.value().channel_count==(stereo?2:1)); CHECK(d.value().interleaved_samples[0]==-1);
        CHECK(d.value().interleaved_samples[1]==0); CHECK(d.value().interleaved_samples[2]==0.5F);
    }
    auto loop=decode(wav(false,false,true)); REQUIRE(loop); REQUIRE(loop.value().loop);
    CHECK(loop.value().loop->begin==1); CHECK(loop.value().loop->end==3);
    const auto good=wav(false,false,true);
    for(std::size_t n=0;n<good.size();++n) CHECK_FALSE(decode({good.begin(),good.begin()+static_cast<std::ptrdiff_t>(n)}));
    auto bad=good; bad[4]=std::byte{255}; CHECK_FALSE(decode(bad));
    bad=wav(); bad[34]=std::byte{3}; CHECK_FALSE(decode(bad)); // format becomes non-PCM
    bad=good; // corrupt authored cue frame
    for(std::size_t i=0;i+4<bad.size();++i) if(bad[i]==std::byte{'c'} && bad[i+1]==std::byte{'u'}) {bad[i+32]=std::byte{255}; break;}
    CHECK_FALSE(decode(bad));
}
TEST_CASE("Mixer emits independent exact sample oracle pitch duration loop and silence","[audio]") {
    a::Mixer m; m.listener({{}, {0,-1,0},1,false});
    a::Command c; c.voice=1; c.asset=sample(); c.attenuation=0;
    m.command(c); std::array<float,12> out{}; m.render(out);
    const std::array<float,6> expected{-1,0,0.5F,0,0.5F,0};
    for(std::size_t i=0;i<6;++i) {CHECK(out[i*2]==expected[i]); CHECK(out[i*2+1]==expected[i]);}
    c.operation=a::Operation::stop; m.command(c); m.render(out); CHECK(std::all_of(out.begin(),out.end(),[](float f){return f==0;}));
    c.operation=a::Operation::start; c.asset=sample(false); c.pitch=2; m.command(c); m.render(out);
    CHECK(out[0]==-1); CHECK(out[2]==0.5F); CHECK(out[4]==0); CHECK(m.active()==0);
    c.pitch=0.5F; m.command(c); m.render(out); CHECK(out[2]==-0.5F); CHECK(out[6]==0.25F);
    m.listener({{}, {0,-1,0},1,true}); m.render(out); CHECK(out[0]==0);
    m.listener({{}, {0,-1,0},1,false}); m.render(out); CHECK(out[0]==0); // no focus catch-up burst
}
TEST_CASE("Audio stereo spatialization and block size are sample clock deterministic","[audio]") {
    auto asset=std::make_shared<assets::AudioAsset>(); asset->sample_rate=48000; asset->channel_count=1; asset->interleaved_samples={0.25F}; asset->loop=assets::AudioAsset::Loop{0,1};
    a::Command c; c.asset=asset; c.voice=1; c.origin={0,-100,0}; c.attenuation=1;
    a::Mixer m; m.listener({{}, {0,-1,0},1,false}); m.command(c); std::array<float,2> out; m.render(out);
    CHECK(out[0]==0); CHECK(out[1]==Catch::Approx(0.45F));
    c.operation=a::Operation::source; c.origin={0,100,0}; m.command(c); m.render(out); CHECK(out[0]==Catch::Approx(0.45F)); CHECK(out[1]==0);
    c.origin={0,1000,0}; m.command(c); m.render(out); CHECK(out[0]==0);
    c.operation=a::Operation::start; c.asset=sample(); c.pitch=1.37F; c.attenuation=0;
    a::Mixer one,many; one.command(c); many.command(c); std::vector<float> large(9600),pieces(9600); one.render(large);
    std::size_t p=0; for(std::size_t frames:{1U,7U,43U,256U,499U,3994U}) {many.render(std::span<float>{pieces}.subspan(p,frames*2)); p+=frames*2;}
    REQUIRE(p==pieces.size()); CHECK(large==pieces);
    CHECK(one.statistics().frames==many.statistics().frames);
    CHECK(one.statistics().started==1); CHECK(many.statistics().started==1);
}
TEST_CASE("GoldSrc literal static and stop bodies retain exact owning sound values","[audio][runtime-control]") {
    // opcode 29, origin (-1,2,3), sparse index 300, volume128, attenuation32,
    // entity19, pitch150, change-volume/pitch flags192. No fixture bit writer.
    auto p=payload(bytes({29,248,255,16,0,24,0,44,1,128,32,19,0,150,192}));
    auto d=control(p); REQUIRE(d); const auto& b=std::get<g::RuntimeControlExactFixedBody>(d.event->body); REQUIRE(b.sound);
    CHECK(b.sound->origin==std::array<float,3>{-1,2,3}); CHECK(b.sound->sound_reference==300); CHECK(b.sound->field_mask==192);
    CHECK(d.event->provenance.end_cursor.absolute_bit_offset()==120);
    p=payload(bytes({16,155,0})); d=control(p); REQUIRE(d); const auto s=std::get<g::RuntimeControlExactFixedBody>(d.event->body).sound;
    REQUIRE(s); CHECK(s->entity_reference==19); CHECK(s->channel==3); CHECK(s->field_mask==32);
    for(std::size_t i=1;i<3;++i) {auto shortp=p; shortp.bytes.resize(i); auto bad=control(shortp); CHECK_FALSE(bad); REQUIRE(bad.error->failure_cursor);}
    p=payload(bytes({6,0})); d=control(p); CHECK_FALSE(d); REQUIRE(d.error->failure_cursor); CHECK(d.error->failure_cursor->absolute_bit_offset()==8);
    p=payload(bytes({6,0x0f,0x24,0x68,0x5a,0x95,0xa2,0x91,0x7c,0x24,0x7a,0x06}));
    d=control(p); REQUIRE(d); const auto& dynamic=std::get<g::RuntimeControlSound>(d.event->body);
    CHECK(dynamic.sound_reference==0x2345); CHECK(dynamic.origin[0]==-291.625F);
    CHECK(dynamic.pitch==0x67); CHECK(dynamic.volume==0x12); CHECK(dynamic.attenuation==0x34);
    for(std::size_t n=1;n<p.bytes.size();++n) {auto truncated=p; truncated.bytes.resize(n); auto failure=control(truncated); CHECK_FALSE(failure); REQUIRE(failure.error->failure_cursor); CHECK(failure.error->failure_cursor->absolute_bit_offset()<=n*8);}
}
TEST_CASE("Committed audio only publishes after complete production record transaction","[audio][runtime-replay]") {
    auto f=g::make_runtime_replay_fixture({}); REQUIRE(f); auto queue=std::make_shared<g::CommittedSoundQueue>(); f.fixture->initialization.sound_events=queue;
    hlclient::client::ClientWorldState world;
    auto session=g::RuntimeReplaySession::initialize(f.fixture->initialization,world); REQUIRE(session);
    auto record=f.fixture->records.front(); record.payload=payload(bytes({29,0,0,0,0,0,0,44,1,255,64,19,0,100,0,255})); record.initial_cursor={};
    CHECK_FALSE(session.session->apply_record(record)); g::CommittedSound got; CHECK_FALSE(queue->pop(got));
    record.payload.bytes.pop_back(); REQUIRE(session.session->apply_record(record)); REQUIRE(queue->pop(got)); CHECK(got.sound.sound_reference==300);
    CHECK_FALSE(session.session->apply_record(record)); CHECK_FALSE(queue->pop(got));
    ++record.record_identity; ++record.record_ordinal; ++record.payload.source_sequence;
    REQUIRE(session.session->apply_record(record)); REQUIRE(queue->pop(got));
    auto d=control(record.payload); REQUIRE(d); queue->publish(30,30,*d.event); queue->publish(30,30,*d.event); CHECK(queue->duplicates==1);
}
TEST_CASE("Impact decals preserve production replay atomicity and exactly once sound",
          "[audio][runtime-replay][temp-entity]") {
    for (const auto& decal : {
        bytes({23,104,8,0,240,255,24,0,7,5,0}),
        bytes({23,109,8,0,240,255,24,0,5,0,7}),
        bytes({23,116,8,0,240,255,24,0,7}),
        bytes({23,117,8,0,240,255,24,0,7}),
        bytes({23,118,8,0,240,255,24,0,7,5,0})}) {
        auto f=g::make_runtime_replay_fixture({}); REQUIRE(f);
        auto queue=std::make_shared<g::CommittedSoundQueue>();
        f.fixture->initialization.sound_events=queue;
        hlclient::client::ClientWorldState world;
        auto session=g::RuntimeReplaySession::initialize(f.fixture->initialization,world);
        REQUIRE(session);
        auto record=f.fixture->records.front(); // actual clientdata/entities prefix
        const auto sound=bytes({6,0,0x36,0x81,0x02,0}); // channel3 entity19 sound5
        record.payload.bytes.insert(record.payload.bytes.end(),sound.begin(),sound.end());
        record.payload.bytes.insert(record.payload.bytes.end(),decal.begin(),decal.end());
        const auto before=world.runtime_observation();
        const auto revision=session.session->publication_revision();
        const auto decoder_before=session.session->decoder_state();
        g::CommittedSound got;

        // Even after a valid clientdata + sound prefix, neither a truncated
        // decal nor an unknown suffix may commit state or publish audio.
        for (bool truncate : {true,false}) {
            auto bad=record;
            if (truncate) bad.payload.bytes.pop_back();
            else bad.payload.bytes.push_back(std::byte{255});
            CHECK_FALSE(session.session->apply_record(bad));
            CHECK(world.runtime_observation()==before);
            CHECK(session.session->publication_revision()==revision);
            const auto& decoder_after=session.session->decoder_state();
            CHECK(decoder_after.control_state()==decoder_before.control_state());
            CHECK(decoder_after.current_snapshot()==decoder_before.current_snapshot());
            CHECK(decoder_after.history().snapshot_count()==decoder_before.history().snapshot_count());
            CHECK(decoder_after.client_data_state().current_frame()==decoder_before.client_data_state().current_frame());
            CHECK(decoder_after.client_data_state().history().frame_count()==decoder_before.client_data_state().history().frame_count());
            CHECK_FALSE(queue->pop(got));
        }
        REQUIRE(session.session->apply_record(record));
        CHECK(session.session->publication_revision()==revision+1);
        REQUIRE(queue->pop(got));
        CHECK(got.sound.sound_reference==5);
        CHECK_FALSE(queue->pop(got)); // decal does not synthesize ricochet audio
        CHECK_FALSE(session.session->apply_record(record));
        CHECK_FALSE(queue->pop(got)); // duplicate record cannot replay the sound
        for (std::size_t i=1;i<f.fixture->records.size();++i)
            REQUIRE(session.session->apply_record(f.fixture->records[i]));
        CHECK(session.session->publication_revision()==revision+f.fixture->records.size());
        CHECK_FALSE(queue->pop(got));
    }
}
TEST_CASE("Pending server loops cancel replace update and reset without life coupling","[audio]") {
    a::OfflineOutput sink; g::ServerAudio server{sink}; FixtureAssets loader; loader.result.status=g::SoundAssetStatus::pending;
    const g::SoundTime t{}; auto e=event(); server.consume(e,t); server.update(&loader,t); CHECK(sink.mixer.active()==0);
    server.consume(event(2,3,32),t); loader.result.status=g::SoundAssetStatus::ready; server.update(&loader,t+20ms); CHECK(sink.mixer.active()==0);
    e=event(3); server.consume(e,t); server.update(&loader,t); CHECK(loader.index==300); CHECK(sink.mixer.active()==1);
    server.consume(e,t); server.update(&loader,t); CHECK(sink.mixer.statistics().started==1);
    e=event(4,3,192); e.sound.pitch=150; e.sound.volume=128; server.consume(e,t); CHECK(sink.mixer.statistics().updated==1); CHECK(sink.mixer.statistics().started==1);
    e=event(5); e.sound.sound_reference=301; server.consume(e,t); server.update(&loader,t); CHECK(sink.mixer.active()==1);
    e=event(6,0); server.consume(e,t); e=event(7,0); server.consume(e,t); server.update(&loader,t); CHECK(sink.mixer.active()==3);
    std::array<g::SoundSource,1> sources{{{19,{0,-100,64}}}};
    for(int i=0;i<10;++i) {sources[0].position.z+=2; server.present(sources,{},t+20ms*i,1);}
    CHECK(sink.mixer.statistics().started==4); // source motion never restarts
    server.reset(2); CHECK(sink.mixer.active()==0); server.consume(event(8),t); CHECK(sink.mixer.active()==0);
    e=event(1); e.generation=2; server.consume(e,t); server.update(&loader,t+11s); CHECK(sink.mixer.active()==0);
}
TEST_CASE("Server audio expires late one shots and bounds auto channels","[audio]") {
    a::OfflineOutput sink; g::ServerAudio server{sink}; FixtureAssets loader; loader.result.asset=sample(false);
    server.consume(event(),{}); server.update(&loader,g::SoundTime{}+251ms); CHECK(sink.mixer.active()==0); CHECK(server.statistics().expired==1);
    loader.result.asset=sample();
    for(std::size_t i=2;i<180;++i) server.consume(event(i,0),{});
    server.update(&loader,{}); CHECK(sink.mixer.active()==128); CHECK(server.statistics().limits>0);
    server.reset(2); CHECK(sink.mixer.active()==0);
}
TEST_CASE("Sound outbox is bounded and generation reset clears the old map","[audio]") {
    const auto p=payload(bytes({6,0,0x36,0x81,0x02,0}));
    auto decoded=control(p); REQUIRE(decoded);
    const auto& sound=std::get<g::RuntimeControlSound>(decoded.event->body);
    CHECK(sound.channel==3); CHECK(sound.entity_reference==19); CHECK(sound.sound_reference==5);
    CHECK_FALSE(sound.pitch); CHECK_FALSE(sound.volume); CHECK_FALSE(sound.attenuation);
    g::CommittedSoundQueue queue;
    for(std::size_t i=1;i<=600;++i) queue.publish(i,i,*decoded.event,{});
    CHECK(queue.dropped==88); CHECK(queue.take_overflow()); CHECK_FALSE(queue.take_overflow());
    g::CommittedSound e; unsigned count=0; while(queue.pop(e)) ++count; CHECK(count==512);
    a::OfflineOutput output; FixtureAssets assets; g::ServerAudio server{output};
    server.consume(event(),{}); server.update(&assets,{}); REQUIRE(output.mixer.active()==1);
    queue.reset_generation(2); REQUIRE(queue.pop(e)); server.consume(e,{}); CHECK(output.mixer.active()==0);
    server.consume(event(99),{}); CHECK(output.mixer.active()==0);
    queue.publish(700,700,*decoded.event,{}); CHECK_FALSE(queue.pop(e)); // old map
}
TEST_CASE("Render update cadence cannot replay committed sounds or change PCM","[audio]") {
    auto run=[](unsigned fps) {
        a::OfflineOutput output; g::ServerAudio server{output}; FixtureAssets assets;
        auto e=event(); e.received={}; server.consume(e,{}); server.update(&assets,{});
        for(unsigned f=0;f<fps;++f) {
            const auto t=g::SoundTime{}+std::chrono::milliseconds{1000*f/fps};
            server.consume(e,t); server.update(&assets,t); // retained input is not a new event
        }
        std::vector<float> pcm(960); output.mixer.render(pcm);
        CHECK(output.mixer.statistics().started==1); CHECK(server.statistics().started==1);
        return pcm;
    };
    CHECK(run(30)==run(144));
}
TEST_CASE("No audio device is a nonfatal explicit silent policy","[audio][no-device]") {
    a::Playback disabled{false}; CHECK(disabled.status()=="disabled"); CHECK_FALSE(disabled.submit({}));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"hlclient_nonexistent_backend"));
    {a::Playback unavailable{true}; CHECK(unavailable.status()=="audio_unavailable"); CHECK_FALSE(unavailable.submit({}));
     auto f=g::make_runtime_replay_fixture({}); REQUIRE(f); hlclient::client::ClientWorldState world;
     auto session=g::RuntimeReplaySession::initialize(f.fixture->initialization,world); REQUIRE(session);
     for(const auto& record:f.fixture->records) REQUIRE(session.session->apply_record(record));}
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
    CHECK(hlclient::core::parse_command_line(std::array<std::string_view,2>{"--audio-volume","0"}));
    CHECK_FALSE(hlclient::core::parse_command_line(std::array<std::string_view,2>{"--audio-volume","101"}));
}
TEST_CASE("Approved async sparse sound binding uses rooted WAV registry with no model fallback","[audio][asset]") {
    namespace f=hlclient::tests::readiness_fixture;
    hlclient::tests::ScopedLocalResourceTestRoot root;
    root.write("valve","maps/test.bsp","project fixture, metadata only");
    root.write("valve","sound/weapons/project.wav",wav(false,false,true));
    root.write("valve","sound/weapons/client_only.wav",wav());
    root.write("valve","sound/player/project.wav",wav());
    root.write("valve","sound/player/pl_step1.wav",wav());
    root.write("valve","sound/player/pl_shell1.wav",wav());
    root.write("valve","sound/player/bad_effect.wav","not a WAV");
    root.write("valve","sound/!sentence.wav",wav());
    root.write("valve","sound/#stream.wav",wav());
    root.write("valve","sound/sound/doubled.wav",wav());
    const auto resources=f::parse_resource_list({
        {2,"maps/test.bsp",1,0xffffffU,0},
        {0,"weapons/project.wav",300,0xffffffU,0},
        {2,"models/not-a-sound.mdl",301,0xffffffU,0},
        {0,"player/project.wav",900,0xffffffU,0},
        {0,"!sentence.wav",901,0xffffffU,0},
        {0,"#stream.wav",902,0xffffffU,0},
        {0,"sound/doubled.wav",903,0xffffffU,0}});
    auto env=std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>{f::make_environment(root)};
    auto inventory=f::build_inventory(resources,*env);
    auto manifest=f::build_manifest(resources,inventory,f::parse_server_info("maps/test.bsp"),*env); REQUIRE(manifest);
    auto owned=std::make_shared<const g::PrecacheManifestState>(std::move(*manifest.state));
    g::ApprovedSoundAssets loader{{owned,env}};
    const auto wait=[&](std::uint16_t index) {
        auto result=loader.request(index);
        const auto deadline=std::chrono::steady_clock::now()+2s;
        while(result.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {std::this_thread::sleep_for(1ms); result=loader.request(index);}
        return result;
    };
    auto first=wait(300); REQUIRE(first.status==g::SoundAssetStatus::ready); REQUIRE(first.asset); CHECK(first.asset->loop.has_value());
    CHECK(first.asset->interleaved_samples[2]==0.5F);
    auto same=wait(300); CHECK(same.asset==first.asset);
    CHECK(wait(301).status==g::SoundAssetStatus::missing); // model index cannot bind sound
    CHECK(wait(299).status==g::SoundAssetStatus::missing); // sparse hole, not ordinal
    auto other=wait(900); REQUIRE(other.status==g::SoundAssetStatus::ready); CHECK_FALSE(other.asset->loop);
    for(auto index:std::array<std::uint16_t,3>{901,902,903}) CHECK(wait(index).status==g::SoundAssetStatus::unsupported);
    a::OfflineOutput output; g::ServerAudio server{output};
    server.consume(event(),{}); server.update(&loader,{}); std::array<float,8> pcm{}; output.mixer.render(pcm);
    CHECK(output.mixer.statistics().started==1); CHECK(pcm[0]!=0); // same production loader -> actual PCM
    hlclient::game_api::LocalSoundReference local;
    constexpr std::string_view local_name="weapons/client_only.wav";
    std::copy(local_name.begin(),local_name.end(),local.sample.begin());
    auto local_result=loader.request_local(local);
    const auto local_deadline=std::chrono::steady_clock::now()+2s;
    while(local_result.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<local_deadline) {
        std::this_thread::sleep_for(1ms); local_result=loader.request_local(local);
    }
    REQUIRE(local_result.status==g::SoundAssetStatus::ready); REQUIRE(local_result.asset);
    CHECK(local_result.asset->interleaved_samples[2]==0.5F);
    CHECK(loader.request_local(local).asset==local_result.asset); // no server precache slot
    local.source=hlclient::game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    CHECK(loader.request_local(local).asset==local_result.asset); // same bounded worker/cache
    for(const auto name:{std::string_view{"player/pl_step1.wav"},
            std::string_view{"player/pl_shell1.wav"}}) {
        hlclient::game_api::LocalSoundReference effect;
        effect.source=hlclient::game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        std::copy(name.begin(),name.end(),effect.sample.begin());
        auto loaded=loader.request_local(effect);
        const auto deadline=std::chrono::steady_clock::now()+2s;
        while(loaded.status==g::SoundAssetStatus::pending &&
              std::chrono::steady_clock::now()<deadline) {
            std::this_thread::sleep_for(1ms);loaded=loader.request_local(effect);
        }
        REQUIRE(loaded.status==g::SoundAssetStatus::ready);
        REQUIRE(loaded.asset);
        CHECK(loaded.asset->interleaved_samples[2]==0.5F);
    }
    hlclient::game_api::LocalSoundReference bad_effect;
    bad_effect.source=hlclient::game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    constexpr std::string_view bad_name="player/bad_effect.wav";
    std::copy(bad_name.begin(),bad_name.end(),bad_effect.sample.begin());
    auto bad=loader.request_local(bad_effect);
    const auto bad_deadline=std::chrono::steady_clock::now()+2s;
    while(bad.status==g::SoundAssetStatus::pending &&
          std::chrono::steady_clock::now()<bad_deadline) {
        std::this_thread::sleep_for(1ms);bad=loader.request_local(bad_effect);
    }
    CHECK(bad.status==g::SoundAssetStatus::decode_failed);
    local.sample.fill(0); constexpr std::string_view unsafe="weapons/../../private.wav";
    std::copy(unsafe.begin(),unsafe.end(),local.sample.begin());
    CHECK(loader.request_local(local).status==g::SoundAssetStatus::not_authorized);
}
TEST_CASE("Opt in local WAV decoding uses original read only assets","[audio][local-audio]") {
    const auto* root=SDL_getenv("HLCLIENT_LOCAL_GAME_ROOT"); if(!root) SKIP("explicit local asset root not enabled");
    for(const auto* name:{"items/gunpickup2.wav","items/medcharge4.wav","items/medshot4.wav","items/suitcharge1.wav","doors/doorstop6.wav","doors/doormove1.wav"}) {
        std::ifstream file{std::filesystem::path{root}/"valve"/"sound"/name,std::ios::binary}; REQUIRE(file);
        std::vector<char> raw{std::istreambuf_iterator<char>{file},{}}; std::vector<std::byte> b(raw.size());
        std::transform(raw.begin(),raw.end(),b.begin(),[](char c){return static_cast<std::byte>(c);});
        auto result=decode(std::move(b)); INFO(name); REQUIRE(result);
        CHECK(result.value().sample_rate>=11025); CHECK_FALSE(result.value().interleaved_samples.empty());
    }
}
TEST_CASE("E8 approved startup preloads all step families without server slots and plays first PCM", "[e8][audio][asset]") {
    namespace api=hlclient::game_api;
    namespace f=hlclient::tests::readiness_fixture;
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,{1}}); host.set_movement_audio_time_origin(0);
    host.configure_movement_materials("S FLOOR\n");
    hlclient::tests::ScopedLocalResourceTestRoot root;
    root.write("valve","maps/test.bsp","project fixture metadata");
    for(const auto& token:host.movement_sound_preparation())
        root.write("valve","sound/"+std::string{token.name()},wav());
    const auto resources=f::parse_resource_list({{2,"maps/test.bsp",1,0xffffffU,0}});
    auto env=std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>{f::make_environment(root)};
    auto inventory=f::build_inventory(resources,*env);
    auto manifest=f::build_manifest(resources,inventory,f::parse_server_info("maps/test.bsp"),*env); REQUIRE(manifest);
    g::ApprovedSoundAssets loader{{std::make_shared<const g::PrecacheManifestState>(std::move(*manifest.state)),env}};
    // This is the same composition order as normal main: prepare all bounded
    // module tokens when the approved root arrives, before attaching prediction.
    for(const auto& token:host.movement_sound_preparation()) (void)loader.request_local(token);
    const auto deadline=std::chrono::steady_clock::now()+2s;
    for(const auto& token:host.movement_sound_preparation()) {
        auto result=loader.request_local(token);
        while(result.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
            std::this_thread::sleep_for(1ms); result=loader.request_local(token);
        }
        REQUIRE(result.status==g::SoundAssetStatus::ready); REQUIRE(result.asset);
        CHECK(result.asset->interleaved_samples[2]==.5F);
    }
    api::MovementAudioObservation o; o.generation=1; o.life_epoch=1; o.command_sequence=1;
    o.command_milliseconds=20; o.scheduled_seconds=.02;
    o.before_mode=o.after_mode=api::MovementAudioMode::ground;
    o.before_grounded=o.after_grounded=true; o.total_speed=o.horizontal_speed=320;
    o.movevars_footsteps=true; o.dry_context=true; o.surface_status=api::MovementSurfaceStatus::found;
    o.texture_name="FLOOR"; o.world_origin=assets::AssetVector3{0,0,0};
    host.observe_movement_audio(o,false); const auto b=host.drain_audio(); REQUIRE(b.count==1);
    CHECK(b.cues[0].reference.name().starts_with("player/pl_slosh"));
    a::OfflineOutput output; output.set_listener({{0,0,64},{1,0,0},1,false});
    g::LocalAudio local(output); local.update(b,&loader,.02,true);
    std::array<float,8> pcm{}; output.mixer.render(pcm);
    CHECK(output.mixer.statistics().presentation_started==1); CHECK(pcm[4]>.2F);
}
TEST_CASE("E8 opt in original step samples decode read only with distinct families", "[e8][audio][installed-materials]") {
    const auto* root=SDL_getenv("HLCLIENT_LOCAL_GAME_ROOT"); if(!root) SKIP("explicit read-only root not enabled");
    hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,{1}});
    for(const auto& token:host.movement_sound_preparation()) {
        std::ifstream file{std::filesystem::path{root}/"valve"/"sound"/token.name(),std::ios::binary};
        INFO(token.name()); REQUIRE(file);
        std::vector<char> raw{std::istreambuf_iterator<char>{file},{}};
        std::vector<std::byte> b(raw.size());
        std::transform(raw.begin(),raw.end(),b.begin(),[](char c){return static_cast<std::byte>(c);});
        auto result=decode(std::move(b)); REQUIRE(result);
        CHECK(std::any_of(result.value().interleaved_samples.begin(),result.value().interleaved_samples.end(),
            [](float value){return std::isfinite(value) && std::abs(value)>.001F;}));
    }
}
TEST_CASE("Opt in short moderate SDL playback exercises actual output without game","[audio][audio-device]") {
    if(!SDL_getenv("HLCLIENT_AUDIO_DEVICE_TEST")) SKIP("explicit playback capability test not enabled");
    a::Playback output{true}; if(output.status()!="playback_ready") SKIP("audio_unavailable");
    output.set_listener({{}, {0,-1,0},0.35F,false});
    auto asset=std::make_shared<assets::AudioAsset>(); asset->sample_rate=48000; asset->channel_count=1;
    for(unsigned i=0;i<7200;++i) asset->interleaved_samples.push_back(0.02F*std::sin(static_cast<float>(i)*440.0F*6.2831853F/48000.0F));
    a::Command c; c.asset=asset; c.voice=1; c.attenuation=0; REQUIRE(output.submit(c));
    std::this_thread::sleep_for(250ms); CHECK(output.statistics().mixer.frames>=7200); CHECK(output.statistics().mixer.started==1);
    CHECK(output.statistics().queue_failures==0); CHECK(output.statistics().queued_frames<=960);
}
