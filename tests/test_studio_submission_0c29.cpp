#include "entity_render/entity_opengl_test_support.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>

#ifndef HLCLIENT_CPU_STUDIO_COMMAND_UNIFORMS
#define HLCLIENT_CPU_STUDIO_COMMAND_UNIFORMS 0
#endif

namespace {
namespace f=hlclient::tests::entity_opengl_fixture;
namespace ef=hlclient::tests::entity_render_fixture;
namespace e=hlclient::entity_render;
namespace r=hlclient::renderer;
namespace a=hlclient::assets;
using Clock=std::chrono::steady_clock;

std::string setting(const char* key) {
#ifdef _MSC_VER
    char* value{};std::size_t size{};std::string result;
    if(_dupenv_s(&value,&size,key)==0 && value)result=value;
    std::free(value);return result;
#else
    const auto* value=std::getenv(key);return value?value:"";
#endif
}
std::uint64_t signature(const std::span<const std::byte> bytes) {
    std::uint64_t hash=14695981039346656037ULL;
    for(const auto byte:bytes){hash^=std::to_integer<unsigned char>(byte);hash*=1099511628211ULL;}
    return hash;
}
struct Counters {std::uint64_t uniforms{},programs{},arrays{},textures{},draws{},poses{};
    std::uint64_t draw_signature{14695981039346656037ULL};
    std::uint64_t uniform_signature{14695981039346656037ULL},pose_signature{14695981039346656037ULL};
    std::uint64_t material_uniforms{},unchanged_uniforms{},texture_dimension_uniforms{};
    std::uint64_t unchanged_uniform_signature{14695981039346656037ULL};
    std::uint64_t effective_uniform_signature{14695981039346656037ULL};};
using DrawWords=std::array<std::uint64_t,4>;
void mix(std::uint64_t& hash,const std::uint64_t word){hash^=word;hash*=1099511628211ULL;}
// Installed only for untimed call-count and exception controls. Timed samples
// use the driver's original dispatch, without a per-call counter/hash wrapper.
struct SubmissionProbe {
    inline static SubmissionProbe* current{};
    Counters counts;std::vector<DrawWords> draw_words;bool fail_draw{};GLint depth_target{};
    struct UniformLocations {GLuint program{};std::array<GLint,6> fields{};};
    std::vector<UniformLocations> locations;GLuint active_program{};
    PFNGLUNIFORM1FPROC one_f{glad_glUniform1f};PFNGLUNIFORM1IPROC one_i{glad_glUniform1i};
    PFNGLUNIFORM2FPROC two_f{glad_glUniform2f};PFNGLUNIFORM3FPROC three_f{glad_glUniform3f};
    PFNGLUNIFORM4FVPROC four_fv{glad_glUniform4fv};
    PFNGLUNIFORMMATRIX4FVPROC matrix{glad_glUniformMatrix4fv};
    PFNGLUSEPROGRAMPROC program{glad_glUseProgram};PFNGLBINDVERTEXARRAYPROC array{glad_glBindVertexArray};
    PFNGLBINDTEXTUREPROC texture{glad_glBindTexture};PFNGLDRAWELEMENTSPROC draw{glad_glDrawElements};
    PFNGLBUFFERSUBDATAPROC buffer{glad_glBufferSubData};PFNGLCLEARPROC clear{glad_glClear};
    SubmissionProbe(){REQUIRE(!current);current=this;
        GLint active{};glGetIntegerv(GL_CURRENT_PROGRAM,&active);active_program=static_cast<GLuint>(active);
        glad_glUniform1f=&u1f;glad_glUniform1i=&u1i;glad_glUniform2f=&u2f;glad_glUniform3f=&u3f;
        glad_glUniform4fv=&u4fv;
        glad_glUniformMatrix4fv=&um;glad_glUseProgram=&up;glad_glBindVertexArray=&va;
        glad_glBindTexture=&tx;glad_glDrawElements=&de;glad_glBufferSubData=&bs;glad_glClear=&cl;}
    ~SubmissionProbe(){glad_glUniform1f=one_f;glad_glUniform1i=one_i;glad_glUniform2f=two_f;
        glad_glUniform3f=three_f;glad_glUniformMatrix4fv=matrix;glad_glUseProgram=program;
        glad_glUniform4fv=four_fv;
        glad_glBindVertexArray=array;glad_glBindTexture=texture;glad_glDrawElements=draw;
        glad_glBufferSubData=buffer;glad_glClear=clear;current=nullptr;}
    const UniformLocations& uniforms_for_program(){
        const auto found=std::find_if(locations.begin(),locations.end(),[&](const auto& item){return item.program==active_program;});
        if(found!=locations.end())return *found;
        UniformLocations item;item.program=active_program;
        constexpr std::array names{"u_texture_dimensions","u_masked","u_static_light_rgb",
            "u_directional_light","u_flatshade","u_chrome"};
        for(std::size_t i=0;i<names.size();++i)item.fields[i]=glGetUniformLocation(active_program,names[i]);
        locations.push_back(item);return locations.back();
    }
    static bool unchanged(const GLint location){
        const auto& fields=current->uniforms_for_program().fields;
        // A sprite can also have u_masked. Only the Studio program has the
        // dimensions/light contract changed by B3; sprite writes stay exact.
        const bool studio=fields[0]>=0 && fields[2]>=0;
        if(studio && location==fields[0])++current->counts.texture_dimension_uniforms;
        if(studio && std::find(fields.begin()+1,fields.end(),location)!=fields.end()){
            ++current->counts.material_uniforms;return false;}
        ++current->counts.unchanged_uniforms;return true;
    }
    static bool uniform(const GLint location,const std::uint64_t kind,const std::span<const GLfloat> values){
        ++current->counts.uniforms;mix(current->counts.uniform_signature,kind);
        const bool retained=unchanged(location);if(retained)mix(current->counts.unchanged_uniform_signature,kind);
        for(const auto value:values){const auto word=std::bit_cast<std::uint32_t>(value);
            mix(current->counts.uniform_signature,word);if(retained)mix(current->counts.unchanged_uniform_signature,word);}
        return retained;}
    static void GLAD_API_PTR u1f(GLint x,GLfloat y){uniform(x,1U,std::array{y});current->one_f(x,y);}
    static void GLAD_API_PTR u1i(GLint x,GLint y){++current->counts.uniforms;mix(current->counts.uniform_signature,2U);mix(current->counts.uniform_signature,static_cast<std::uint32_t>(y));
        if(unchanged(x)){mix(current->counts.unchanged_uniform_signature,2U);mix(current->counts.unchanged_uniform_signature,static_cast<std::uint32_t>(y));}current->one_i(x,y);}
    static void GLAD_API_PTR u2f(GLint x,GLfloat y,GLfloat z){uniform(x,3U,std::array{y,z});current->two_f(x,y,z);}
    static void GLAD_API_PTR u3f(GLint x,GLfloat y,GLfloat z,GLfloat w){uniform(x,4U,std::array{y,z,w});current->three_f(x,y,z,w);}
    static void GLAD_API_PTR u4fv(GLint x,GLsizei y,const GLfloat* z){uniform(x,6U,{z,static_cast<std::size_t>(y)*4U});current->four_fv(x,y,z);}
    static void GLAD_API_PTR um(GLint x,GLsizei y,GLboolean z,const GLfloat* w){const bool retained=uniform(x,5U,{w,static_cast<std::size_t>(y)*16U});mix(current->counts.uniform_signature,z);if(retained)mix(current->counts.unchanged_uniform_signature,z);current->matrix(x,y,z,w);}
    static void GLAD_API_PTR up(GLuint x){++current->counts.programs;current->program(x);current->active_program=x;}
    static void GLAD_API_PTR va(GLuint x){++current->counts.arrays;current->array(x);}
    static void GLAD_API_PTR tx(GLenum x,GLuint y){++current->counts.textures;current->texture(x,y);}
    static void GLAD_API_PTR bs(GLenum x,GLintptr y,GLsizeiptr z,const void* w){if(x==GL_UNIFORM_BUFFER){++current->counts.poses;mix(current->counts.pose_signature,static_cast<std::uint64_t>(z));if(w && z>0)mix(current->counts.pose_signature,signature({static_cast<const std::byte*>(w),static_cast<std::size_t>(z)}));}current->buffer(x,y,z,w);}
    static void GLAD_API_PTR de(GLenum x,GLsizei y,GLenum z,const void* w){
        const DrawWords words{std::uint64_t{x},static_cast<std::uint64_t>(y),std::uint64_t{z},
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(w))};
        current->draw_words.push_back(words);for(const auto word:words)mix(current->counts.draw_signature,word);
        // Query actual GL state immediately before the real draw. Missing
        // sprite/world fields have explicit presence markers. Never installed
        // during the CPU sample loop, so these readbacks cannot affect timing.
        const auto& fields=current->uniforms_for_program().fields;
        for(std::size_t i=0;i<fields.size();++i){
            mix(current->counts.effective_uniform_signature,i);
            mix(current->counts.effective_uniform_signature,fields[i]>=0?1U:0U);
            if(fields[i]<0)continue;
            if(i==0U || i==2U){std::array<GLfloat,3> values{};
                glGetUniformfv(current->active_program,fields[i],values.data());
                const std::size_t size=i==0U?2U:3U;
                for(std::size_t j=0;j<size;++j)mix(current->counts.effective_uniform_signature,std::bit_cast<std::uint32_t>(values[j]));
            }else{GLint value{};glGetUniformiv(current->active_program,fields[i],&value);
                mix(current->counts.effective_uniform_signature,static_cast<std::uint32_t>(value));}
        }
        ++current->counts.draws;current->draw(x,y,z,w);
        if(current->fail_draw){current->fail_draw=false;glBindTexture(0xffffffffU,0);}}
    static void GLAD_API_PTR cl(GLbitfield mask){
        if((mask&(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))==(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&current->depth_target);
        current->clear(mask);}
};

std::shared_ptr<const a::ModelAsset> submission_model(const unsigned variant=0U,const bool minimum=false) {
    auto model=std::make_shared<a::ModelAsset>(*ef::model_asset({0U,
        hlclient::goldsrc::studio::kGoldSrcStudioTextureMasked,
        hlclient::goldsrc::studio::kGoldSrcStudioTextureChrome,
        hlclient::goldsrc::studio::kGoldSrcStudioTextureFullbright,
        hlclient::goldsrc::studio::kGoldSrcStudioTextureFlatshade},true));
    auto skeleton=std::make_shared<a::SkeletalModelAssetData>(*model->skeletal_data);
    // Repeated opaque meshes are a normal shared-material Studio shape, not a
    // second renderer. The final mesh is masked; another skin selects chrome,
    // flatshade and fullbright while preserving the same indexed geometry.
    skeleton->skin_families[0].texture_indices={0,1};
    skeleton->skin_families[1].texture_indices={2,3};
    skeleton->skin_families.push_back({{4,1}});
    for(auto& submodel:skeleton->submodels){
        submodel.indices.clear();submodel.meshes.clear();
        for(unsigned mesh=0;mesh<4;++mesh){
            const auto first=static_cast<std::uint32_t>(submodel.indices.size());
            submodel.indices.insert(submodel.indices.end(),{0,1,2});
            submodel.meshes.push_back({first,3U,mesh==3U?1U:0U,mesh,1U,0U,0U});
        }
    }
    if(variant){
        skeleton->skin_families[0].texture_indices={1,0};
        skeleton->skin_families[1].texture_indices={3,2};
        for(auto& submodel:skeleton->submodels)for(auto& vertex:submodel.vertices)
            vertex.source_position.x+=.35F;
    }
    if(minimum){skeleton->bodyparts.resize(1);skeleton->bodyparts[0].submodel_indices={0};
        skeleton->submodels.resize(1);skeleton->submodels[0].meshes.resize(1);skeleton->submodels[0].indices.resize(3);}
    model->skeletal_data=std::move(skeleton);
    return model;
}
std::shared_ptr<const e::EntitySceneRenderPackage> package() {
    auto built=ef::scene_package(ef::render_assets(true,true,a::SpriteTextureFormat::normal,
        true,a::SpriteOrientation::view_parallel,submission_model()));
    REQUIRE(built);return std::make_shared<const e::EntitySceneRenderPackage>(std::move(*built.package));
}
std::shared_ptr<const e::EntityRenderFrame> frame(
    const e::EntitySceneRenderPackage& owner,unsigned actors,unsigned phase,bool viewmodel=false) {
    e::EntityRenderFrameBuildInput input;input.resource_id=0x0C29U;input.resource_revision=phase+1U;
    input.interpolation={.5,0,1,.5F,phase+1U,phase+2U,e::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    input.studio_poses.push_back({owner.studio_assets()[0]->source_identity(),{f::pose_matrix(float(phase)*.015F)}});
    for(unsigned i=0;i<actors;++i){
        e::StudioEntityRenderInstance actor;actor.entity_number=i+1U;
        actor.body_value=(i+phase)%2U;actor.skin_family_index=phase==0?0U:(i+phase)%3U;
        actor.transform.origin={-4.5F+float(i%4)*3.F,-1.F+float(i/4)*3.F,1.F};
        actor.transform.rotation_degrees={90.F,0,0};actor.transform.uniform_scale=1.25F;
        if(viewmodel){actor.transform.origin={5.F,-.5F,-.6F};actor.transform.rotation_degrees={0,90.F,0};}
        actor.interpolated_bounds={{-10,-10,-10},{20,20,20}};
        actor.static_light_rgb=phase==0?std::array{.45F,.55F,.65F}:
            std::array{.25F+float(i%3)*.2F,.55F,.65F};
        if(phase==3 && i%2){actor.blend_mode=e::StudioBlendMode::alpha_blended;actor.opacity=.5F;}
        input.studio_instances.push_back(actor);
        if(!viewmodel){auto companion=actor;companion.subinstance_ordinal=1U;
            companion.transform.origin.x+=.2F;companion.transform.origin.z+=.4F;
            companion.transform.uniform_scale=.4F;companion.tint={.65F,.8F,1.F};
            input.studio_instances.push_back(companion);}
    }
    if(phase==3 && !viewmodel){
        e::SpriteEntityRenderInstance sprite;sprite.entity_number=actors+1U;
        sprite.orientation=owner.sprite_assets()[0]->orientation();
        sprite.texture_format_support=owner.sprite_assets()[0]->texture_support_status();
        sprite.transform.origin={0,3,2};sprite.transform.uniform_scale=.35F;
        sprite.bounds={{-10,-10,-10},{20,20,20}};sprite.blend_mode=e::SpriteBlendMode::alpha_blended;
        sprite.opacity=.4F;input.sprite_instances.push_back(sprite);
    }
    auto built=e::EntityRenderFrameBuilder{}.build(owner,std::move(input));
    INFO((built.error?built.error->context:""));REQUIRE(built);
    return std::make_shared<const e::EntityRenderFrame>(std::move(*built.frame));
}
r::RenderScene scene(const std::shared_ptr<const e::EntitySceneRenderPackage>& owner,
    unsigned actors,unsigned phase,unsigned samples) {
    auto result=f::render_scene({owner,frame(*owner,actors,phase)});
    result.requested_msaa_samples=samples;result.image_adjustments={};
    const auto local=f::render_scene({owner,frame(*owner,1U,phase,true)});
    result.first_person_entities=local.dynamic_entities;
    result.transient_world_light=r::RenderPointLight{{0,0,3},32,.2F,{.4F,.5F,.8F}};
    result.transient_first_person_light=r::RenderPointLight{{3,0,0},20,.1F,{.8F,.5F,.2F}};
    if(phase){result.world_effect_light_count=1;result.world_effect_lights[0]={{float(phase),1,3},20,.2F,{.8F,.2F,.3F}};}
    return result;
}
std::vector<std::byte> color(r::RenderExtent extent) {
    std::vector<std::byte> pixels(std::size_t(extent.width)*std::size_t(extent.height)*4U);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
    glReadPixels(0,0,extent.width,extent.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    REQUIRE(glGetError()==GL_NO_ERROR);return pixels;
}
std::uint64_t depth(GLint source,r::RenderExtent extent) {
    GLint old_read{},old_draw{},old_rb{};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&old_rb);
    GLuint target{},buffer{};
    if(source){glGenFramebuffers(1,&target);glGenRenderbuffers(1,&buffer);glBindRenderbuffer(GL_RENDERBUFFER,buffer);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,extent.width,extent.height);
        glBindFramebuffer(GL_FRAMEBUFFER,target);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,buffer);
        glReadBuffer(GL_NONE);glDrawBuffer(GL_NONE);REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(source));
        glBlitFramebuffer(0,0,extent.width,extent.height,0,0,extent.width,extent.height,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,target);}
    std::vector<float> values(std::size_t(extent.width)*std::size_t(extent.height));
    glReadPixels(0,0,extent.width,extent.height,GL_DEPTH_COMPONENT,GL_FLOAT,values.data());
    const auto error=glGetError();glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(old_read));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(old_draw));glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(old_rb));
    if(target)glDeleteFramebuffers(1,&target);if(buffer)glDeleteRenderbuffers(1,&buffer);
    REQUIRE(error==GL_NO_ERROR);REQUIRE(std::any_of(values.begin(),values.end(),[](float value){return value<1.F;}));
    return signature(std::as_bytes(std::span{values}));
}
struct MaterialCallOracle {std::uint64_t baseline{},candidate{},draws{},commands{};};
MaterialCallOracle material_call_oracle(const r::RenderScene& input) {
    MaterialCallOracle result;
    const auto visit=[&](const std::optional<r::RenderDynamicEntities>& entities){
        if(!entities)return;
        for(const auto& command:entities->frame->draw_commands()){
            if(command.visual_kind!=e::RuntimeEntityVisualKind::studio_model)continue;
            const auto& instance=entities->frame->studio_instances()[command.instance_index];
            if(instance.visibility_status!=e::RuntimeEntityVisibilityStatus::visible ||
                (instance.blend_mode!=e::StudioBlendMode::opaque && instance.opacity==0.F))continue;
            ++result.commands;++result.baseline;++result.candidate; // Unchanged initial static light write.
            const auto& asset=*entities->package->studio_assets()[instance.studio_asset_index];
            const auto selected=asset.select_submodels(instance.body_value);REQUIRE(selected);
            std::optional<std::array<std::uint32_t,7>> previous;
            for(const auto submodel_index:selected.submodel_indices){const auto& submodel=asset.submodels()[submodel_index];
                for(std::uint32_t i=0;i<submodel.mesh_count;++i){
                    const auto material=asset.select_material(submodel.first_mesh+i,instance.skin_family_index);REQUIRE(material);
                    const auto& value=asset.materials()[*material.material_index];
                    const bool masked=value.profile==e::StudioRenderMaterialProfile::masked;
                    if(value.profile==e::StudioRenderMaterialProfile::unsupported ||
                        masked!=(command.draw_class==e::EntityDrawClass::studio_masked))continue;
                    const auto light=value.fullbright_metadata?std::array{1.F,1.F,1.F}:
                        instance.static_light_rgb.value_or(std::array{.85F,.85F,.85F});
                    const std::array<std::uint32_t,7> bits{masked?1U:0U,
                        std::bit_cast<std::uint32_t>(light[0]),std::bit_cast<std::uint32_t>(light[1]),
                        std::bit_cast<std::uint32_t>(light[2]),instance.static_light_rgb && !value.fullbright_metadata?1U:0U,
                        value.flatshade_metadata?1U:0U,value.support_status==e::StudioRenderMaterialSupportStatus::supported_chrome?1U:0U};
                    ++result.draws;result.baseline+=5U;
                    if(!previous)result.candidate+=5U;
                    else{result.candidate+=((*previous)[0]!=bits[0]?1U:0U);
                        result.candidate+=((*previous)[1]!=bits[1] || (*previous)[2]!=bits[2] || (*previous)[3]!=bits[3]?1U:0U);
                        for(std::size_t field=4;field<bits.size();++field)result.candidate+=((*previous)[field]!=bits[field]?1U:0U);}
                    previous=bits;
                }
            }
        }
    };
    visit(input.dynamic_entities);visit(input.first_person_entities);
    return result;
}
void require_material_calls(const r::RenderScene& input,const Counters& calls){
    const auto expected=material_call_oracle(input);
    CHECK(calls.material_uniforms==(HLCLIENT_CPU_STUDIO_COMMAND_UNIFORMS?expected.candidate:expected.baseline));
    CHECK(calls.texture_dimension_uniforms==expected.draws);
    CHECK(calls.uniforms==calls.material_uniforms+calls.unchanged_uniforms);
    // The expected reduction follows source-selected per-field transitions,
    // not material identity or an assumed constant number of meshes/commands.
    CHECK(expected.candidate<=expected.baseline);
}
void emit(const char* label,unsigned actors,unsigned phase,unsigned mode,const Counters& c,
    std::uint64_t rgba,std::uint64_t z,const r::RenderScene& input) {
    require_material_calls(input,c);const auto expected=material_call_oracle(input);
    std::cout<<"[0c29-submission-proof] {\"label\":"<<std::quoted(label)<<",\"actors\":"<<actors
        <<",\"phase\":"<<phase<<",\"aa\":"<<mode<<",\"uniforms\":"<<c.uniforms
        <<",\"programs\":"<<c.programs<<",\"arrays\":"<<c.arrays<<",\"textures\":"<<c.textures
        <<",\"draws\":"<<c.draws<<",\"poses\":"<<c.poses<<",\"color\":\""<<rgba
        <<"\",\"depth\":\""<<z<<"\",\"draw_signature\":\""<<c.draw_signature
        <<"\",\"uniform_signature\":\""<<c.uniform_signature<<"\",\"uniform_call_signature\":\""<<c.uniform_signature
        <<"\",\"unchanged_uniforms\":"<<c.unchanged_uniforms<<",\"material_uniforms\":"<<c.material_uniforms
        <<",\"texture_dimension_uniforms\":"<<c.texture_dimension_uniforms
        <<",\"unchanged_uniform_signature\":\""<<c.unchanged_uniform_signature
        <<"\",\"effective_uniform_signature\":\""<<c.effective_uniform_signature
        <<"\",\"expected_baseline_material_uniforms\":"<<expected.baseline
        <<",\"expected_candidate_material_uniforms\":"<<expected.candidate
        <<",\"command_uniform_reuse\":"<<HLCLIENT_CPU_STUDIO_COMMAND_UNIFORMS
        <<",\"pose_signature\":\""<<c.pose_signature<<"\"}\n";
}

