#include "snark_test_fixture.hpp"
#include "hand_grenade_world_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"

#include <hlclient/audio/output.hpp>
#include <hlclient/games/halflife/snark_profile.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
struct SnarkModelAudit {
    std::string_view name;
    std::uint16_t slot;
    std::size_t sequences;
    std::uint64_t sources;
};
constexpr std::array snark_models{
    SnarkModelAudit{"models/v_squeak.mdl",75,6,1},
    SnarkModelAudit{"models/p_squeak.mdl",71,1,1},
    SnarkModelAudit{"models/w_sqknest.mdl",237,2,2},
    SnarkModelAudit{"models/w_squeak.mdl",238,4,2}};

struct SnarkSoundAudit {
    std::string_view name;
    std::uint16_t slot;
    std::uint8_t channel;
    std::uint8_t pitch;
};
// Exact SDK monster precache names, with representative server channels/pitches.
constexpr std::array snark_sounds{
    SnarkSoundAudit{"sound/squeek/sqk_blast1.wav",101,3,100},
    SnarkSoundAudit{"sound/common/bodysplat.wav",102,2,200},
    SnarkSoundAudit{"sound/squeek/sqk_die1.wav",103,2,131},
    SnarkSoundAudit{"sound/squeek/sqk_hunt1.wav",104,2,95},
    SnarkSoundAudit{"sound/squeek/sqk_hunt2.wav",105,2,105},
    SnarkSoundAudit{"sound/squeek/sqk_hunt3.wav",106,2,105},
    SnarkSoundAudit{"sound/squeek/sqk_deploy1.wav",107,1,155}};

std::filesystem::path approved_snark_root() {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* raw=nullptr;
    std::size_t count{};
    if(_wdupenv_s(&raw,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && raw)root=raw;
    std::free(raw);
#endif
    return root;
}

bool opted_in(const std::filesystem::path& root) {
    return root.lexically_normal()==
        std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal();
}

void print_snark_bounds(std::string_view resource,std::string_view kind,
    const hlclient::assets::ModelBounds& bounds) {
    std::cout<<"snark-bounds "<<resource<<" "<<kind<<" min="
        <<bounds.minimum.x<<","<<bounds.minimum.y<<","<<bounds.minimum.z<<" max="
        <<bounds.maximum.x<<","<<bounds.maximum.y<<","<<bounds.maximum.z<<'\n';
    CHECK(std::isfinite(bounds.minimum.x));CHECK(std::isfinite(bounds.minimum.y));
    CHECK(std::isfinite(bounds.minimum.z));CHECK(std::isfinite(bounds.maximum.x));
    CHECK(std::isfinite(bounds.maximum.y));CHECK(std::isfinite(bounds.maximum.z));
}
}

