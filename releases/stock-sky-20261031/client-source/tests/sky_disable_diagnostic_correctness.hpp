#pragma once
// Test-only synthetic diagnostics. Intentionally different sky colors are never
// reported as a quality-preserving optimization. No stencil observer/state.
namespace sky_disable_diagnostic {
bool disabled(){const auto p=hlc_background_sky_policy();return p==BackgroundSkyPolicy::no_background || p==BackgroundSkyPolicy::no_sky_color;}
bool constant(){return hlc_background_sky_policy()==BackgroundSkyPolicy::no_sky_color;}
struct Color {std::uint8_t r,g,b;};
bool is_color(const Samples& samples,std::size_t index,Color value){return samples.color[index*4]==value.r && samples.color[index*4+1]==value.g && samples.color[index*4+2]==value.b;}
void neutral() {
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDepthRange(0,1);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glDisable(GL_COLOR_LOGIC_OP);glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);glDisable(GL_SAMPLE_COVERAGE);glDisable(GL_SAMPLE_MASK);
    glDisable(GL_RASTERIZER_DISCARD);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
}
r::RenderScene benchmark_scene(std::string_view name,unsigned samples) {
    auto scene=scene_for(name,samples,false);
    if(name=="room")scene.static_world->package=package({Quad{-8,8,-8,8,0,false,false,85},Quad{-1.5F,1.5F,-1.5F,1.5F,1,false,true,224}});
    else scene.static_world->package=package({Quad{-.5F,.5F,-.5F,.5F,0,false,false,90},Quad{-1.5F,1.5F,-1.5F,1.5F,1,false,true,224}});
    return scene;
}
struct Definition {const char* name;bool finite{};std::vector<Color> foreground;};
const std::vector<Definition>& definitions(){static const std::vector<Definition> cases{
    {"room"},{"open"},{"masked_edges"},{"portal_front",true,{{48,96,40}}},{"portal_behind",true,{{48,96,40}}},
    {"far_plane"},{"far_rounding"},{"depth_equal_clear"},{"overlap_ties"},{"runtime_brushes"},{"translucent_studio"},
    {"decals"},{"effects"},{"water"},{"no_sky"},{"resource_replacement"},{"roll_aspect"},{"finite_sky_only",true},
    {"finite_sky_masked_front",true,{{180,96,40}}},{"finite_sky_brush",true,{{210,65,180}}},{"finite_missing_cube_opaque",true},{"finite_missing_cube_masked",true},
    {"finite_alpha_to_coverage",true},{"finite_missing_cube_masked_alpha_to_coverage",true}};return cases;}
r::RenderScene scene_for_case(std::string_view name,unsigned samples) {
    if(name=="finite_alpha_to_coverage" || name=="finite_missing_cube_masked_alpha_to_coverage") {
        auto scene=scene_for_case(name=="finite_alpha_to_coverage"?"finite_sky_only":"finite_missing_cube_masked",samples);
        // A masked source with an available cubemap still emits alpha1 in the
        // original finite-sky branch, but prevents flat-sky clear override.
        if(name=="finite_alpha_to_coverage")scene.static_world->package=package({Quad{-1.127F,.783F,-.793F,.917F,0,true,true,224}});
        scene.clear_color.alpha=.375F;return scene;}
    if(name=="overlap_ties") {auto scene=scene_for("open",samples,true);scene.static_world->package=package({
        Quad{-.8F,.8F,-.8F,.8F,.25F,false,false,160},Quad{-.6F,.6F,-.6F,.6F,0,false,false,110},
        Quad{-.4F,.4F,-.4F,.4F,0,false,false,220},Quad{-.2F,.2F,-.2F,.2F,-.25F,false,false,35}});return scene;}
    if(name=="depth_equal_clear") {auto scene=scene_for("room",samples,true);scene.static_world->package=package({Quad{-8,8,-8,8,0,false,false,200}});return scene;}
    if(name=="resource_replacement")return scene_for("replacement",samples,true);
    if(name=="finite_sky_only" || name=="finite_sky_brush" || name=="finite_missing_cube_opaque" || name=="finite_missing_cube_masked") {
        auto scene=scene_for("open",samples,true);scene.static_world->package=package({Quad{-1.127F,.783F,-.793F,.917F,0,name=="finite_missing_cube_masked",true,224}});
        if(name=="finite_sky_brush")sky_advanced::add_runtime_brush(scene);
        if(name.starts_with("finite_missing_cube"))scene.skybox.reset();return scene;
    }
    if(name=="finite_sky_masked_front") {auto scene=scene_for("open",samples,true);scene.static_world->package=package({
        Quad{-1,1,-1,1,0,false,true,224},Quad{-.8F,.8F,-.8F,.8F,-.25F,true,false,180}});return scene;}
    return scene_for(name,samples,true);
}
struct Draw {std::string kind;GLenum mode{},type{};GLint first{};GLsizei count{};std::uintptr_t offset{};};
struct Contract {bool coverage_alpha{},masked_alpha_enabled{},sampler_cube_absent{},all_samplers_absent{},discard_present{};
    unsigned active_sampler_uniforms{},active_cube_sampler_uniforms{};std::vector<std::pair<std::string,GLenum>> uniforms;std::array<float,4> color{};};