std::shared_ptr<const e::EntitySceneRenderPackage> metadata_package(
    const unsigned variant=0U,const std::uint64_t revision=1U,const bool minimum=false) {
    auto asset=e::StudioModelRenderAssetBuilder{}.build(*submission_model(variant,minimum),{0xB200U,revision});
    REQUIRE(asset);e::EntitySceneRenderPackageCreateInfo input;
    input.asset_source=e::EntitySceneRenderAssetSource::project_generated_diagnostic;input.resource_id=0xB201U;
    input.studio_assets.push_back(std::make_shared<const e::StudioModelRenderAsset>(std::move(*asset.asset)));
    auto built=e::EntitySceneRenderPackageBuilder{}.build(std::move(input));REQUIRE(built);
    return std::make_shared<const e::EntitySceneRenderPackage>(std::move(*built.package));
}
e::EntityRenderFrameBuildInput selection_input(const e::EntitySceneRenderPackage& owner,
    const std::uint32_t body,const std::uint32_t skin,const bool empty=false) {
    e::EntityRenderFrameBuildInput input;input.resource_id=0xB202U;input.resource_revision=1U;
    input.interpolation={.5,0,1,.5F,1U,2U,e::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    if(!empty){input.studio_poses.push_back({owner.studio_assets()[0]->source_identity(),{f::pose_matrix(.125F)}});
        e::StudioEntityRenderInstance actor;actor.entity_number=1U;actor.body_value=body;actor.skin_family_index=skin;
        actor.transform.rotation_degrees={90.F,0,0};actor.transform.uniform_scale=2.F;
        actor.interpolated_bounds={{-10,-10,-10},{10,10,10}};actor.static_light_rgb=std::array{.35F,.55F,.75F};
        input.studio_instances.push_back(actor);}
    return input;
}
r::RenderScene metadata_scene(const std::shared_ptr<const e::EntitySceneRenderPackage>& owner,
    const std::uint32_t body,const std::uint32_t skin,const bool empty=false) {
    auto built=e::EntityRenderFrameBuilder{}.build(*owner,selection_input(*owner,body,skin,empty));REQUIRE(built);
    return f::render_scene({owner,std::make_shared<const e::EntityRenderFrame>(std::move(*built.frame))});
}
std::vector<DrawWords> uncached_draw_oracle(const r::RenderScene& input) {
    REQUIRE(input.dynamic_entities);const auto& entities=*input.dynamic_entities;
    std::vector<DrawWords> words;
    for(const auto& command:entities.frame->draw_commands()){
        REQUIRE(command.visual_kind==e::RuntimeEntityVisualKind::studio_model);
        const auto& actor=entities.frame->studio_instances()[command.instance_index];
        const auto& asset=*entities.package->studio_assets()[actor.studio_asset_index];
        const auto selected=asset.select_submodels(actor.body_value);REQUIRE(selected);
        for(const auto submodel_index:selected.submodel_indices){const auto& submodel=asset.submodels()[submodel_index];
            for(std::uint32_t i=0;i<submodel.mesh_count;++i){const auto mesh_index=submodel.first_mesh+i;
                const auto material=asset.select_material(mesh_index,actor.skin_family_index);REQUIRE(material);
                const auto& selected_material=asset.materials()[*material.material_index];
                const bool masked=selected_material.profile==e::StudioRenderMaterialProfile::masked;
                if(masked!=(command.draw_class==e::EntityDrawClass::studio_masked))continue;
                const auto& mesh=asset.meshes()[mesh_index];
                words.push_back({GL_TRIANGLES,mesh.index_count,GL_UNSIGNED_INT,
                    static_cast<std::uint64_t>(mesh.first_index)*sizeof(std::uint32_t)});
            }
        }
    }
    return words;
}
struct Observation {Counters calls;std::vector<DrawWords> words;std::vector<std::byte> pixels;std::uint64_t resolved_depth{};};
Observation observe(f::HiddenContext& context,const r::RenderScene& input,const r::RenderExtent extent) {
    SubmissionProbe probe;context.renderer().render(input,extent);
    Observation result{probe.counts,probe.draw_words,color(extent),depth(probe.depth_target,extent)};
    require_material_calls(input,result.calls);
    CHECK(result.words==uncached_draw_oracle(input));CHECK(glGetError()==GL_NO_ERROR);return result;
}
void same_observation(const Observation& a,const Observation& b){
    CHECK(a.pixels==b.pixels);CHECK(a.resolved_depth==b.resolved_depth);CHECK(a.words==b.words);
    CHECK(a.calls.uniforms==b.calls.uniforms);CHECK(a.calls.programs==b.calls.programs);
    CHECK(a.calls.arrays==b.calls.arrays);CHECK(a.calls.textures==b.calls.textures);
    CHECK(a.calls.draws==b.calls.draws);CHECK(a.calls.poses==b.calls.poses);
    CHECK(a.calls.draw_signature==b.calls.draw_signature);CHECK(a.calls.uniform_signature==b.calls.uniform_signature);
    CHECK(a.calls.unchanged_uniforms==b.calls.unchanged_uniforms);
    CHECK(a.calls.material_uniforms==b.calls.material_uniforms);
    CHECK(a.calls.texture_dimension_uniforms==b.calls.texture_dimension_uniforms);
    CHECK(a.calls.unchanged_uniform_signature==b.calls.unchanged_uniform_signature);
    CHECK(a.calls.effective_uniform_signature==b.calls.effective_uniform_signature);
    CHECK(a.calls.pose_signature==b.calls.pose_signature);
}
}

