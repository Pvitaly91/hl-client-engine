#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/core/presentation_settings.hpp>
#include <hlclient/app/application_presentation_settings.hpp>
#include <hlclient/app/external_session.hpp>
#include <hlclient/audio/mixer.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/renderer/render_camera_math.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace {
namespace core=hlclient::core;
namespace platform=hlclient::platform;
namespace render=hlclient::renderer;
namespace fixture=hlclient::tests::entity_opengl_fixture;

core::CommandLineOptions launch_options(const core::ExternalLaunchConfiguration& supplied) {
    const auto encoded=core::encode_external_launch_configuration(supplied);REQUIRE(encoded);
    const auto decoded=core::decode_external_launch_configuration(*encoded);REQUIRE(decoded);
    const std::array<std::string_view,8> argv{
        "--external-config",R"(C:\E31 test\session.json)",
        "--session-receipt-root",R"(C:\E31 test\run)",
        "--session-run-id","0123456789abcdef0123456789abcdef","--session-role","A"};
    auto parsed=core::parse_command_line(argv);REQUIRE(parsed);
    REQUIRE(hlclient::app::apply_external_launch_configuration(*decoded.configuration,*parsed.options));
    CHECK(parsed.options->external_server_client);
    CHECK(parsed.options->video==supplied.video);
    CHECK(parsed.options->image==supplied.image);
    CHECK(parsed.options->hud_scale==supplied.hud_scale);
    CHECK(parsed.options->audio_volume==supplied.volume);
    CHECK(parsed.options->renderer==core::RendererBackend::opengl);
    return std::move(*parsed.options);
}

auto read(fixture::HiddenContext& context,const render::RenderScene& scene,
        render::RenderExtent extent={96,96}) {
    context.renderer().render(scene,extent);
    auto result=context.renderer().observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(result);REQUIRE(glGetError()==GL_NO_ERROR);
    return result.rgba8;
}

std::array<std::uint8_t,4> pixel(const std::vector<std::uint8_t>& pixels,
        int x,int y,render::RenderExtent extent={96,96}) {
    const auto index=(static_cast<std::size_t>(extent.height-1-y)*extent.width+x)*4U;
    REQUIRE(index+3U<pixels.size());
    return {pixels[index],pixels[index+1],pixels[index+2],pixels[index+3]};
}

void expected_adjustment(const std::array<std::uint8_t,4>& before,
        const std::array<std::uint8_t,4>& after,const core::ImageAdjustments& settings) {
    const auto expected=core::adjusted_image_color(
        {before[0]/255.0F,before[1]/255.0F,before[2]/255.0F,before[3]/255.0F},settings);
    for(std::size_t c=0;c<3;++c)
        CHECK(std::abs(static_cast<float>(after[c])-255.0F*expected[c])<=2.0F);
    CHECK(after[3]==before[3]);
}

