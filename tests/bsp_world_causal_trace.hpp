#pragma once
#ifdef HLCLIENT_BSP_CANDIDATE
// Offline, explicitly requested forensic renders only. GL state reads, index
// readbacks and blocking query retrieval below NEVER enter measured rendering.
#include "pvs_negative_control_diagnostics.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace hlclient::tests::bsp_world_causal_trace {
struct RestorePolicies {
    renderer::opengl::OpenGlRenderer& renderer;
    renderer::opengl::OpenGlStaticWorldSelectionPolicy selection;
    renderer::opengl::OpenGlStaticWorldOrderPolicy order;
    ~RestorePolicies() {renderer.set_static_world_selection_policy(selection);renderer.set_static_world_order_policy(order);}
};
// Record the owned scene depth target, not the post-adjustment default FBO.
// A full-frame read includes the later viewmodel depth clear and is labelled
// separately from the world-only proof; neither read is in a timed sample.
struct DepthTargetCapture {
    inline static DepthTargetCapture* active{};
    PFNGLCLEARPROC saved{};GLint target{};
    static void GLAD_API_PTR clear(GLbitfield mask) {
        if((mask&(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))==(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&active->target);
        active->saved(mask);
    }
    DepthTargetCapture() {REQUIRE(active==nullptr);active=this;saved=glad_glClear;glad_glClear=&clear;}
    ~DepthTargetCapture(){glad_glClear=saved;active=nullptr;}
};
// Test-only submission control: same offsets, counts, order, state and EBO;
// change only MultiDrawElements into an ordered DrawElements loop. Saved raw
// DrawElements intentionally avoids re-entering an outer forensic trace hook.
struct LoopMultiDraw {
    inline static LoopMultiDraw* active{};
    PFNGLMULTIDRAWELEMENTSPROC saved_multi{};
    PFNGLDRAWELEMENTSPROC saved_draw{};
    std::uint64_t intercepted_calls{},submitted_ranges{};
    static void GLAD_API_PTR multi(GLenum mode,const GLsizei* counts,GLenum type,const void* const* offsets,GLsizei ranges) {
        auto& self=*active;++self.intercepted_calls;
        for(GLsizei i=0;i<ranges;++i){self.saved_draw(mode,counts[i],type,offsets[i]);++self.submitted_ranges;}
    }
    explicit LoopMultiDraw(bool enabled) {
        if(enabled){REQUIRE(active==nullptr);active=this;saved_multi=glad_glMultiDrawElements;saved_draw=glad_glDrawElements;glad_glMultiDrawElements=&multi;}
    }
    ~LoopMultiDraw(){if(active==this){glad_glMultiDrawElements=saved_multi;active=nullptr;}}
};
inline void hash_word(std::uint64_t& hash,std::uint32_t word) {
    for(unsigned shift=0;shift<32;shift+=8){hash^=(word>>shift)&255U;hash*=1099511628211ULL;}
}
inline std::uint64_t uniform_signature(GLuint program) {
    GLint count{},max_name{};glGetProgramiv(program,GL_ACTIVE_UNIFORMS,&count);
    glGetProgramiv(program,GL_ACTIVE_UNIFORM_MAX_LENGTH,&max_name);
    std::vector<char> text(static_cast<std::size_t>(std::max(max_name,1)));
    std::uint64_t hash=14695981039346656037ULL;
    for(GLint i=0;i<count;++i) {
        GLsizei length{};GLint size{};GLenum type{};
        glGetActiveUniform(program,static_cast<GLuint>(i),max_name,&length,&size,&type,text.data());
        std::string name(text.data(),static_cast<std::size_t>(length));
        const auto bracket=name.find("[0]");if(bracket!=std::string::npos)name.resize(bracket);
        for(const auto c:name){hash^=static_cast<unsigned char>(c);hash*=1099511628211ULL;}
        hash_word(hash,type);hash_word(hash,static_cast<std::uint32_t>(size));
        bool integer=false;std::size_t components{};
        switch(type) {
        case GL_FLOAT:components=1;break;case GL_FLOAT_VEC2:components=2;break;
        case GL_FLOAT_VEC3:components=3;break;case GL_FLOAT_VEC4:components=4;break;
        case GL_FLOAT_MAT4:components=16;break;
        case GL_INT:case GL_BOOL:case GL_SAMPLER_2D:case GL_SAMPLER_CUBE:
        case GL_SAMPLER_2D_ARRAY:integer=true;components=1;break;
        default:FAIL("Unexpected uniform type in offline world trace");
        }
        for(GLint element=0;element<size;++element) {
            const auto element_name=size>1?name+"["+std::to_string(element)+"]":name;
            const auto location=glGetUniformLocation(program,element_name.c_str());
            REQUIRE(location>=0);
            if(integer) {
                std::array<GLint,16> values{};glGetUniformiv(program,location,values.data());
                for(std::size_t j=0;j<components;++j)hash_word(hash,static_cast<std::uint32_t>(values[j]));
            } else {
                std::array<GLfloat,16> values{};glGetUniformfv(program,location,values.data());
                for(std::size_t j=0;j<components;++j)hash_word(hash,std::bit_cast<std::uint32_t>(values[j]));
            }
        }
    }
    return hash;
}
inline std::uint64_t raster_state_signature() {
    std::uint64_t hash=14695981039346656037ULL;
    for(const GLenum capability:{GL_DEPTH_TEST,GL_BLEND,GL_CULL_FACE,GL_SCISSOR_TEST,GL_POLYGON_OFFSET_FILL})
        hash_word(hash,glIsEnabled(capability));
    for(const GLenum name:{GL_DEPTH_FUNC,GL_CULL_FACE_MODE,GL_FRONT_FACE,GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,
            GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA}) {
        GLint value{};glGetIntegerv(name,&value);hash_word(hash,static_cast<std::uint32_t>(value));
    }
    GLboolean depth_mask{},color_mask[4]{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
    glGetBooleanv(GL_COLOR_WRITEMASK,color_mask);hash_word(hash,depth_mask);
    for(const auto value:color_mask)hash_word(hash,value);
    GLfloat depth_range[2]{};glGetFloatv(GL_DEPTH_RANGE,depth_range);
    for(const auto value:depth_range)hash_word(hash,std::bit_cast<std::uint32_t>(value));
    for(const GLenum name:{GL_POLYGON_OFFSET_FACTOR,GL_POLYGON_OFFSET_UNITS}) {
        GLfloat value{};glGetFloatv(name,&value);hash_word(hash,std::bit_cast<std::uint32_t>(value));
    }
    for(const GLenum name:{GL_VIEWPORT,GL_SCISSOR_BOX}) {
        GLint values[4]{};glGetIntegerv(name,values);
        for(const auto value:values)hash_word(hash,static_cast<std::uint32_t>(value));
    }
    GLint polygon_mode[2]{};glGetIntegerv(GL_POLYGON_MODE,polygon_mode);
    for(const auto value:polygon_mode)hash_word(hash,static_cast<std::uint32_t>(value));
    return hash;
}
struct Draw {
    struct IndexRange {std::uintptr_t byte_offset{};std::uint32_t count{};std::size_t stream_first{};};
    GLint vao{},ebo{},program{};
    std::array<GLint,3> textures{};
    std::uint64_t uniforms{},samples_passed{},raster_state{};
    GLuint query{};std::size_t ranges{};
    unsigned sky_class{}; // 0 unchanged/other, 1 common sky, 2 extra sky
    std::vector<std::uint32_t> indices;
    std::vector<IndexRange> index_ranges;
};
struct Trace {
    inline static Trace* active{};
    PFNGLDRAWELEMENTSPROC saved_draw{};
    PFNGLMULTIDRAWELEMENTSPROC saved_multi{};
    PFNGLCLEARPROC saved_clear{};
    GLint depth_target{};
    const std::map<std::array<std::uint32_t,3>,unsigned>* sky_classes{};
    GLint classified_world_vao{};
    std::vector<Draw> draws;
    static void GLAD_API_PTR clear(GLbitfield mask) {
        if((mask&(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))==(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&active->depth_target);
        active->saved_clear(mask);
    }
    Draw& begin(GLenum mode,GLenum type,const GLsizei* counts,const void* const* offsets,GLsizei ranges) {
        REQUIRE(mode==GL_TRIANGLES);REQUIRE(type==GL_UNSIGNED_INT);REQUIRE(ranges>0);
        draws.emplace_back();auto& row=draws.back();row.ranges=static_cast<std::size_t>(ranges);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&row.vao);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&row.ebo);
        glGetIntegerv(GL_CURRENT_PROGRAM,&row.program);REQUIRE(row.vao>0);REQUIRE(row.ebo>0);REQUIRE(row.program>0);
        GLint active_texture{};glGetIntegerv(GL_ACTIVE_TEXTURE,&active_texture);
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&row.textures[0]);
        glActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&row.textures[1]);
        glActiveTexture(GL_TEXTURE2);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&row.textures[2]);
        glActiveTexture(static_cast<GLenum>(active_texture));
        row.uniforms=uniform_signature(static_cast<GLuint>(row.program));
        row.raster_state=raster_state_signature();
        for(GLsizei range=0;range<ranges;++range) {
            REQUIRE(counts[range]>0);REQUIRE(counts[range]%3==0);
            const auto first=row.indices.size();row.indices.resize(first+static_cast<std::size_t>(counts[range]));
            row.index_ranges.push_back({reinterpret_cast<std::uintptr_t>(offsets[range]),
                static_cast<std::uint32_t>(counts[range]),first});
            glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLintptr>(reinterpret_cast<std::uintptr_t>(offsets[range])),
                static_cast<GLsizeiptr>(counts[range])*sizeof(std::uint32_t),row.indices.data()+first);
        }
        GLint current{};glGetQueryiv(GL_SAMPLES_PASSED,GL_CURRENT_QUERY,&current);REQUIRE(current==0);
        glGenQueries(1,&row.query);glBeginQuery(GL_SAMPLES_PASSED,row.query);
        return row;
    }
    static void GLAD_API_PTR draw(GLenum mode,GLsizei count,GLenum type,const void* indices) {
        if(active->split_sky(mode,type,&count,&indices,1))return;
        active->begin(mode,type,&count,&indices,1);
        active->saved_draw(mode,count,type,indices);glEndQuery(GL_SAMPLES_PASSED);
    }
    static void GLAD_API_PTR multi(GLenum mode,const GLsizei* counts,GLenum type,const void* const* offsets,GLsizei ranges) {
        if(active->split_sky(mode,type,counts,offsets,ranges))return;
        active->begin(mode,type,counts,offsets,ranges);
        active->saved_multi(mode,counts,type,offsets,ranges);glEndQuery(GL_SAMPLES_PASSED);
    }
    bool split_sky(GLenum mode,GLenum type,const GLsizei* counts,const void* const* offsets,GLsizei ranges) {
        if(!sky_classes)return false;
        GLint vao{};glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        if(vao!=classified_world_vao)return false;
        REQUIRE(mode==GL_TRIANGLES);REQUIRE(type==GL_UNSIGNED_INT);
        struct Group {std::uintptr_t offset{};GLsizei count{};unsigned classification{};};
        std::vector<Group> groups;bool any_sky=false;
        for(GLsizei range=0;range<ranges;++range) {
            REQUIRE(counts[range]>0);REQUIRE(counts[range]%3==0);
            const auto offset=reinterpret_cast<std::uintptr_t>(offsets[range]);
            std::vector<std::uint32_t> indices(static_cast<std::size_t>(counts[range]));
            glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,static_cast<GLintptr>(offset),
                static_cast<GLsizeiptr>(indices.size()*sizeof(std::uint32_t)),indices.data());
            for(std::size_t i=0;i<indices.size();i+=3U) {
                const auto found=sky_classes->find({indices[i],indices[i+1U],indices[i+2U]});
                const unsigned classification=found==sky_classes->end()?0U:found->second;
                any_sky|=classification!=0U;
                const auto current=offset+i*sizeof(std::uint32_t);
                if(!groups.empty()&&groups.back().classification==classification&&
                    groups.back().offset+static_cast<std::uintptr_t>(groups.back().count)*sizeof(std::uint32_t)==current)
                    groups.back().count+=3;
                else groups.push_back({current,3,classification});
            }
        }
        if(!any_sky)return false;
        for(const auto& group:groups) {
            const auto* offset=reinterpret_cast<const void*>(group.offset);
            auto& row=begin(mode,type,&group.count,&offset,1);row.sky_class=group.classification;
            saved_draw(mode,group.count,type,offset);glEndQuery(GL_SAMPLES_PASSED);
        }
        return true;
    }
    explicit Trace(const std::map<std::array<std::uint32_t,3>,unsigned>* classes=nullptr,GLint world_vao=0)
        :sky_classes{classes},classified_world_vao{world_vao} {
        REQUIRE(active==nullptr);active=this;
        saved_draw=glad_glDrawElements;saved_multi=glad_glMultiDrawElements;saved_clear=glad_glClear;
        glad_glDrawElements=&draw;glad_glMultiDrawElements=&multi;glad_glClear=&clear;
    }
    void finish() {
        glad_glDrawElements=saved_draw;glad_glMultiDrawElements=saved_multi;glad_glClear=saved_clear;active=nullptr;
        for(auto& row:draws) {
            GLuint64 samples{};glGetQueryObjectui64v(row.query,GL_QUERY_RESULT,&samples);row.samples_passed=samples;
            glDeleteQueries(1,&row.query);row.query=0;
        }
        REQUIRE(glGetError()==GL_NO_ERROR);
    }
    ~Trace() {
        if(active==this){glad_glDrawElements=saved_draw;glad_glMultiDrawElements=saved_multi;glad_glClear=saved_clear;active=nullptr;}
        for(auto& row:draws)if(row.query)glDeleteQueries(1,&row.query);
    }
};
inline std::vector<float> read_depth(GLint target,renderer::RenderExtent extent) {
    GLint old_read{},old_pack{},old_alignment{},old_row{},old_x{},old_y{};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&old_pack);
    glGetIntegerv(GL_PACK_ALIGNMENT,&old_alignment);glGetIntegerv(GL_PACK_ROW_LENGTH,&old_row);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&old_x);glGetIntegerv(GL_PACK_SKIP_ROWS,&old_y);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(target));glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
    std::vector<float> depth(static_cast<std::size_t>(extent.width)*extent.height);
    glReadPixels(0,0,extent.width,extent.height,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(old_read));glBindBuffer(GL_PIXEL_PACK_BUFFER,static_cast<GLuint>(old_pack));
    glPixelStorei(GL_PACK_ALIGNMENT,old_alignment);glPixelStorei(GL_PACK_ROW_LENGTH,old_row);glPixelStorei(GL_PACK_SKIP_PIXELS,old_x);glPixelStorei(GL_PACK_SKIP_ROWS,old_y);
    REQUIRE(glGetError()==GL_NO_ERROR);return depth;
}
struct Triangle {
    std::array<std::uint32_t,3> index{};GLint program{};std::array<GLint,3> textures{};std::uint64_t uniforms{},raster_state{};
    bool operator==(const Triangle&) const=default;
};
inline std::vector<Triangle> triangles(const Trace& trace,GLint vao) {
    std::vector<Triangle> result;
    for(const auto& draw:trace.draws)if(draw.vao==vao)
        for(std::size_t i=0;i<draw.indices.size();i+=3U)
            result.push_back({{draw.indices[i],draw.indices[i+1U],draw.indices[i+2U]},draw.program,draw.textures,draw.uniforms,draw.raster_state});
    return result;
}
struct SourceTriangle {std::size_t surface{},material{};bool sky{};};
using SourceLookup=std::map<std::array<std::uint32_t,3>,SourceTriangle>;
inline SourceLookup sources(const world_render::WorldRenderPackage& package) {
    SourceLookup result;const auto indices=package.indices();
    for(const auto& surface:package.surface_ranges())
        for(std::size_t i=surface.first_index;i<surface.first_index+surface.index_count;i+=3U)
            result.emplace(std::array{indices[i],indices[i+1U],indices[i+2U]},
                SourceTriangle{surface.source_world_surface_index,surface.render_material_index,
                    package.materials()[surface.render_material_index].sky_surface});
    return result;
}
inline void print(const Trace& trace,std::string_view policy,std::string_view view,bool capture,GLint world_vao,const SourceLookup& lookup) {
    std::uint64_t total{},static_total{};std::size_t static_ranges{},static_triangles{},static_calls{};
    std::array<std::uint64_t,3> classified_samples{},classified_triangles{};
    for(std::size_t i=0;i<trace.draws.size();++i) {
        const auto& row=trace.draws[i];total+=row.samples_passed;
        if(row.vao==world_vao){static_total+=row.samples_passed;static_ranges+=row.ranges;static_triangles+=row.indices.size()/3U;++static_calls;}
        classified_samples[row.sky_class]+=row.samples_passed;classified_triangles[row.sky_class]+=row.indices.size()/3U;
        std::uint64_t index_signature=14695981039346656037ULL;
        for(const auto index:row.indices)hash_word(index_signature,index);
        struct SourceCount {std::size_t material{},triangles{};bool sky{};};
        std::map<std::size_t,SourceCount> source_counts;
        if(row.vao==world_vao)for(std::size_t j=0;j<row.indices.size();j+=3U) {
            const auto found=lookup.find({row.indices[j],row.indices[j+1U],row.indices[j+2U]});REQUIRE(found!=lookup.end());
            auto& source=source_counts[found->second.surface];source.material=found->second.material;
            source.sky=found->second.sky;++source.triangles;
        }
        std::cout<<"[bsp-causal-draw] {\"policy\":"<<std::quoted(policy)<<",\"view\":"<<std::quoted(view)
            <<",\"remote_capture\":"<<(capture?"true":"false")<<",\"draw\":"<<i<<",\"static_world\":"<<(row.vao==world_vao?"true":"false")
            <<",\"vao\":"<<row.vao<<",\"ebo\":"<<row.ebo<<",\"program\":"<<row.program
            <<",\"base_texture\":"<<row.textures[0]<<",\"lightmap_texture\":"<<row.textures[1]<<",\"sky_texture\":"<<row.textures[2]
            <<",\"uniform_signature\":\""<<row.uniforms<<"\",\"raster_state_signature\":\""<<row.raster_state
            <<"\",\"ranges\":"<<row.ranges<<",\"triangles\":"<<row.indices.size()/3U
            <<",\"index_signature\":\""<<index_signature<<"\",\"samples_passed\":"<<row.samples_passed
            <<",\"sky_class\":"<<row.sky_class<<",\"source_surfaces\":[";
        bool first=true;for(const auto& [source,counts]:source_counts) {
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"source\":"<<source<<",\"material\":"<<counts.material<<",\"triangles\":"<<counts.triangles
                <<",\"sky\":"<<(counts.sky?"true":"false")<<'}';
        }
        std::cout<<"]}\n";
    }
    std::cout<<"[bsp-causal-summary] {\"policy\":"<<std::quoted(policy)<<",\"view\":"<<std::quoted(view)
        <<",\"remote_capture\":"<<(capture?"true":"false")<<",\"static_calls\":"<<static_calls
        <<",\"static_ranges\":"<<static_ranges<<",\"static_triangles\":"<<static_triangles<<",\"static_samples_passed\":"<<static_total
        <<",\"common_sky_triangles\":"<<(trace.sky_classes?std::to_string(classified_triangles[1]):"null")
        <<",\"common_sky_samples_passed\":"<<(trace.sky_classes?std::to_string(classified_samples[1]):"null")
        <<",\"extra_sky_triangles\":"<<(trace.sky_classes?std::to_string(classified_triangles[2]):"null")
        <<",\"extra_sky_samples_passed\":"<<(trace.sky_classes?std::to_string(classified_samples[2]):"null")
        <<",\"all_indexed_samples_passed\":"<<total<<",\"scope\":\"untimed_world_only_samples_passing_depth_not_shader_invocations\"}\n";
}
inline void compare(renderer::opengl::OpenGlRenderer& renderer,const renderer::RenderScene& full,
    renderer::RenderExtent extent,std::string_view view,bool capture) {
    using Policy=renderer::opengl::OpenGlStaticWorldSelectionPolicy;
    auto scene=full;scene.dynamic_entities.reset();scene.first_person_entities.reset();
    REQUIRE(scene.static_world);REQUIRE(scene.static_world->scene_package);REQUIRE(scene.requested_msaa_samples==0U);
    renderer.set_static_world_selection_policy(Policy::legacy_prepared_frustum);
    Trace a;renderer.render(scene,extent);a.finish();
    const auto color_a=renderer.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_a);
    const auto depth_a=read_depth(a.depth_target,extent);
    renderer.set_static_world_selection_policy(Policy::prepared_bsp);
    Trace b;renderer.render(scene,extent);b.finish();
    const auto color_b=renderer.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_b);
    const auto depth_b=read_depth(b.depth_target,extent);
    REQUIRE_FALSE(a.draws.empty());REQUIRE_FALSE(b.draws.empty());
    const auto world_vao=a.draws.front().vao;REQUIRE(b.draws.front().vao==world_vao);
    // Skybox and final adjustments use DrawArrays. The first indexed draw is
    // the owned static-world VAO; later brush VAOs are reported separately.
    const auto ta=triangles(a,world_vao),tb=triangles(b,world_vao);
    const auto lookup=sources(*scene.static_world->scene_package->world_package());
    std::size_t next{},retained_matches{};
    for(const auto& triangle:tb) {
        while(next<ta.size()&&!(ta[next]==triangle))++next;
        if(next==ta.size())break;++next;++retained_matches;
    }
    std::map<std::array<std::uint32_t,3>,std::vector<std::size_t>> baseline_positions;
    for(std::size_t i=0;i<ta.size();++i)baseline_positions[ta[i].index].push_back(i);
    std::size_t common{},missing{},inversions{},ambiguous{},program_differences{},texture_differences{},uniform_differences{},cursor{},details{};
    for(std::size_t i=0;i<tb.size();++i) {
        const auto& triangle=tb[i];const auto found=baseline_positions.find(triangle.index);
        const auto source=lookup.find(triangle.index);REQUIRE(source!=lookup.end());
        bool program_equal=true,texture_equal=true,uniform_equal=true;
        if(found==baseline_positions.end())++missing;
        else {
            ++common;ambiguous+=found->second.size()>1U;
            const auto ordered=std::lower_bound(found->second.begin(),found->second.end(),cursor);
            const auto at=ordered==found->second.end()?found->second.front():*ordered;
            if(at<cursor)++inversions;cursor=at+1U;
            const auto& original=ta[at];
            program_equal=triangle.program==original.program;texture_equal=triangle.textures==original.textures;uniform_equal=triangle.uniforms==original.uniforms;
            program_differences+=!program_equal;texture_differences+=!texture_equal;uniform_differences+=!uniform_equal;
        }
        if(details<16U&&(found==baseline_positions.end()||!program_equal||!texture_equal||!uniform_equal)) {
            ++details;std::cout<<"[bsp-causal-triangle-detail] {\"view\":"<<std::quoted(view)
                <<",\"remote_capture\":"<<(capture?"true":"false")<<",\"candidate_triangle\":"<<i
                <<",\"indices\":["<<triangle.index[0]<<','<<triangle.index[1]<<','<<triangle.index[2]<<"]"
                <<",\"source_surface\":"<<source->second.surface<<",\"material\":"<<source->second.material
                <<",\"sky\":"<<(source->second.sky?"true":"false")
                <<",\"absent_from_legacy_submission\":"<<(found==baseline_positions.end()?"true":"false")
                <<",\"program_equal\":"<<(found==baseline_positions.end()?"null":program_equal?"true":"false")
                <<",\"textures_equal\":"<<(found==baseline_positions.end()?"null":texture_equal?"true":"false")
                <<",\"uniforms_equal\":"<<(found==baseline_positions.end()?"null":uniform_equal?"true":"false")<<"}\n";
        }
    }
    std::size_t color_diff{},depth_diff{};
    REQUIRE(color_a.rgba8.size()==color_b.rgba8.size());REQUIRE(depth_a.size()==depth_b.size());
    for(std::size_t p=0;p<depth_a.size();++p) {
        color_diff+=!std::equal(color_a.rgba8.begin()+p*4U,color_a.rgba8.begin()+p*4U+4U,color_b.rgba8.begin()+p*4U);
        depth_diff+=std::bit_cast<std::uint32_t>(depth_a[p])!=std::bit_cast<std::uint32_t>(depth_b[p]);
    }
    print(a,"legacy",view,capture,world_vao,lookup);print(b,"bsp",view,capture,world_vao,lookup);
    std::cout<<"[bsp-causal-comparison] {\"view\":"<<std::quoted(view)<<",\"remote_capture\":"<<(capture?"true":"false")
        <<",\"legacy_triangles\":"<<ta.size()<<",\"candidate_triangles\":"<<tb.size()
        <<",\"same_order_and_program_texture_uniform_subsequence\":"<<(retained_matches==tb.size()?"true":"false")
        <<",\"matched_triangles\":"<<retained_matches<<",\"common_geometry_triangles\":"<<common
        <<",\"candidate_triangles_absent_from_legacy_submission\":"<<missing<<",\"common_order_inversions\":"<<inversions
        <<",\"ambiguous_duplicate_triangle_keys\":"<<ambiguous<<",\"common_program_differences\":"<<program_differences
        <<",\"common_texture_differences\":"<<texture_differences<<",\"common_uniform_differences\":"<<uniform_differences
        <<",\"color_diff\":"<<color_diff<<",\"depth_diff\":"<<depth_diff<<"}\n";
    CHECK(color_diff==0U);CHECK(depth_diff==0U);
    std::map<std::array<std::uint32_t,3>,unsigned> sky_classes;
    for(const auto& [key,source]:lookup)if(source.sky)
        sky_classes.emplace(key,baseline_positions.contains(key)?1U:2U);
    Trace split{&sky_classes,world_vao};renderer.render(scene,extent);split.finish();
    const auto color_split=renderer.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_split);
    const auto depth_split=read_depth(split.depth_target,extent);
    REQUIRE(color_split.rgba8.size()==color_b.rgba8.size());REQUIRE(depth_split.size()==depth_b.size());
    std::size_t split_color_diff{},split_depth_diff{};
    for(std::size_t p=0;p<depth_b.size();++p) {
        split_color_diff+=!std::equal(color_split.rgba8.begin()+p*4U,color_split.rgba8.begin()+p*4U+4U,color_b.rgba8.begin()+p*4U);
        split_depth_diff+=std::bit_cast<std::uint32_t>(depth_split[p])!=std::bit_cast<std::uint32_t>(depth_b[p]);
    }
    print(split,"bsp_split_sky_class",view,capture,world_vao,lookup);
    std::cout<<"[bsp-causal-sky-split-proof] {\"view\":"<<std::quoted(view)<<",\"remote_capture\":"<<(capture?"true":"false")
        <<",\"color_diff\":"<<split_color_diff<<",\"depth_diff\":"<<split_depth_diff
        <<",\"same_triangle_order_and_state\":"<<(triangles(split,world_vao)==tb?"true":"false")<<"}\n";
    CHECK(split_color_diff==0U);CHECK(split_depth_diff==0U);CHECK(triangles(split,world_vao)==tb);
}
inline void print_order_summary(const Trace& trace,std::string_view selection,std::string_view order,
    std::string_view view,bool capture,GLint world_vao,const SourceLookup& lookup) {
    std::size_t calls{},ranges{},sky_triangles{},ordinary_triangles{},program_changes{},texture_changes{},uniform_changes{},raster_changes{};
    std::uint64_t sky_samples{},ordinary_samples{};const Draw* previous=nullptr;
    for(const auto& draw:trace.draws)if(draw.vao==world_vao) {
        ++calls;ranges+=draw.ranges;std::size_t sky_count{};
        for(std::size_t i=0;i<draw.indices.size();i+=3U) {
            const auto source=lookup.find({draw.indices[i],draw.indices[i+1U],draw.indices[i+2U]});
            REQUIRE(source!=lookup.end());sky_count+=source->second.sky;
        }
        // Material batches may not mix finite sky with ordinary triangles.
        REQUIRE((sky_count==0U||sky_count==draw.indices.size()/3U));
        sky_triangles+=sky_count;ordinary_triangles+=draw.indices.size()/3U-sky_count;
        if(sky_count)sky_samples+=draw.samples_passed;else ordinary_samples+=draw.samples_passed;
        if(previous) {
            program_changes+=draw.program!=previous->program;texture_changes+=draw.textures!=previous->textures;
            uniform_changes+=draw.uniforms!=previous->uniforms;raster_changes+=draw.raster_state!=previous->raster_state;
        }
        previous=&draw;
    }
    std::cout<<"[bsp-sky-order-summary] {\"view\":"<<std::quoted(view)<<",\"remote_capture\":"<<(capture?"true":"false")
        <<",\"selection\":"<<std::quoted(selection)<<",\"order\":"<<std::quoted(order)
        <<",\"static_calls\":"<<calls<<",\"static_ranges\":"<<ranges<<",\"sky_triangles\":"<<sky_triangles
        <<",\"ordinary_triangles\":"<<ordinary_triangles<<",\"sky_samples_passed\":"<<sky_samples
        <<",\"ordinary_samples_passed\":"<<ordinary_samples<<",\"program_transitions\":"<<program_changes
        <<",\"texture_transitions\":"<<texture_changes<<",\"uniform_transitions\":"<<uniform_changes
        <<",\"raster_state_transitions\":"<<raster_changes
        <<",\"gpu_finite_sky_ms\":null,\"gpu_ordinary_static_ms\":null"
        <<",\"scope\":\"untimed_world_only_draw_boundary_state_and_samples_not_shader_invocations\"}\n";
}
inline void compare_sky_order(renderer::opengl::OpenGlRenderer& renderer,const renderer::RenderScene& full,
    renderer::RenderExtent extent,std::string_view view,bool capture,
    renderer::opengl::OpenGlStaticWorldSelectionPolicy restore_selection,
    renderer::opengl::OpenGlStaticWorldOrderPolicy restore_order) {
    using Selection=renderer::opengl::OpenGlStaticWorldSelectionPolicy;
    using Order=renderer::opengl::OpenGlStaticWorldOrderPolicy;
    RestorePolicies restore{renderer,restore_selection,restore_order};
    auto scene=full;scene.dynamic_entities.reset();scene.first_person_entities.reset();
    REQUIRE(scene.static_world);REQUIRE(scene.static_world->scene_package);REQUIRE(scene.requested_msaa_samples==0U);
    const auto lookup=sources(*scene.static_world->scene_package->world_package());
    for(const auto selection:{Selection::legacy_prepared_frustum,Selection::prepared_bsp}) {
        const std::string_view label=selection==Selection::prepared_bsp?"bsp":"legacy";
        renderer.set_static_world_selection_policy(selection);
        renderer.set_static_world_order_policy(Order::original);
        Trace a;renderer.render(scene,extent);a.finish();
        const auto color_a=renderer.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_a);
        const auto depth_a=read_depth(a.depth_target,extent);
        renderer.set_static_world_order_policy(Order::defer_safe_sky);
        Trace b;renderer.render(scene,extent);b.finish();
        const auto color_b=renderer.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_b);
        const auto depth_b=read_depth(b.depth_target,extent);
        REQUIRE_FALSE(a.draws.empty());REQUIRE_FALSE(b.draws.empty());
        const auto world_vao=a.draws.front().vao;REQUIRE(b.draws.front().vao==world_vao);
        const auto ta=triangles(a,world_vao),tb=triangles(b,world_vao);
        auto canonical_a=ta,canonical_b=tb;
        const auto less=[](const Triangle& x,const Triangle& y) {
            return std::tie(x.index,x.program,x.textures,x.uniforms,x.raster_state)
                <std::tie(y.index,y.program,y.textures,y.uniforms,y.raster_state);
        };
        std::sort(canonical_a.begin(),canonical_a.end(),less);std::sort(canonical_b.begin(),canonical_b.end(),less);
        const bool same_geometry_state=canonical_a==canonical_b;
        const auto group=[&](const auto& input,bool sky) {
            std::vector<Triangle> result;
            for(const auto& triangle:input) {
                const auto source=lookup.find(triangle.index);REQUIRE(source!=lookup.end());
                if(source->second.sky==sky)result.push_back(triangle);
            }
            return result;
        };
        const bool same_sky_order=group(ta,true)==group(tb,true);
        const bool same_ordinary_order=group(ta,false)==group(tb,false);
        std::size_t color_diff{},depth_diff{};
        REQUIRE(color_a.rgba8.size()==color_b.rgba8.size());REQUIRE(depth_a.size()==depth_b.size());
        for(std::size_t p=0;p<depth_a.size();++p) {
            color_diff+=!std::equal(color_a.rgba8.begin()+p*4U,color_a.rgba8.begin()+p*4U+4U,color_b.rgba8.begin()+p*4U);
            depth_diff+=std::bit_cast<std::uint32_t>(depth_a[p])!=std::bit_cast<std::uint32_t>(depth_b[p]);
        }
        print(a,std::string(label)+"_original",view,capture,world_vao,lookup);
        print(b,std::string(label)+"_deferred",view,capture,world_vao,lookup);
        print_order_summary(a,label,"original",view,capture,world_vao,lookup);
        print_order_summary(b,label,"deferred",view,capture,world_vao,lookup);
        std::cout<<"[bsp-sky-order-comparison] {\"view\":"<<std::quoted(view)<<",\"remote_capture\":"<<(capture?"true":"false")
            <<",\"selection\":"<<std::quoted(label)<<",\"original_triangles\":"<<ta.size()<<",\"deferred_triangles\":"<<tb.size()
            <<",\"same_geometry_and_state_multiset\":"<<(same_geometry_state?"true":"false")
            <<",\"same_sky_group_order\":"<<(same_sky_order?"true":"false")
            <<",\"same_ordinary_group_order\":"<<(same_ordinary_order?"true":"false")
            <<",\"color_diff\":"<<color_diff<<",\"depth_diff\":"<<depth_diff
            <<",\"boundary\":\"completed_world_before_entities\",\"timed\":false}\n";
        CHECK(same_geometry_state);CHECK(same_sky_order);CHECK(same_ordinary_order);
        CHECK(color_diff==0U);CHECK(depth_diff==0U);
    }
}
}
#endif