TEST_CASE("Studio metadata selection retains A-B-A ordered duplicates materials and cold-reset outputs",
    "[0c29-metadata-contract][0c29-submission][renderer][opengl][no-stock]") {
    auto context=f::try_context();if(!context || !f::capable_context())SKIP("OpenGL3.3 Core unavailable");
    context->initialize_renderer();const auto owner=metadata_package();constexpr r::RenderExtent extent{96,96};
    for(const unsigned mode:{0U,2U,4U,8U}){
        CAPTURE(mode);auto first=metadata_scene(owner,0U,0U),second=metadata_scene(owner,1U,1U);
        auto third=metadata_scene(owner,std::numeric_limits<std::uint32_t>::max(),2U);
        auto empty=metadata_scene(owner,0U,0U,true);
        for(auto* input:{&first,&second,&third,&empty})input->requested_msaa_samples=mode;
        context->renderer().reset_session_resources();context->renderer().render(empty,extent);
        REQUIRE(context->renderer().scene_target_statistics().actual_samples==mode);
        // The empty frame uploads this owner without selecting metadata. Thus
        // the first probe is an actual plan miss, without counting GPU uploads.
        const auto first_cold=observe(*context,first,extent);
        CHECK(first_cold.calls.draws==8U);REQUIRE(first_cold.words.size()==8U);
        CHECK(std::count(first_cold.words.begin(),first_cold.words.end(),first_cold.words[0])==2);
        same_observation(first_cold,observe(*context,first,extent));
        const auto second_miss=observe(*context,second,extent);
        CHECK(first_cold.pixels!=second_miss.pixels);
        same_observation(first_cold,observe(*context,first,extent));
        same_observation(second_miss,observe(*context,second,extent));
        const auto maximum_body=observe(*context,third,extent);
        CHECK(maximum_body.calls.draws==8U);
        same_observation(first_cold,observe(*context,first,extent));
        // Full body integers are valid modulo aliases. More than eight exact
        // keys forces bounded-plan eviction; returning to a retired key must
        // rebuild the original ordered duplicates/material selection exactly.
        auto alias=metadata_scene(owner,100U,2U);alias.requested_msaa_samples=mode;
        const auto alias_expected=observe(*context,alias,extent);
        for(std::uint32_t body=101U;body<=109U;++body){
            auto churn=metadata_scene(owner,body,2U);churn.requested_msaa_samples=mode;
            static_cast<void>(observe(*context,churn,extent));
        }
        same_observation(alias_expected,observe(*context,alias,extent));
        same_observation(first_cold,observe(*context,first,extent));
        context->renderer().reset_session_resources();context->renderer().render(empty,extent);
        same_observation(second_miss,observe(*context,second,extent));
        same_observation(maximum_body,observe(*context,third,extent));
        same_observation(first_cold,observe(*context,first,extent));
        emit("metadata-A-B-A",1U,0U,mode,first_cold.calls,signature(first_cold.pixels),first_cold.resolved_depth,first);
    }
}