TEST_CASE("E31 neutral image settings validate finite independent controls", "[e31][video][no-stock]") {
    CHECK(core::valid_image_adjustments({}));
    CHECK(core::valid_image_adjustments({-.5F,.25F,0}));
    CHECK(core::valid_image_adjustments({.5F,4,2}));
    for(const float invalid:{std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        CHECK_FALSE(core::valid_image_adjustments({invalid,1,1}));
        CHECK_FALSE(core::valid_image_adjustments({0,invalid,1}));
        CHECK_FALSE(core::valid_image_adjustments({0,1,invalid}));
    }
    CHECK_FALSE(core::valid_image_adjustments({0,0,1}));
    CHECK_FALSE(core::valid_image_adjustments({.51F,1,1}));
    CHECK_FALSE(core::valid_image_adjustments({0,1,2.1F}));
    const std::array<float,4> source{.25F,.5F,.75F,.37F};
    CHECK(core::adjusted_image_color(source,{})==source);
    CHECK(core::adjusted_image_color(source,{.1F,1,1})[0]==Catch::Approx(.35F));
    CHECK(core::adjusted_image_color(source,{0,2,1})[0]==Catch::Approx(.5F));
    CHECK(core::adjusted_image_color(source,{0,1,1.5F})[0]==Catch::Approx(.125F));
    CHECK(core::adjusted_image_color(source,{.1F,2,1.5F})[3]==source[3]);
    render::null::NullRenderer renderer;renderer.initialize();render::RenderScene scene;
    scene.image_adjustments.gamma=0;
    CHECK_THROWS_AS(renderer.render(scene,{96,96}),std::invalid_argument);
    CHECK(renderer.statistics().rendered_frames==0);
}

TEST_CASE("E31 launcher JSON CLI application config reaches real SDL renderer before HUD", "[e31][video][launcher][opengl][no-stock]") {
    core::ExternalLaunchConfiguration config;
    config.video={0,core::VideoWindowMode::windowed,640,480,false};
    config.image={.12F,1.8F,1.3F};config.volume=25;
    const auto options=launch_options(config);
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime=std::make_unique<platform::SdlRuntime>();
        window=std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{
            "E31 configured output",options.video.width,options.video.height,true,
            options.video.display_index,options.video.mode,options.video.vsync});
    }catch(const std::exception& error) {
        if(platform::classify_opengl_startup_capability_failure(error)!=platform::OpenGlStartupCapabilityFailure::none)
            SKIP("SDL/OpenGL startup capability unavailable: "<<error.what());
        throw;
    }
    const auto actual=window->video_state();
    REQUIRE(actual.window_extent==platform::PixelExtent{640,480});
    CHECK(actual.requested==options.video);CHECK_FALSE(actual.vsync);
    const render::RenderExtent extent{actual.framebuffer_extent.width,actual.framebuffer_extent.height};
    render::opengl::OpenGlRenderer renderer;
    auto world=hlclient::tests::world_render_fixture::make_package();REQUIRE(world);
    render::RenderScene scene;
    scene.clear_color={.2F,.3F,.4F,.5F};
    scene.static_world.emplace();scene.static_world->package=std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*world.package));
    scene.camera.position={8,8,32};scene.camera.target={8,8,0};scene.camera.up={0,1,0};
    scene.basic_hud.emplace();scene.basic_hud->rectangles.push_back({10,10,20,20,{.2F,.3F,.4F,1}});
    const auto read_scene=[&] {
        renderer.render(scene,extent);
        auto observation=renderer.observe_framebuffer(extent,scene.clear_color,true);
        REQUIRE(observation);REQUIRE(glGetError()==GL_NO_ERROR);
        return observation.rgba8;
    };
    auto neutral_config=config;neutral_config.image={};
    const auto neutral_options=launch_options(neutral_config);
    hlclient::app::apply_application_scene_settings(scene,neutral_options);
    const auto baseline=read_scene();
    hlclient::app::apply_application_scene_settings(scene,options);
    const auto adjusted=read_scene();
    const auto center=pixel(baseline,extent.width/2,extent.height/2,extent);
    CHECK(center[0]<20U); // sampled lit world surface, not a request counter
    expected_adjustment(center,pixel(adjusted,extent.width/2,extent.height/2,extent),config.image);
    CHECK(pixel(baseline,15,15,extent)==pixel(adjusted,15,15,extent));
    CHECK(adjusted!=baseline);
    CHECK(renderer.statistics().upload_count==1);
    CHECK(renderer.image_adjustment_statistics().adjusted_frames==1);
    hlclient::app::apply_application_scene_settings(scene,neutral_options);
    CHECK(read_scene()==baseline);
}

TEST_CASE("E31 launcher config volume reaches existing isolated production PCM", "[e31][video][launcher][audio][no-stock]") {
    auto asset=std::make_shared<hlclient::assets::AudioAsset>();
    asset->sample_rate=hlclient::audio::output_rate;asset->channel_count=1;
    asset->interleaved_samples={.5F,-.25F,.125F,0};
    for(const unsigned percent:{0U,25U,50U,100U}) {
        core::ExternalLaunchConfiguration config;config.volume=percent;
        const auto options=launch_options(config);
        hlclient::audio::Mixer mixer;
        mixer.listener({{}, {0,-1,0},hlclient::app::application_audio_master_volume(options),false});
        hlclient::audio::Command command;command.voice=1;command.asset=asset;command.local=true;
        mixer.command(command);REQUIRE(mixer.statistics().started==1);
        std::array<float,8> mixed{};mixer.render(mixed);
        for(std::size_t frame=0;frame<asset->interleaved_samples.size();++frame) {
            const float expected=asset->interleaved_samples[frame]*static_cast<float>(percent)/100.0F;
            CHECK(std::isfinite(mixed[frame*2]));
            CHECK(mixed[frame*2]==Catch::Approx(expected));
            CHECK(mixed[frame*2+1]==Catch::Approx(expected));
        }
    }
}