TEST_CASE("E27 approved snark view held nest living models and seven server samples use normal import GL and PCM",
    "[e27][snark-assets][optional-local]") {
    namespace t=hlclient::tests;
    namespace sf=t::snark_fixture;
    namespace wf=t::hand_grenade_world_fixture;
    namespace c=hlclient::client;
    namespace g=hlclient::goldsrc;
    namespace api=hlclient::game_api;
    namespace r=hlclient::renderer;

    const auto root=approved_snark_root();
    if(!opted_in(root))SKIP("Exact approved read-only research root opt-in absent");
    const auto resources=t::readiness_fixture::parse_resource_list({
        {2,"maps/crossfire.bsp",9,0,0},
        {2,"models/v_squeak.mdl",75,0,0},{2,"models/p_squeak.mdl",71,0,0},
        {2,"models/w_sqknest.mdl",237,0,0},{2,"models/w_squeak.mdl",238,0,0},
        {2,"sprites/bloodspray.spr",321,0,0},{2,"sprites/blood.spr",322,0,0},
        {0,"squeek/sqk_blast1.wav",101,0,0},{0,"common/bodysplat.wav",102,0,0},
        {0,"squeek/sqk_die1.wav",103,0,0},{0,"squeek/sqk_hunt1.wav",104,0,0},
        {0,"squeek/sqk_hunt2.wav",105,0,0},{0,"squeek/sqk_hunt3.wav",106,0,0},
        {0,"squeek/sqk_deploy1.wav",107,0,0}});
    auto made=hlclient::app::RuntimeReplayLocalAssets::create(resources,
        t::readiness_fixture::parse_server_info("maps/crossfire.bsp"),root,"valve",
        hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(made.projection!=nullptr);
    auto s=sf::state();
    wf::stamp(s,1,1);
    c::ClientWorldState world;
    REQUIRE(made.projection->reset_generation(s,world));

    for(const auto& expected:snark_models) {
        INFO(expected.name);
        const auto data=made.projection->studio_model_data(expected.slot);
        REQUIRE(data!=nullptr);
        CHECK(data->sequences.size()==expected.sequences);
        // A source count of two is the production-approved main/texture bundle,
        // not a raw game-file open or an external animation-group assumption.
        CHECK(data->statistics.source_count==expected.sources);
        REQUIRE(data->sequence_groups.size()==1);
        CHECK_FALSE(data->sequence_groups[0].externally_stored);
        REQUIRE_FALSE(data->bones.empty());REQUIRE_FALSE(data->bodyparts.empty());
        REQUIRE_FALSE(data->textures.empty());REQUIRE_FALSE(data->skin_families.empty());
        std::cout<<"snark-model "<<expected.name<<" sources="<<data->statistics.source_count
            <<" bones="<<data->bones.size()<<" attachments="<<data->attachments.size()
            <<" controllers="<<data->bone_controllers.size()<<" hitboxes="<<data->hitboxes.size()
            <<" bodies="<<data->bodyparts.size()<<" submodels="<<data->submodels.size()
            <<" textures="<<data->textures.size()<<" skins="<<data->skin_families.size()<<'\n';
        print_snark_bounds(expected.name,"movement",data->source_movement_bounds);
        print_snark_bounds(expected.name,"clipping",data->source_clipping_bounds);
        for(std::size_t i=0;i<data->sequences.size();++i) {
            const auto& sequence=data->sequences[i];
            CHECK_FALSE(sequence.label.empty());CHECK(sequence.frame_count>0);
            CHECK(std::isfinite(sequence.frames_per_second));
            CHECK(sequence.frames_per_second>=0);
            if(sequence.frame_count>1)CHECK(sequence.frames_per_second>0);
            std::cout<<"snark-sequence "<<expected.name<<" "<<i<<" "<<sequence.label
                <<" frames="<<sequence.frame_count<<" fps="<<sequence.frames_per_second
                <<" flags="<<sequence.source_flags<<" events="<<sequence.events.size()<<'\n';
            print_snark_bounds(expected.name,sequence.label,sequence.bounds);
        }
        if(expected.slot==75) {
            // Observed through this approved importer in the first actual E27
            // audit. These are asset facts, not SDK-enum-derived label guesses.
            constexpr std::array<std::string_view,6> labels{
                "idle1","fidgetfit","fidgetnip","down","up","throw"};
            constexpr std::array<std::uint32_t,6> frames{31,71,81,21,51,14};
            constexpr std::array<float,6> fps{16,16,16,16,30,24};
            REQUIRE(data->sequences.size()==labels.size());
            for(std::size_t i=0;i<labels.size();++i) {
                CHECK(data->sequences[i].label==labels[i]);
                CHECK(data->sequences[i].frame_count==frames[i]);
                CHECK(data->sequences[i].frames_per_second==fps[i]);
            }
        }
        for(std::size_t i=0;i<data->bodyparts.size();++i) {
            const auto& body=data->bodyparts[i];
            std::cout<<"snark-bodypart "<<expected.name<<" "<<i<<" "<<body.name
                <<" base="<<body.base<<" submodels="<<body.submodel_indices.size()<<'\n';
        }
        for(std::size_t i=0;i<data->bones.size();++i)std::cout<<"snark-bone "<<expected.name
            <<" "<<i<<" "<<data->bones[i].name<<" parent="<<data->bones[i].parent_index<<'\n';
        for(std::size_t i=0;i<data->attachments.size();++i) {
            const auto& attachment=data->attachments[i];
            std::cout<<"snark-attachment "<<expected.name<<" "<<i<<" "<<attachment.name
                <<" bone="<<attachment.bone_index<<" origin="<<attachment.local_origin.x<<","
                <<attachment.local_origin.y<<","<<attachment.local_origin.z<<'\n';
        }
        const auto metadata=made.projection->presentation_model(1,expected.slot);
        REQUIRE(metadata.has_value());
        CHECK(metadata->resource_name==expected.name);
        CHECK(metadata->supported_bodies[0]);CHECK(metadata->selectable_bodies[0]);
        // Run-length reporting preserves the complete 256-body bitmap without
        // repeating identical body selections hundreds of times in diagnostics.
        for(std::size_t body=0;body<metadata->supported_bodies.size();) {
            const auto first=body;
            const bool supported=metadata->supported_bodies[body];
            const bool selectable=metadata->selectable_bodies[body];
            while(++body<metadata->supported_bodies.size() &&
                metadata->supported_bodies[body]==supported &&
                metadata->selectable_bodies[body]==selectable) {}
            std::cout<<"snark-body "<<expected.name<<" first="<<first<<" last="<<body-1
                <<" supported="<<supported<<" selectable="<<selectable<<'\n';
        }
    }
    std::cout<<"snark-companion models/w_sqknestT.mdl approved_sources=2\n"
        <<"snark-companion models/w_squeakT.mdl approved_sources=2\n";
    const auto view=made.projection->presentation_model(1,75);
    REQUIRE(view.has_value());
    // Imported labels have been reported before this gate: a profile mismatch
    // cannot hide the actual evidence or encourage guessed replacement labels.
    CHECK(hlclient::games::halflife::snark_sequences(*view));

    g::ApprovedSoundAssets sounds(made.projection->sound_resources());
    for(const auto& sample:snark_sounds) {
        INFO(sample.name);
        CHECK(sounds.virtual_name(sample.slot)==sample.name);
        auto loaded=sounds.request(sample.slot);
        for(unsigned retry=0;loaded.status==g::SoundAssetStatus::pending && retry<1000;++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            loaded=sounds.request(sample.slot);
        }
        REQUIRE(loaded.status==g::SoundAssetStatus::ready);
        REQUIRE(loaded.asset!=nullptr);
        REQUIRE(loaded.asset->sample_rate>0);REQUIRE(loaded.asset->channel_count>0);
        REQUIRE_FALSE(loaded.asset->interleaved_samples.empty());
        CHECK(loaded.asset->interleaved_samples.size()%loaded.asset->channel_count==0);
        double peak{};
        CHECK(std::ranges::all_of(loaded.asset->interleaved_samples,
            [](float value){return std::isfinite(value);}));
        for(const auto value:loaded.asset->interleaved_samples)
            peak=std::max(peak,double(std::abs(value)));
        CHECK(peak>.001);
        const auto frames=loaded.asset->interleaved_samples.size()/loaded.asset->channel_count;
        std::cout<<"snark-pcm "<<sample.name<<" decoded_float32 rate="<<loaded.asset->sample_rate
            <<" channels="<<loaded.asset->channel_count<<" frames="<<frames
            <<" samples="<<loaded.asset->interleaved_samples.size()
            <<" seconds="<<double(frames)/loaded.asset->sample_rate<<" peak="<<peak
            <<" loop="<<loaded.asset->loop.has_value()<<'\n';
        wf::Sink sink;
        g::ServerAudio audio(sink);
        audio.present({},{{},{0,-1,0},1,false},{},1);
        auto event=wf::sound(sample.slot,40,sample.slot,-20,sample.channel);
        event.sound.volume=255;event.sound.pitch=sample.pitch;
        audio.consume(event,{});audio.update(&sounds,{});
        REQUIRE(sink.starts.size()==1);
        std::array<float,4096> pcm{};
        double output_peak{};
        for(unsigned block=0;block<50;++block) {
            sink.output.mixer.render(pcm);
            CHECK(std::ranges::all_of(pcm,[](float value){return std::isfinite(value);}));
            for(const auto value:pcm)output_peak=std::max(output_peak,double(std::abs(value)));
        }
        CHECK(output_peak>.0001);
        std::cout<<"snark-mixed "<<sample.name<<" channel="<<unsigned(sample.channel)
            <<" pitch="<<unsigned(sample.pitch)<<" peak="<<output_peak<<'\n';
    }

    // The pop's two approved sprites travel through the existing temporary
    // sprite materializer. Inspect its actual masked color coverage, not raw
    // palette files or a fabricated replacement bitmap.
    constexpr std::array blood_names{"sprites/bloodspray.spr","sprites/blood.spr"};
    for(std::size_t index=0;index<blood_names.size();++index) {
        INFO(blood_names[index]);
        hlclient::app::TemporarySpriteFrame blood;
        blood.identity={1,1,24,static_cast<std::uint32_t>(index)};
        blood.sprite.model_slot=static_cast<std::uint16_t>(321+index);
        blood.sprite.scale=.25F;blood.sprite.frames_per_second=12;
        blood.sprite.blend=api::TemporarySpriteBlend::alpha_blended;
        blood.sprite.tint={1,.85F,0};
        const auto materialized=made.projection->materialize_temporary_sprites(std::span{&blood,1U},0);
        REQUIRE(materialized.has_value());
        REQUIRE(materialized->frame->sprite_instances().size()==1);
        const auto& instance=materialized->frame->sprite_instances()[0];
        CHECK(instance.tint==blood.sprite.tint);
        const auto& asset=*materialized->package->sprite_assets()[instance.sprite_asset_index];
        std::size_t covered{};
        std::size_t covered_blue_chromatic{};
        std::size_t transparent_chromatic{};
        unsigned minimum_alpha=255,maximum_alpha=0;
        bool grayscale=true,covered_grayscale=true;
        for(const auto& frame:asset.frames()) {
            REQUIRE(frame.rgba8.size()%4==0);
            for(std::size_t pixel=0;pixel<frame.rgba8.size();pixel+=4) {
                const auto red=std::to_integer<unsigned>(frame.rgba8[pixel]);
                const auto green=std::to_integer<unsigned>(frame.rgba8[pixel+1]);
                const auto blue=std::to_integer<unsigned>(frame.rgba8[pixel+2]);
                const auto alpha=std::to_integer<unsigned>(frame.rgba8[pixel+3]);
                grayscale=grayscale && red==green && green==blue;
                covered+=alpha>0;
                covered_blue_chromatic+=alpha>0 && (blue!=red || blue!=green);
                if(alpha>0)covered_grayscale=covered_grayscale && red==green && green==blue;
                transparent_chromatic+=alpha==0 && (red!=green || green!=blue);
                minimum_alpha=std::min(minimum_alpha,alpha);
                maximum_alpha=std::max(maximum_alpha,alpha);
            }
        }
        std::cout<<"snark-blood-sprite "<<blood_names[index]
            <<" format="<<static_cast<unsigned>(asset.texture_format())
            <<" frames="<<asset.frames().size()<<" grayscale="<<grayscale
            <<" covered="<<covered<<" covered_gray="<<covered_grayscale
            <<" covered_blue_chromatic="<<covered_blue_chromatic<<" transparent_chromatic="<<transparent_chromatic
            <<" alpha_min="<<minimum_alpha<<" alpha_max="<<maximum_alpha<<'\n';
        CHECK(asset.texture_format()==hlclient::assets::SpriteTextureFormat::alpha_test);
        CHECK(asset.render_profile()==hlclient::entity_render::SpriteRenderTextureProfile::alpha_test_masked);
        CHECK_FALSE(grayscale);CHECK(covered>0);CHECK(minimum_alpha<maximum_alpha);
        CHECK(covered_grayscale);CHECK(covered_blue_chromatic==0);CHECK(transparent_chromatic>0);
        CHECK(minimum_alpha==0);CHECK(maximum_alpha==255);
    }

    auto gl=t::entity_opengl_fixture::try_context(320,240);
    if(!gl || !t::entity_opengl_fixture::capable_context())
        SKIP("SDL/OpenGL 3.3 unavailable after approved import and PCM checks");
    gl->initialize_renderer();
    sf::Module module;
    module.host.bind_model(view);
    module.host.observe(s,0);
    r::RenderScene scene;
    scene.clear_color={0,0,0,1};
    scene.camera={{100,80,80},{0,80,80},{0,0,1},1.04719755F,.1F,4096};
    const auto draw=[&]() {
        gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        return gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    };
    const auto baseline=draw();
    for(std::uint32_t sequence=0;sequence<view->sequences.size();++sequence) {
        const auto& metadata=view->sequences[sequence];
        const double start=10+double(sequence)*10;
        std::size_t visible{};
        const auto stride=std::max(1U,metadata.frame_count/8U);
        for(std::uint32_t frame=0;frame<metadata.frame_count;frame+=stride) {
            const double now=start+(metadata.fps>0 ? frame/metadata.fps : 0);
            const auto pose=module.host.viewmodel(s,view,now,
                api::LocalWeaponVisual{sequence,0,100+sequence,start});
            REQUIRE(pose.status==api::ViewmodelStatus::ready);
            scene.first_person_entities=made.projection->materialize_viewmodel(pose);
            REQUIRE(scene.first_person_entities.has_value());
            visible+=draw().rgba8!=baseline.rgba8;
        }
        std::cout<<"snark-visible models/v_squeak.mdl sequence="<<sequence<<" frames="<<visible<<'\n';
        CHECK(visible>0);
    }
    scene.first_person_entities.reset();
    // Ordinary packet entities use the normal imported Studio path. The SDK
    // RUN index is 3; pickup nest animation is 1. No local monster simulation.
    std::size_t ordinal=20;
    for(const auto variant:std::array{std::array<unsigned,2>{71,0},
        std::array<unsigned,2>{237,1},std::array<unsigned,2>{238,3}}) {
        scene.dynamic_entities.reset();
        scene.camera={{100,-100,60},{0,0,8},{0,0,1},1.04719755F,.1F,256};
        const auto empty=draw();
        c::RuntimePacketEntityObservation entity;
        entity.entity_number=40;entity.model_index=variant[0];entity.sequence=variant[1];
        entity.body=entity.skin=0;entity.frame=0;entity.frame_rate=1;
        entity.animation_time_seconds=double(ordinal);entity.origin={0,0,0};entity.angles={0,0,0};
        entity.ordinary_visual_schema=true;entity.solid=variant[0]==238 ? 2U : 0U;
        entity.brush_move_type=variant[0]==238 ? 10U : 5U;entity.effects=0;
        s.packet_entities={entity};++ordinal;wf::stamp(s,ordinal,double(ordinal));
        world.set_camera({{100,-100,60},{0,0,8},{0,0,1}});
        REQUIRE(made.projection->project_entities(s,world));
        REQUIRE(made.projection->present_entities(module.host,double(ordinal),1,world,{320,240}));
        const auto frame=world.entity_frame();REQUIRE(frame!=nullptr);
        REQUIRE(frame->studio_instances().size()==1);
        CHECK(frame->studio_instances()[0].body_value==0);
        for(const auto& pose:frame->studio_poses())for(const auto& matrix:pose.bone_matrices)
            CHECK(std::ranges::all_of(matrix,[](float value){return std::isfinite(value);}));
        scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),frame,{}};
        const auto image=draw();CHECK(image.rgba8!=empty.rgba8);
        const auto name=std::ranges::find(snark_models,variant[0],&SnarkModelAudit::slot);
        REQUIRE(name!=snark_models.end());
        std::cout<<"snark-world-visible "<<name->name<<" sequence="<<variant[1]
            <<" pixels="<<image.non_clear_pixel_count<<'\n';
    }
    // Actual gray covered texels with white tint A versus HAL yellow tint B.
    // Colored transparent padding is retained by the normal masked texture
    // path. Only tint changes; geometry, alpha, asset and camera stay identical.
    scene={};scene.clear_color={0,0,0,1};
    scene.camera={{0,-64,16},{0,0,16},{0,0,1},1.04719755F,.1F,256};
    const auto neutral=draw();
    for(std::size_t index=0;index<blood_names.size();++index) {
        INFO(blood_names[index]);
        hlclient::app::TemporarySpriteFrame blood;
        blood.identity={1,2,24,static_cast<std::uint32_t>(index)};
        blood.origin={0,0,16};blood.sprite.model_slot=static_cast<std::uint16_t>(321+index);
        blood.sprite.scale=.5F;blood.sprite.frames_per_second=12;
        blood.sprite.blend=api::TemporarySpriteBlend::alpha_blended;
        blood.sprite.tint={1,1,1};
        scene.transient_sprite_entities=made.projection->materialize_temporary_sprites(std::span{&blood,1U},0);
        REQUIRE(scene.transient_sprite_entities.has_value());
        CHECK((scene.transient_sprite_entities->frame->sprite_instances()[0].tint==std::array<float,3>{1,1,1}));
        const auto source_tinted=draw();
        blood.sprite.tint={1,.85F,0};
        scene.transient_sprite_entities=made.projection->materialize_temporary_sprites(std::span{&blood,1U},0);
        REQUIRE(scene.transient_sprite_entities.has_value());
        CHECK((scene.transient_sprite_entities->frame->sprite_instances()[0].tint==std::array<float,3>{1,.85F,0}));
        const auto yellow=draw();
        REQUIRE(source_tinted.rgba8.size()==neutral.rgba8.size());
        REQUIRE(yellow.rgba8.size()==neutral.rgba8.size());
        std::size_t color_changes{},darker_pixels{},white_gray_pixels{},yellow_pixels{},yellow_chromatic{},yellow_red{},covered{},background{},source_covered{};
        std::array<std::uint64_t,3> source_total{},yellow_total{};
        std::array<unsigned,3> source_max{},yellow_max{};
        double source_max_red_green{},source_max_green_red{};
        bool same_coverage=true,same_alpha=true,same_red=true,nonincreasing_intensity=true;
        for(std::size_t pixel=0;pixel<neutral.rgba8.size();pixel+=4) {
            const auto changed=[&](const auto& image) {
                return image.rgba8[pixel]!=neutral.rgba8[pixel] ||
                    image.rgba8[pixel+1]!=neutral.rgba8[pixel+1] ||
                    image.rgba8[pixel+2]!=neutral.rgba8[pixel+2] ||
                    image.rgba8[pixel+3]!=neutral.rgba8[pixel+3];
            };
            const bool before=changed(source_tinted),after=changed(yellow);
            same_coverage=same_coverage && before==after;
            same_alpha=same_alpha && source_tinted.rgba8[pixel+3]==yellow.rgba8[pixel+3];
            same_red=same_red && source_tinted.rgba8[pixel]==yellow.rgba8[pixel];
            covered+=after;background+=!after;
            bool changed_color=false,darker=false;
            for(std::size_t channel=0;channel<3;++channel) {
                const auto original=source_tinted.rgba8[pixel+channel];
                const auto tinted=yellow.rgba8[pixel+channel];
                changed_color=changed_color || tinted!=original;
                darker=darker || tinted<original;
                nonincreasing_intensity=nonincreasing_intensity && tinted<=original;
            }
            color_changes+=changed_color;darker_pixels+=darker;
            if(before) {
                const auto red=unsigned(source_tinted.rgba8[pixel]);
                const auto green=unsigned(source_tinted.rgba8[pixel+1]);
                const auto blue=unsigned(source_tinted.rgba8[pixel+2]);
                ++source_covered;
                const std::array rgb{red,green,blue};
                for(std::size_t channel=0;channel<rgb.size();++channel) {
                    source_total[channel]+=rgb[channel];source_max[channel]=std::max(source_max[channel],rgb[channel]);
                }
                source_max_red_green=std::max(source_max_red_green,double(red)/std::max(1U,green));
                source_max_green_red=std::max(source_max_green_red,double(green)/std::max(1U,red));
                white_gray_pixels+=red>40 && red==green && green==blue;
            }
            if(after) {
                const auto red=unsigned(yellow.rgba8[pixel]);
                const auto green=unsigned(yellow.rgba8[pixel+1]);
                const auto blue=unsigned(yellow.rgba8[pixel+2]);
                const std::array rgb{red,green,blue};
                for(std::size_t channel=0;channel<rgb.size();++channel) {
                    yellow_total[channel]+=rgb[channel];yellow_max[channel]=std::max(yellow_max[channel],rgb[channel]);
                }
                yellow_pixels+=red>20 && green>20 && green>=red*.65 && blue<red*.2;
                // Require the actual selected tint's chromatic ratio, allowing
                // quantization and filtered masked edges over gray background.
                yellow_chromatic+=red>40 && std::abs(double(green)/red-.85)<.035 &&
                    blue==0;
                yellow_red+=red>20 && red>green*1.8 && red>blue*1.8;
            }
        }
        // The white control must expose actual gray visible texels, and the
        // selected tint must change them without changing the masked footprint.
        CHECK(white_gray_pixels>4);CHECK(color_changes>4);
        CHECK(darker_pixels==color_changes);CHECK(nonincreasing_intensity);CHECK(same_red);
        CHECK(yellow_pixels>4);CHECK(yellow_chromatic>4);CHECK(yellow_red==0);
        CHECK(yellow.rgba8!=source_tinted.rgba8);CHECK(same_coverage);CHECK(same_alpha);
        CHECK(covered>4);CHECK(background>neutral.rgba8.size()/8);
        CHECK(draw().rgba8==yellow.rgba8);
        REQUIRE(source_covered>0);REQUIRE(covered>0);REQUIRE(source_total[0]>0);REQUIRE(yellow_total[0]>0);
        CHECK(yellow_total[0]+yellow_total[1]+yellow_total[2]<source_total[0]+source_total[1]+source_total[2]);
        CHECK(yellow_total[2]==0);
        std::cout<<"snark-blood-gpu "<<blood_names[index]<<" color_changes="<<color_changes
            <<" darker_pixels="<<darker_pixels<<" white_gray="<<white_gray_pixels
            <<" nonincreasing_intensity="<<nonincreasing_intensity<<" same_red="<<same_red
            <<" yellow="<<yellow_pixels<<" yellow_chromatic="<<yellow_chromatic<<" yellow_red="<<yellow_red<<" covered="<<covered
            <<" background="<<background<<" same_coverage="<<same_coverage<<" same_alpha="<<same_alpha<<'\n';
        std::cout<<"snark-blood-gpu-color "<<blood_names[index]
            <<" source_mean_rgb="<<double(source_total[0])/source_covered<<","<<double(source_total[1])/source_covered<<","<<double(source_total[2])/source_covered
            <<" source_max_rgb="<<source_max[0]<<","<<source_max[1]<<","<<source_max[2]
            <<" source_mean_g_over_r="<<double(source_total[1])/source_total[0]
            <<" source_max_r_over_g="<<source_max_red_green<<" source_max_g_over_r="<<source_max_green_red
            <<" yellow_mean_rgb="<<double(yellow_total[0])/covered<<","<<double(yellow_total[1])/covered<<","<<double(yellow_total[2])/covered
            <<" yellow_max_rgb="<<yellow_max[0]<<","<<yellow_max[1]<<","<<yellow_max[2]
            <<" yellow_mean_g_over_r="<<double(yellow_total[1])/yellow_total[0]
            <<" yellow_mean_b_over_r="<<double(yellow_total[2])/yellow_total[0]<<'\n';
        scene.transient_sprite_entities.reset();CHECK(draw().rgba8==neutral.rgba8);
    }
    GLboolean depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);
    CHECK(depth==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    gl->release_renderer();
}

TEST_CASE("E27 opt in silent normal SDL playback backend starts without a game session",
    "[e27][snark-assets][audio-device][optional-local]") {
    if(!opted_in(approved_snark_root()))SKIP("Exact approved read-only research root opt-in absent");
    if(!SDL_getenv("HLCLIENT_AUDIO_DEVICE_TEST"))SKIP("Explicit audio-device startup opt-in absent");
    hlclient::audio::Playback playback{true};
    if(playback.status()!="playback_ready")SKIP("SDL playback unavailable");
    playback.set_listener({{}, {0,-1,0},0,true});
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    const auto statistics=playback.statistics();
    CHECK(statistics.queue_failures==0);CHECK(statistics.mixer.started==0);
    CHECK(statistics.queued_frames<=960);
    std::cout<<"snark-playback ready=1 silent=1 frames="<<statistics.mixer.frames
        <<" queued="<<statistics.queued_frames<<'\n';
}