TEST_CASE("Studio metadata plan is owned across distinct assets revisions destruction and minimum selection",
    "[0c29-metadata-contract][0c29-submission][renderer][opengl][no-stock]") {
    auto context=f::try_context();if(!context || !f::capable_context())SKIP("OpenGL3.3 Core unavailable");
    context->initialize_renderer();constexpr r::RenderExtent extent{96,96};
    const auto owner_a=metadata_package(),owner_b=metadata_package(1U),owner_revision=metadata_package(1U,2U);
    REQUIRE(owner_a->studio_assets()[0].get()!=owner_b->studio_assets()[0].get());
    CHECK(owner_a->studio_assets()[0]->source_identity()==owner_b->studio_assets()[0]->source_identity());
    CHECK(owner_a->studio_assets()[0]->resource_revision()!=owner_b->studio_assets()[0]->resource_revision());
    CHECK(owner_b->studio_assets()[0]->source_identity()!=owner_revision->studio_assets()[0]->source_identity());
    auto first=metadata_scene(owner_a,0U,0U),second=metadata_scene(owner_b,0U,0U);
    auto replacement=metadata_scene(owner_revision,0U,0U);
    const auto capture=[&](const r::RenderScene& input){
        // Ensure this owner's GPU resources outside the actual-call probe;
        // an empty frame does not populate the metadata selection plan.
        context->renderer().render(metadata_scene(input.dynamic_entities->package,0U,0U,true),extent);
        return observe(*context,input,extent);
    };
    const auto a=capture(first),b=capture(second);CHECK(a.pixels!=b.pixels);CHECK(a.words!=b.words);
    same_observation(a,capture(first));same_observation(b,capture(second));
    const auto revised=capture(replacement);same_observation(b,revised);same_observation(a,capture(first));
    context->renderer().reset_session_resources();same_observation(b,capture(second));same_observation(a,capture(first));
    std::weak_ptr<const e::StudioModelRenderAsset> retired;
    Observation minimum;
    {
        const auto temporary=metadata_package(0U,5U,true);retired=temporary->studio_assets()[0];
        const auto minimal=metadata_scene(temporary,0U,0U);minimum=capture(minimal);
        CHECK(minimum.calls.draws==1U);CHECK(minimum.words.size()==1U);
    }
    CHECK_FALSE(retired.expired());context->renderer().reset_session_resources();CHECK(retired.expired());
    // Do not assume a particular address is recycled; no cached borrowed spans
    // or owner pointer may survive resource reset/destruction in either case.
    const auto fresh=metadata_package(0U,5U,true);same_observation(minimum,capture(metadata_scene(fresh,0U,0U)));
}