TEST_CASE("E31 production OpenGL color pass is neutral and precedes opaque HUD", "[e31][video][opengl][no-stock]") {
    auto context=fixture::try_context();if(!context)SKIP("OpenGL startup capability unavailable");
    context->initialize_renderer();
    render::RenderScene scene;scene.clear_color={.25F,.5F,.75F,.6F};
    scene.basic_hud.emplace();
    scene.basic_hud->rectangles.push_back({10,10,20,20,{.2F,.3F,.4F,1}});
    scene.basic_hud->rectangles.push_back({46,47,5,1,{1,1,1,1}});
    scene.basic_hud->rectangles.push_back({48,45,1,5,{1,1,1,1}});
    const auto baseline=read(*context,scene);
    CHECK(read(*context,scene)==baseline);
    CHECK(context->renderer().image_adjustment_statistics().active_texture_count==0);
    for(const core::ImageAdjustments settings:std::array{
            core::ImageAdjustments{.15F,1,1},core::ImageAdjustments{0,2,1},
            core::ImageAdjustments{0,1,1.5F},core::ImageAdjustments{-.1F,1.7F,.8F}}) {
        scene.image_adjustments=settings;const auto corrected=read(*context,scene);
        expected_adjustment(pixel(baseline,80,80),pixel(corrected,80,80),settings);
        CHECK(pixel(baseline,15,15)==pixel(corrected,15,15));
        CHECK(pixel(baseline,48,47)==pixel(corrected,48,47));
        CHECK(pixel(baseline,50,47)==pixel(corrected,50,47));
        CHECK(pixel(baseline,48,49)==pixel(corrected,48,49));
    }
    CHECK(context->renderer().image_adjustment_statistics().active_texture_count==1);
    CHECK(context->renderer().image_adjustment_statistics().storage_reallocations==1);
    scene.image_adjustments={};CHECK(read(*context,scene)==baseline);
    scene.image_adjustments.gamma=std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(context->renderer().render(scene,{96,96}),std::invalid_argument);
}

TEST_CASE("E31 actual world color pass leaves geometry FOV and HUD after resize intact", "[e31][video][opengl][no-stock]") {
    auto context=fixture::try_context(160,120);if(!context)SKIP("OpenGL startup capability unavailable");
    context->initialize_renderer();
    auto package=hlclient::tests::world_render_fixture::make_package();REQUIRE(package);
    render::RenderScene scene;scene.clear_color={.1F,.2F,.3F,.5F};
    scene.basic_hud.emplace();
    scene.basic_hud->rectangles.push_back({10,10,20,20,{.2F,.3F,.4F,1}});
    scene.static_world.emplace();scene.static_world->package=std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*package.package));
    scene.camera.position={8,8,32};scene.camera.target={8,8,0};scene.camera.up={0,1,0};
    auto* native=SDL_GL_GetCurrentWindow();REQUIRE(native);
    for(const render::RenderExtent desired:std::array<render::RenderExtent,4>{{{96,96},{128,80},{112,96},{96,96}}}) {
        REQUIRE(SDL_SetWindowSize(native,desired.width,desired.height));REQUIRE(SDL_SyncWindow(native));
        render::RenderExtent actual;REQUIRE(SDL_GetWindowSizeInPixels(native,&actual.width,&actual.height));
        REQUIRE(actual.width>0);REQUIRE(actual.height>0);
        for(const float fov:{20.0F,40.0F,75.0F}) {
            scene.camera.vertical_field_of_view_radians=fov*3.14159265358979323846F/180.0F;
            scene.image_adjustments={};const auto baseline=read(*context,scene,actual);
            scene.image_adjustments={.07F,1.7F,1.2F};const auto corrected=read(*context,scene,actual);
            const auto center=pixel(baseline,actual.width/2,actual.height/2,actual);
            CHECK(center[0]<20U); // actual lit project-owned world, not just clear color
            expected_adjustment(center,pixel(corrected,actual.width/2,actual.height/2,actual),scene.image_adjustments);
            CHECK(pixel(baseline,15,15,actual)==pixel(corrected,15,15,actual));
            CHECK(pixel(baseline,29,29,actual)==pixel(corrected,29,29,actual));
            CHECK(context->renderer().statistics().last_extent==actual);
            CHECK(context->renderer().image_adjustment_statistics().last_extent==actual);
            CHECK(context->renderer().image_adjustment_statistics().active_texture_count==1);
        }
    }
    CHECK(context->renderer().image_adjustment_statistics().storage_reallocations==4);
    CHECK(context->renderer().statistics().upload_count==1); // sliders/resizes do not reimport assets
}