struct DrawObserver {
    inline static DrawObserver* active{};PFNGLDRAWARRAYSPROC arrays=glad_glDrawArrays;PFNGLDRAWELEMENTSPROC elements=glad_glDrawElements;
    std::vector<Draw> order;std::vector<Contract> contracts;unsigned fullscreen{},finite_textured{},finite_constant{},non_sky{};
    const bool& tracking;bool missing_cube_finite{};
    DrawObserver(const bool& enabled,bool missing=false):tracking(enabled),missing_cube_finite(missing){need(!active,"Nested diagnostic draw observer");active=this;glad_glDrawArrays=&draw_arrays;glad_glDrawElements=&draw_elements;}
    ~DrawObserver(){glad_glDrawArrays=arrays;glad_glDrawElements=elements;active=nullptr;}
    static void GLAD_API_PTR draw_arrays(GLenum mode,GLint first,GLsizei count) {
        auto& self=*active;if(!self.tracking){self.arrays(mode,first,count);return;}{sky_fixture::CallCounter::Pause observer;
            GLint program{};glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            const bool fullscreen=glGetUniformLocation(static_cast<GLuint>(program),"u_cube")>=0 && mode==GL_TRIANGLES && first==0 && count==3;
            if(fullscreen)++self.fullscreen;
            else self.order.push_back({"arrays",mode,0,first,count,0});}
        self.arrays(mode,first,count);
    }
    static void GLAD_API_PTR draw_elements(GLenum mode,GLsizei count,GLenum type,const void* indices) {
        auto& self=*active;if(!self.tracking){self.elements(mode,count,type,indices);return;}{sky_fixture::CallCounter::Pause observer;
            GLint program{};glGetIntegerv(GL_CURRENT_PROGRAM,&program);const auto p=static_cast<GLuint>(program);
            const auto matrix=glGetUniformLocation(p,"u_model_view_projection"),clear=glGetUniformLocation(p,"u_sky_clear_color");
            bool finite=clear>=0 || (self.missing_cube_finite && matrix>=0);
            if(!finite){const auto sky=glGetUniformLocation(p,"u_sky_surface");if(sky>=0){GLint enabled{};glGetUniformiv(p,sky,&enabled);finite=enabled!=0;}}
            std::string kind="other-indexed";
            if(clear>=0) {
                ++self.finite_constant;kind="world:finite";Contract contract;
                contract.coverage_alpha=glGetUniformLocation(p,"u_base_texture")>=0;glGetUniformfv(p,clear,contract.color.data());
                const auto masked=glGetUniformLocation(p,"u_masked_alpha");if(masked>=0){GLint value{};glGetUniformiv(p,masked,&value);contract.masked_alpha_enabled=value!=0;}
                GLint uniform_count{};glGetProgramiv(p,GL_ACTIVE_UNIFORMS,&uniform_count);need(uniform_count>=2 && uniform_count<=8,"Unexpected finite constant active-uniform inventory");
                for(GLuint i=0;i<static_cast<GLuint>(uniform_count);++i) {
                    GLchar name[256]{};GLsizei length{};GLint size{};GLenum uniform_type{};glGetActiveUniform(p,i,256,&length,&size,&uniform_type,name);
                    need(length>0 && length<255 && size==1,"Unsupported constant active-uniform shape");contract.uniforms.emplace_back(std::string(name,length),uniform_type);
                    const bool cube=uniform_type==GL_SAMPLER_CUBE || uniform_type==GL_SAMPLER_CUBE_SHADOW || uniform_type==GL_INT_SAMPLER_CUBE || uniform_type==GL_UNSIGNED_INT_SAMPLER_CUBE;
                    if(cube)++contract.active_cube_sampler_uniforms;
                    if(uniform_type==GL_SAMPLER_2D || cube)++contract.active_sampler_uniforms;
                    need(uniform_type==GL_FLOAT_MAT4 || uniform_type==GL_FLOAT_VEC4 || (contract.coverage_alpha && (uniform_type==GL_SAMPLER_2D || uniform_type==GL_INT)),"Constant shader has an unexpected active sampler/type");
                }
                need(contract.active_cube_sampler_uniforms==0 && contract.active_sampler_uniforms==(contract.coverage_alpha?1U:0U),"Linked finite constant sampler reflection violates contract");
                GLint shader_count{};glGetProgramiv(p,GL_ATTACHED_SHADERS,&shader_count);need(shader_count==2,"Constant program missing actual attached stages");
                GLuint shaders[2]{};glGetAttachedShaders(p,2,nullptr,shaders);std::string source;
                for(const auto shader:shaders){GLint size{};glGetShaderiv(shader,GL_SHADER_SOURCE_LENGTH,&size);need(size>0 && size<16384,"Constant source unavailable/bound exceeded");
                    std::string stage(static_cast<std::size_t>(size),'\0');GLsizei written{};glGetShaderSource(shader,size,&written,stage.data());stage.resize(written);source+=stage;}
                contract.sampler_cube_absent=source.find("samplerCube")==std::string::npos;
                contract.all_samplers_absent=source.find("sampler")==std::string::npos;
                contract.discard_present=source.find("discard")!=std::string::npos;
                need(contract.sampler_cube_absent,"Constant finite sky still samples a cubemap");
                if(contract.coverage_alpha)need((!contract.masked_alpha_enabled || contract.discard_present) && source.find("sampler2D")!=std::string::npos,"Missing-cube masked coverage lost original alpha test");
                else need(contract.all_samplers_absent && !contract.discard_present && source.find("texture(")==std::string::npos,"Pure constant sky contains texture sampling or discard");
                need(source.find("gl_FragDepth")==std::string::npos && source.find("gl_SampleMask")==std::string::npos,"Constant shader changes depth/sample outputs");
                self.contracts.push_back(contract);
            } else if(matrix>=0) {if(finite){++self.finite_textured;kind="world:finite";}else{++self.non_sky;kind="world:opaque";}}
            self.order.push_back({std::move(kind),mode,type,0,count,reinterpret_cast<std::uintptr_t>(indices)});}
        self.elements(mode,count,type,indices);
    }
};
void order_json(std::ostream& out,const std::vector<Draw>& order) {
    out<<'[';bool comma{};for(const auto& d:order){if(comma)out<<',';comma=true;out<<"{\"kind\":"<<json(d.kind)<<",\"mode\":"<<d.mode<<",\"type\":"<<d.type<<",\"first\":"<<d.first<<",\"count\":"<<d.count<<",\"offset\":"<<d.offset<<'}';}out<<']';
}
std::vector<std::uint8_t> covered_mask(const Samples& samples) {
    std::vector<std::uint8_t> mask(samples.depth_bits.size());for(std::size_t i=0;i<mask.size();++i)
        mask[i]=samples.depth_bits[i]!=std::bit_cast<std::uint32_t>(1.0F) || is_color(samples,i,{200,96,40});return mask;
}
std::vector<std::uint8_t> opaque_mask(const Samples& samples,const Definition& definition,const std::vector<std::uint8_t>& covered) {
    if(!definition.finite)return covered;std::vector<std::uint8_t> mask(covered.size());
    for(std::size_t i=0;i<mask.size();++i)for(const auto color:definition.foreground)if(is_color(samples,i,color))mask[i]=1;return mask;
}
void masked_samples(const Path& directory,std::string_view boundary,std::string_view label,const Samples& samples,const std::vector<std::uint8_t>& mask) {
    need(samples.color.size()==mask.size()*4,"Diagnostic color mask shape mismatch");std::vector<std::uint8_t> selected(samples.color.size());
    for(std::size_t i=0;i<mask.size();++i)if(mask[i])std::copy_n(samples.color.begin()+static_cast<std::ptrdiff_t>(i*4),4,selected.begin()+static_cast<std::ptrdiff_t>(i*4));
    binary(directory/(std::string(boundary)+"."+std::string(label)+".mask.samples.bin"),mask);
    binary(directory/(std::string(boundary)+"."+std::string(label)+".rgba8.samples.bin"),selected);
}
void masked_resolved(const Path& directory,std::string_view label,const Samples& samples,const std::vector<std::uint8_t>& mask,const std::vector<std::uint8_t>& resolved) {
    const auto pixels=resolved.size()/4;need(mask.size()==pixels*samples.samples,"Resolved safe mask shape mismatch");
    std::vector<std::uint8_t> pixel_mask(pixels,1),selected(resolved.size());
    for(std::size_t p=0;p<pixels;++p){for(unsigned n=0;n<samples.samples;++n)if(!mask[p+pixels*n])pixel_mask[p]=0;
        if(pixel_mask[p])std::copy_n(resolved.begin()+static_cast<std::ptrdiff_t>(p*4),4,selected.begin()+static_cast<std::ptrdiff_t>(p*4));}
    binary(directory/(std::string("final-default.")+std::string(label)+".mask.bin"),pixel_mask);
    binary(directory/(std::string("final-default.")+std::string(label)+".rgba8.bin"),selected);
}
void run(const Options& options,gl::OpenGlRenderer& gpu) {
    need(hlc_background_sky_policy()!=BackgroundSkyPolicy::depth,"Diagnostic suite does not test the historical depth policy");
    Extractor extractor;std::ofstream out(options.output/L"diagnostic-correctness.json");out<<std::setprecision(17);
    out<<"{\"schema\":\"hlc.sky-disable.diagnostic-correctness.v1\",\"status\":\"ARTIFACTS_REQUIRE_CONTRACT_COMPARISON\",\"policy\":"<<json(background_sky_policy_name(hlc_background_sky_policy()))<<',';hardware(out,gpu);
    out<<",\"color_semantics\":\"Sky intentionally differs. Compare covered masks/colors for no_background; non-sky opaque masks/colors for no_sky_color; exact all-sample depth for both. Resolved comparison requires every sample safe.\","
        "\"fixture_settings\":{\"dither\":false,\"framebuffer_srgb\":false},\"case_count\":48,\"cases\":[";
    bool comma{};
    for(const auto samples:std::array<unsigned,2>{0,4})for(const auto& definition:definitions()) {
        const std::string_view name=definition.name;neutral();auto scene=scene_for_case(name,samples);const int w=name=="roll_aspect"?127:96,h=96;
        const bool missing_cube_finite=name.starts_with("finite_missing_cube");
        const bool alpha_coverage=name.ends_with("alpha_to_coverage");if(alpha_coverage)glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        if(name=="depth_equal_clear")glDepthRange(1,1);
        gpu.render(scene,{w,h});
        if(name=="resource_replacement"){scene.skybox=sky(2);scene.static_world->package=package({Quad{-.3F,.7F,-.4F,.8F,0,false,false,137}});}
        hlc_background_sky_reset_stats();const auto before=gpu.statistics();const auto entities_before=gpu.entity_statistics();const auto decals_before=gpu.decal_statistics();
        const auto directory=options.output/(std::string(name)+"-msaa"+std::to_string(samples));std::filesystem::create_directory(directory);
        GLuint source{};Samples world;sky_fixture::Calls calls;std::vector<Draw> draw_order;std::vector<Contract> contracts;unsigned fullscreen{},finite_textured{},finite_constant{},non_sky{};
        {sky_fixture::CallCounter counter;counter.tracking=true;DrawObserver draws(counter.tracking,missing_cube_finite);ClearHook hook(extractor,w,h,samples);gpu.render(scene,{w,h});counter.tracking=false;
            if(hook.error)std::rethrow_exception(hook.error);need(hook.world_count==1 && hook.world,"Missing pre-viewmodel depth/color checkpoint");
            source=hook.source;world=std::move(*hook.world);calls=counter.count;draw_order=std::move(draws.order);contracts=std::move(draws.contracts);
            fullscreen=draws.fullscreen;finite_textured=draws.finite_textured;finite_constant=draws.finite_constant;non_sky=draws.non_sky;}
        const auto final=extractor.capture(source,w,h,samples);const auto observed=gpu.observe_framebuffer({w,h},scene.clear_color,true);
        need(observed && gpu.scene_target_statistics().actual_samples==samples,"Diagnostic silently changed target/sample quality");
        save_samples(directory,"world-before-viewmodel",world);save_samples(directory,"final-scene",final);binary(directory/L"final-default.rgba8.bin",observed.rgba8);
        auto covered=covered_mask(world),opaque=opaque_mask(world,definition,covered);auto final_covered=covered,final_opaque=opaque;
        for(std::size_t i=0;i<final.depth_bits.size();++i)if(final.depth_bits[i]!=std::bit_cast<std::uint32_t>(1.0F)){final_covered[i]=1;final_opaque[i]=1;}
        masked_samples(directory,"world-before-viewmodel","covered",world,covered);masked_samples(directory,"world-before-viewmodel","non-sky",world,opaque);
        masked_samples(directory,"final-scene","covered",final,final_covered);masked_samples(directory,"final-scene","non-sky",final,final_opaque);
        masked_resolved(directory,"covered",final,final_covered,observed.rgba8);masked_resolved(directory,"non-sky",final,final_opaque,observed.rgba8);
        const auto stats=hlc_background_sky_stats();const auto& legacy=hlc_background_sky_diagnostics();
        need(stats.frames==1 && stats.late_frames==0 && stats.bounds_tested==0 && stats.gl_state_queries==0 && stats.fallback_bits==0 &&
            legacy.guard_ns==0 && legacy.prepare_ns==0 && legacy.range_tests==0 && legacy.prepared_frames==0,"Diagnostic/original entered an experimental depth guard");
        const bool sky_available=scene.skybox!=nullptr;
        need(stats.fullscreen_sky_draws==(disabled()?0U:sky_available?1U:0U) && stats.fullscreen_sky_skipped_frames==(disabled() && sky_available?1U:0U),"Fullscreen skip/draw contract violated");
        need(fullscreen==stats.fullscreen_sky_draws && finite_constant==stats.finite_sky_constant_draws && non_sky==stats.non_sky_world_draws,"Actual GL draws differ from diagnostic counters");
        need(finite_textured==stats.finite_sky_textured_draws,"Textured finite-sky draw counter mismatch");
        need(constant()?stats.finite_sky_textured_draws==0:stats.finite_sky_constant_draws==0,"Wrong finite-sky color path executed");
        need(definition.finite?(stats.finite_sky_textured_draws+stats.finite_sky_constant_draws)>0:(stats.finite_sky_textured_draws+stats.finite_sky_constant_draws)==0,"Finite geometry path was not actually exercised as declared");
        need(stats.finite_sky_coverage_alpha_draws==std::count_if(contracts.begin(),contracts.end(),[](const auto& c){return c.coverage_alpha;}),"Coverage-only alpha subset counter mismatch");
        if(alpha_coverage)need(glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) && stats.last_frame_clear_color[3]==.375F,"A2C/clear-alpha control was normalized away");
        // The accepted legacy counter includes decal arrays in FrameDrawCounters;
        // diagnostic world-range counters intentionally count package draws only.
        // Reconcile both inventories without dropping the real decal pass.
        const auto legacy_world_plus_decals=gpu.statistics().draw_call_count-before.draw_call_count;
        const auto decal_draws=gpu.decal_statistics().rendered_batch_count-decals_before.rendered_batch_count;
        const auto world_draws=stats.last_frame_world_draws;
        need(legacy_world_plus_decals==world_draws+decal_draws &&
            stats.non_sky_world_draws+stats.finite_sky_textured_draws+stats.finite_sky_constant_draws==world_draws &&
            non_sky+finite_textured+finite_constant==world_draws,"World/package/decal draw inventories do not reconcile");
        for(const auto& contract:contracts)for(std::size_t i=0;i<4;++i)need(contract.color[i]==stats.last_frame_clear_color[i],"Finite constant does not use actual selected clear color");
        std::array<std::uint8_t,4> clear{};for(std::size_t i=0;i<4;++i)clear[i]=static_cast<std::uint8_t>(std::lround(std::clamp(stats.last_frame_clear_color[i],0.0F,1.0F)*255));
        std::size_t clear_samples{},constant_samples{},object_samples{};
        for(std::size_t i=0;i<world.depth_bits.size();++i) {
            const bool same=std::equal(clear.begin(),clear.end(),world.color.begin()+static_cast<std::ptrdiff_t>(i*4));
            if(!covered[i] && same)++clear_samples;
            if(definition.finite && covered[i] && !opaque[i] && std::equal(clear.begin(),clear.begin()+3,world.color.begin()+static_cast<std::ptrdiff_t>(i*4)))++constant_samples;
            if(is_color(world,i,{48,96,40}))++object_samples;
        }
        if(disabled() && (name=="open" || name=="finite_sky_only"))need(clear_samples>0,"Skipped background did not expose actual clear RGB");
        if(constant() && definition.finite)need(constant_samples>0,"Finite constant color did not actually paint coverage");
        if(name=="portal_front")need(object_samples>0,"Object in front of finite sky was not visible");
        if(name=="portal_behind")need(object_samples==0,"Object behind finite sky leaked through depth occlusion");
        if(name=="runtime_brushes" || name=="finite_sky_brush")need(gpu.statistics().runtime_brush_submitted_count-before.runtime_brush_submitted_count==2,"Both runtime brushes were not actually submitted");
        if(name=="decals")need(gpu.decal_statistics().rendered_batch_count-decals_before.rendered_batch_count==1,"Decal pass not executed");
        need(gpu.entity_statistics().studio_draw_count-entities_before.studio_draw_count==(name=="translucent_studio"?3U:1U),"Real Studio/viewmodel pass missing");
        if(name=="masked_edges" || name.starts_with("finite_missing_cube_masked")) {
            const auto pixels=static_cast<std::size_t>(w)*h;bool mixed{};std::size_t holes{},survivors{};
            for(unsigned n=1;n<world.samples;++n)if(!std::equal(world.depth_bits.begin(),world.depth_bits.begin()+static_cast<std::ptrdiff_t>(pixels),world.depth_bits.begin()+static_cast<std::ptrdiff_t>(pixels*n)))mixed=true;
            for(unsigned n=0;n<world.samples;++n)for(int y=h/3;y<2*h/3;++y)for(int x=w/3;x<2*w/3;++x)
                if(world.depth_bits[pixels*n+static_cast<std::size_t>(y)*w+x]==std::bit_cast<std::uint32_t>(1.0F))++holes;else ++survivors;
            need(holes>0 && survivors>0,"Masked polygon lost holes/survivors");if(samples==4)need(mixed,"Masked fixture lost partial MSAA samples");
        }
        gl_ok("Diagnostic correctness left GL errors");if(comma)out<<',';comma=true;
        out<<"{\"name\":"<<json(name)<<",\"width\":"<<w<<",\"height\":"<<h<<",\"actual_samples\":"<<samples<<",\"alpha_to_coverage\":"<<(alpha_coverage?"true":"false")<<",\"finite_sky_fixture\":"<<(definition.finite?"true":"false")<<",\"sky_available\":"<<(sky_available?"true":"false")<<",\"candidate\":";stats_json(out,stats);
        out<<",\"world_samples\":";sample_summary(out,world);out<<",\"final_samples\":";sample_summary(out,final);
        out<<",\"world_covered_mask_samples\":"<<std::count(covered.begin(),covered.end(),1)<<",\"world_non_sky_mask_samples\":"<<std::count(opaque.begin(),opaque.end(),1)
            <<",\"actual_clear_background_samples\":"<<clear_samples<<",\"actual_constant_finite_samples\":"<<constant_samples<<",\"front_object_samples\":"<<object_samples
            <<",\"actual_draw_deltas\":{\"world_draws\":"<<world_draws<<",\"legacy_world_plus_decal_draws\":"<<legacy_world_plus_decals<<",\"triangles\":"<<gpu.statistics().triangle_count-before.triangle_count<<",\"brush_draws\":"<<gpu.statistics().brush_draw_call_count-before.brush_draw_call_count
            <<",\"brush_instances\":"<<gpu.statistics().runtime_brush_submitted_count-before.runtime_brush_submitted_count<<",\"studio_draws\":"<<gpu.entity_statistics().studio_draw_count-entities_before.studio_draw_count<<",\"decal_batches\":"<<gpu.decal_statistics().rendered_batch_count-decals_before.rendered_batch_count<<"},\"geometry_draw_order_excluding_fullscreen\":";order_json(out,draw_order);
        out<<",\"constant_program_contracts\":[";bool ccomma{};for(const auto& contract:contracts){if(ccomma)out<<',';ccomma=true;out<<"{\"coverage_alpha_only\":"<<(contract.coverage_alpha?"true":"false")<<",\"masked_alpha_enabled\":"<<(contract.masked_alpha_enabled?"true":"false")<<",\"sampler_cube_absent\":"<<(contract.sampler_cube_absent?"true":"false")<<",\"all_samplers_absent\":"<<(contract.all_samplers_absent?"true":"false")<<",\"discard_present\":"<<(contract.discard_present?"true":"false")
                <<",\"active_sampler_uniforms\":"<<contract.active_sampler_uniforms<<",\"active_cube_sampler_uniforms\":"<<contract.active_cube_sampler_uniforms<<",\"active_uniforms\":[";
            bool ucomma{};for(const auto& uniform:contract.uniforms){if(ucomma)out<<',';ucomma=true;out<<"{\"name\":"<<json(uniform.first)<<",\"type\":"<<uniform.second<<'}';}out<<"]}";}
        out<<"],\"render_gl_calls_excluding_readback\":";sky_fixture::json(out,calls);out<<'}';
    }
    out<<"],\"thresholds\":\"zero depth/mask/selected-color/draw-order differences; sky-color differences intentional, never globally ignored for quality acceptance\",\"target_performance\":\"NOT_MEASURED\"}";need(bool(out),"Diagnostic manifest write failed");
}
} // namespace sky_disable_diagnostic