TEST_CASE("Studio metadata selection preserves reachable construction errors draw failure and recovery",
    "[0c29-metadata-contract][0c29-submission][renderer][opengl][no-stock]") {
    auto context=f::try_context();if(!context || !f::capable_context())SKIP("OpenGL3.3 Core unavailable");
    context->initialize_renderer();constexpr r::RenderExtent extent{96,96};const auto owner=metadata_package();
    auto first=metadata_scene(owner,0U,0U),second=metadata_scene(owner,1U,1U);
    context->renderer().render(metadata_scene(owner,0U,0U,true),extent);
    const auto expected=observe(*context,first,extent);static_cast<void>(observe(*context,second,extent));
    {
        SubmissionProbe probe;
        // All uint32 body values are reachable valid modulo selections. An
        // invalid skin cannot enter the immutable frame through the public API.
        const auto invalid_skin=e::EntityRenderFrameBuilder{}.build(*owner,
            selection_input(*owner,0U,std::numeric_limits<std::uint32_t>::max()));
        REQUIRE_FALSE(invalid_skin);REQUIRE(invalid_skin.error);
        CHECK(invalid_skin.error->code==e::EntityRenderFrameErrorCode::invalid_asset_reference);
        CHECK(invalid_skin.error->context=="Studio body, skin, mesh, or material selection is invalid");
        auto model=std::make_shared<a::ModelAsset>(*submission_model());
        auto skeleton=std::make_shared<a::SkeletalModelAssetData>(*model->skeletal_data);
        skeleton->bodyparts[0].base=0;model->skeletal_data=std::move(skeleton);
        const auto invalid_body=e::StudioModelRenderAssetBuilder{}.build(*model,{0xB200U,1U});
        REQUIRE_FALSE(invalid_body);REQUIRE(invalid_body.error);
        CHECK(invalid_body.error->code==e::StudioModelRenderAssetErrorCode::invalid_bodypart);
        CHECK(invalid_body.error->context=="Studio bodypart requires a positive base and submodels");
        CHECK(probe.counts.draws==0U);CHECK(probe.counts.uniforms==0U);CHECK(probe.counts.poses==0U);
    }
    same_observation(expected,observe(*context,first,extent));
    {
        SubmissionProbe probe;probe.fail_draw=true;bool caught=false;
        try{context->renderer().render(second,extent);}
        catch(const r::opengl::OpenGlRendererError& error){caught=true;
            CHECK(error.code()==r::opengl::OpenGlRendererErrorCode::gl_operation_failed);
            CHECK(std::string_view{error.what()}.find("OpenGL entity draw failed")!=std::string_view::npos);}
        CHECK(caught);CHECK_FALSE(probe.fail_draw);CHECK(probe.counts.draws>0U);
    }
    same_observation(expected,observe(*context,first,extent));
    context->renderer().reset_session_resources();context->renderer().render(metadata_scene(owner,0U,0U,true),extent);
    same_observation(expected,observe(*context,first,extent));CHECK(glGetError()==GL_NO_ERROR);
}