TEST_CASE("E31 scene color pass restores copy and ordinary render states", "[e31][video][opengl][no-stock]") {
    auto context=fixture::try_context();if(!context)SKIP("OpenGL startup capability unavailable");
    context->initialize_renderer();render::RenderScene scene;scene.clear_color={.2F,.3F,.4F,.5F};
    scene.image_adjustments={.05F,1.3F,.8F};
    glEnable(GL_DEPTH_TEST);glEnable(GL_BLEND);glEnable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_STENCIL_TEST);
    glActiveTexture(GL_TEXTURE3);glReadBuffer(GL_FRONT);
    context->renderer().render(scene,{96,96});
    CHECK(glIsEnabled(GL_DEPTH_TEST)==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_TRUE);
    CHECK(glIsEnabled(GL_CULL_FACE)==GL_TRUE);CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB)==GL_FALSE);
    GLint value{};glGetIntegerv(GL_ACTIVE_TEXTURE,&value);CHECK(value==GL_TEXTURE3);
    glGetIntegerv(GL_READ_BUFFER,&value);CHECK(value==GL_FRONT);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&value);CHECK(value==0);
    CHECK(glGetError()==GL_NO_ERROR);
}

TEST_CASE("E31 SDL window reports logical pixels display and completed resize", "[e31][video][sdl][no-stock]") {
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime=std::make_unique<platform::SdlRuntime>();
        window=std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{"E31 hidden video",128,96,true,0,core::VideoWindowMode::windowed,false});
    } catch(const std::exception& error) {
        if(platform::classify_opengl_startup_capability_failure(error)!=platform::OpenGlStartupCapabilityFailure::none)
            SKIP("SDL/OpenGL startup capability unavailable: "<<error.what());
        throw;
    }
    const auto displays=platform::enumerate_video_displays();REQUIRE_FALSE(displays.empty());
    const auto initial=window->video_state();
    CHECK(initial.requested.width==128);CHECK(initial.window_extent==platform::PixelExtent{128,96});
    CHECK(initial.framebuffer_extent==window->pixel_extent());
    CHECK(initial.actual_mode==core::VideoWindowMode::windowed);CHECK_FALSE(initial.vsync);
    CHECK(initial.display_scale>0);CHECK(initial.desktop_extent.width>0);
    CHECK(initial.display_content_scale==SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(SDL_GL_GetCurrentWindow())));
    CHECK(initial.pixel_density==SDL_GetWindowPixelDensity(SDL_GL_GetCurrentWindow()));
    CHECK(initial.pixel_density>0);
    int window_x{},window_y{};REQUIRE(SDL_GetWindowPosition(SDL_GL_GetCurrentWindow(),&window_x,&window_y));
    CHECK(initial.window_x==window_x);CHECK(initial.window_y==window_y);
    CHECK(initial.default_framebuffer_samples_observed);
    CHECK(initial.default_framebuffer_sample_buffers==0);CHECK(initial.default_framebuffer_samples==0);
    REQUIRE(SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),224,144));REQUIRE(SDL_SyncWindow(SDL_GL_GetCurrentWindow()));
    const auto resized=window->video_state();CHECK(resized.window_extent==platform::PixelExtent{224,144});
    CHECK(resized.framebuffer_extent==window->pixel_extent());
    CHECK(resized.requested.width==128); // requested and actual deliberately distinct
    platform::PlatformEvent event;bool resize_seen=false;
    while(window->poll_event(event))if(const auto* e=std::get_if<platform::WindowEvent>(&event))
        if(e->type==platform::WindowEventType::resized)resize_seen=true;
    CHECK(resize_seen);
}

TEST_CASE("E31.1 scene AA request never leaks into SDL default framebuffer attributes", "[e311][e31][video][sdl][no-stock]") {
    std::unique_ptr<platform::SdlRuntime> runtime;
    try {runtime=std::make_unique<platform::SdlRuntime>();}
    catch(const std::exception&error){if(platform::classify_opengl_startup_capability_failure(error)!=platform::OpenGlStartupCapabilityFailure::none)SKIP("SDL unavailable: "<<error.what());throw;}
    for(const unsigned samples:{0U,2U,4U,8U}) {
        std::unique_ptr<platform::SdlWindow> window;
        try {
            REQUIRE(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,1));
            REQUIRE(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,8));
            window=std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{
                "E31.1 native AA separation",128,96,true,0,core::VideoWindowMode::windowed,false,samples});
        } catch(const std::exception&error){if(platform::classify_opengl_startup_capability_failure(error)!=platform::OpenGlStartupCapabilityFailure::none)SKIP("OpenGL unavailable: "<<error.what());throw;}
        const auto actual=window->video_state();CHECK(actual.requested.msaa_samples==samples);
        REQUIRE(actual.default_framebuffer_samples_observed);
        CHECK(actual.default_framebuffer_sample_buffers==0);CHECK(actual.default_framebuffer_samples==0);
        CHECK(actual.framebuffer_extent==window->pixel_extent());
    }
}