TEST_CASE("Studio submission pass reuse retains cold reset exception and external-state outputs",
    "[0c29-submission][renderer][opengl][no-stock]") {
    auto context=f::try_context();if(!context || !f::capable_context())SKIP("OpenGL3.3 Core unavailable");
    context->initialize_renderer();auto owner=package();constexpr r::RenderExtent extent{96,96};
    for(const unsigned mode:{0U,2U,4U,8U})for(unsigned phase=0;phase<4;++phase){
        CAPTURE(mode,phase);auto input=scene(owner,8U,phase,mode);
        context->renderer().render(input,extent);
        if(context->renderer().scene_target_statistics().actual_samples!=mode){WARN("Requested actual MSAA mode unavailable");continue;}
        const auto expected=color(extent);
        // Deliberate caller binding changes between calls must never be trusted
        // by a retained cache. The next call starts with all bindings unknown.
        GLuint foreign{};glGenTextures(1,&foreign);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,foreign);
        glBindVertexArray(0);glUseProgram(0);glActiveTexture(GL_TEXTURE5);
        {SubmissionProbe probe;context->renderer().render(input,extent);
            CHECK(probe.counts.draws>0);CHECK(probe.counts.poses>0);
            emit("same-input",8U,phase,mode,probe.counts,signature(color(extent)),depth(probe.depth_target,extent),input);}
        CHECK(color(extent)==expected);glDeleteTextures(1,&foreign);
        {SubmissionProbe probe;probe.fail_draw=true;CHECK_THROWS(context->renderer().render(input,extent));CHECK_FALSE(probe.fail_draw);}
        context->renderer().render(input,extent);CHECK(color(extent)==expected);
        context->renderer().reset_session_resources();context->renderer().render(input,extent);CHECK(color(extent)==expected);
        auto world_only=input;world_only.first_person_entities.reset();
        {SubmissionProbe probe;context->renderer().render(world_only,extent);
            emit("world-only",8U,phase,mode,probe.counts,signature(color(extent)),depth(probe.depth_target,extent),world_only);}
    }
}

TEST_CASE("Opt-in warmed production Studio submission records uncached-driver CPU samples",
    "[0c29-submission-timing][renderer][opengl][no-stock]") {
    if(setting("HLCLIENT_0C29_SUBMISSION_TIMING")!="1")SKIP("Explicit bounded offline timing opt-in required");
    const auto count_text=setting("HLCLIENT_0C29_ACTORS"),mode_text=setting("HLCLIENT_0C29_MSAA");
    const unsigned actors=count_text.empty()?16U:static_cast<unsigned>(std::stoul(count_text));
    const unsigned mode=mode_text.empty()?0U:static_cast<unsigned>(std::stoul(mode_text));
    REQUIRE((actors==1U||actors==4U||actors==8U||actors==12U||actors==16U));
    REQUIRE((mode==0U||mode==2U||mode==4U||mode==8U));
    constexpr r::RenderExtent extent{1920,1080};
    auto context=f::try_context(extent.width,extent.height);
    if(!context || !f::capable_context())SKIP("OpenGL3.3 Core unavailable");
    REQUIRE(SDL_GL_SetSwapInterval(0));int actual_swap{};REQUIRE(SDL_GL_GetSwapInterval(&actual_swap));
    REQUIRE(actual_swap==0);
    context->initialize_renderer();auto owner=package();std::array<r::RenderScene,4> inputs;
    for(unsigned phase=0;phase<inputs.size();++phase)inputs[phase]=scene(owner,actors,phase,mode);
    const auto& info=context->renderer().information();
    std::cout<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<"[0c29-submission-context] {\"vendor\":"<<std::quoted(info.vendor)<<",\"renderer\":"<<std::quoted(info.device)
        <<",\"version\":"<<std::quoted(info.version)<<",\"width\":1920,\"height\":1080,\"requested_msaa\":"<<mode
        <<",\"actors\":"<<actors<<",\"fixture\":\"synthetic_prebuilt_time_varying\",\"historical_replay\":false,\"swaps\":false,\"swap_interval\":"<<actual_swap<<",\"dispatch_hooks_in_timing\":false}\n";
    const auto cold=Clock::now();context->renderer().render(inputs[0],extent);
    const auto cold_ms=std::chrono::duration<double,std::milli>{Clock::now()-cold}.count();
    if(context->renderer().scene_target_statistics().actual_samples!=mode)SKIP("Requested actual MSAA mode unavailable");
    constexpr unsigned warm=96,samples=512;
    std::vector<double> times;times.reserve(samples);
    for(unsigned frame_index=0;frame_index<warm+samples;++frame_index){
        const auto begin=Clock::now();context->renderer().render(inputs[frame_index%inputs.size()],extent);
        const auto end=Clock::now();if(frame_index>=warm)times.push_back(std::chrono::duration<double,std::milli>{end-begin}.count());
        glFlush(); // Test-only, outside each timed interval; no swap/readback/wait in the timed scope.
    }
    for(unsigned phase=0;phase<inputs.size();++phase){SubmissionProbe probe;context->renderer().render(inputs[phase],extent);
        emit("timed-input",actors,phase,mode,probe.counts,signature(color(extent)),depth(probe.depth_target,extent),inputs[phase]);}
    std::cout<<"[0c29-submission-cpu] {\"actors\":"<<actors<<",\"aa\":"<<mode<<",\"cold_ms\":"<<cold_ms<<",\"samples\":[";
    for(std::size_t i=0;i<times.size();++i){if(i)std::cout<<',';std::cout<<times[i];}std::cout<<"]}\n";
    REQUIRE(times.size()==samples);CHECK(glGetError()==GL_NO_ERROR);
}