TEST_CASE("E31 SDL unsupported exclusive and vanished display recover owned safe window", "[e31][video][sdl][no-stock]") {
    std::unique_ptr<platform::SdlRuntime> runtime;
    try {runtime=std::make_unique<platform::SdlRuntime>();}
    catch(const std::exception& error) {
        if(platform::classify_opengl_startup_capability_failure(error)!=platform::OpenGlStartupCapabilityFailure::none)
            SKIP("SDL startup capability unavailable: "<<error.what());
        throw;
    }
    const auto displays=platform::enumerate_video_displays();REQUIRE_FALSE(displays.empty());
    for(const auto mode:{core::VideoWindowMode::exclusive,core::VideoWindowMode::windowed}) {
        const auto index=mode==core::VideoWindowMode::exclusive ? 0U : 31U;
        if(mode==core::VideoWindowMode::windowed && displays.size()==32U)continue;
        platform::SdlWindow window{{"E31 safe recovery",800,600,true,index,mode,false}};
        const auto actual=window.video_state();CHECK(actual.safe_window_recovery);
        CHECK(actual.actual_mode==core::VideoWindowMode::windowed);
        CHECK(actual.recovery_reason==(mode==core::VideoWindowMode::exclusive ?
            "exclusive_mode_unsupported" : "selected_display_unavailable"));
        CHECK(actual.window_extent.width<=actual.desktop_extent.width);
        CHECK(actual.window_extent.height<=actual.desktop_extent.height);
    }
    if(displays[0].desktop_width<8192) {
        // The production external window is interactive. Hidden offscreen
        // fixtures deliberately do not participate in desktop-fit recovery.
        platform::SdlWindow oversized{{"E31 oversized recovery",8192,1024,false,0,core::VideoWindowMode::windowed,false}};
        const auto actual=oversized.video_state();CHECK(actual.safe_window_recovery);
        CHECK(actual.recovery_reason=="window_resolution_exceeds_display");
        CHECK(actual.actual_mode==core::VideoWindowMode::windowed);
        CHECK(actual.window_extent.width<=actual.desktop_extent.width);
    }
}

TEST_CASE("E31 preserves legacy hidden HD and ultrawide framebuffer extents",
          "[e31][video][sdl][no-stock]") {
    for(const auto requested:std::array<platform::PixelExtent,2>{{{1920,1080},{3440,1440}}}) {
        INFO(requested.width);INFO(requested.height);
        std::unique_ptr<platform::SdlRuntime> runtime;
        std::unique_ptr<platform::SdlWindow> window;
        try {
            runtime=std::make_unique<platform::SdlRuntime>();
            window=std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{
                "E31 legacy hidden framebuffer",requested.width,requested.height,true});
        }catch(const std::exception& error) {
            if(platform::classify_opengl_startup_capability_failure(error)!=platform::OpenGlStartupCapabilityFailure::none)
                SKIP("SDL/OpenGL startup capability unavailable: "<<error.what());
            throw;
        }
        const auto actual=window->video_state();
        CHECK(actual.requested.width==requested.width);
        CHECK(actual.requested.height==requested.height);
        CHECK(actual.window_extent==requested);
        CHECK(actual.framebuffer_extent==window->pixel_extent());
        // A high-DPI platform may provide more pixels, never shrink the
        // requested offscreen drawing region to fit the desktop work area.
        CHECK(actual.framebuffer_extent.width>=requested.width);
        CHECK(actual.framebuffer_extent.height>=requested.height);
        CHECK_FALSE(actual.safe_window_recovery);
        CHECK(actual.recovery_reason.empty());
    }
}

TEST_CASE("E31 SDL borderless applies desktop pixels or reports safe recovery", "[e31][video][sdl][no-stock]") {
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime=std::make_unique<platform::SdlRuntime>();
        window=std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{"E31 hidden borderless",800,600,true,0,core::VideoWindowMode::borderless,false});
    }catch(const std::exception& error) {
        if(platform::classify_opengl_startup_capability_failure(error)!=platform::OpenGlStartupCapabilityFailure::none)
            SKIP("SDL/OpenGL startup capability unavailable: "<<error.what());
        throw;
    }
    const auto actual=window->video_state();CHECK(actual.requested.width==800);
    if(actual.actual_mode==core::VideoWindowMode::borderless) {
        CHECK(actual.framebuffer_extent==actual.desktop_extent);
        CHECK_FALSE(actual.safe_window_recovery);
    }else {
        CHECK(actual.actual_mode==core::VideoWindowMode::windowed);
        CHECK(actual.safe_window_recovery);CHECK_FALSE(actual.recovery_reason.empty());
    }
}
} // namespace
