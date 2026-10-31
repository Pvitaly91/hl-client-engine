#pragma once
#include <glad/gl.h>
#include <array>
#include <cstdint>
#include <ostream>
#include <stdexcept>

namespace sky_fixture {
// Identical A/B count-only observation. Wrappers run INSIDE render CPU timing;
// no timestamps, payload logging, GL queries or allocation per observed call.
struct Calls {
    std::uint64_t use_program{},bind_vao{},enable{},disable{},depth_func{},depth_mask{};
    std::uint64_t get_integer{},get_boolean{},get_double{},get_attachment{};
    std::uint64_t draw_arrays{},draw_elements{},array_vertices{},element_indices{};
};
inline void json(std::ostream& o,const Calls& c) {
    o<<"{\"use_program\":"<<c.use_program<<",\"bind_vao\":"<<c.bind_vao
     <<",\"enable\":"<<c.enable<<",\"disable\":"<<c.disable
     <<",\"depth_func\":"<<c.depth_func<<",\"depth_mask\":"<<c.depth_mask
     <<",\"get_integer\":"<<c.get_integer<<",\"get_boolean\":"<<c.get_boolean
     <<",\"get_double\":"<<c.get_double<<",\"get_attachment\":"<<c.get_attachment
     <<",\"draw_arrays\":"<<c.draw_arrays<<",\"draw_elements\":"<<c.draw_elements
     <<",\"array_vertices\":"<<c.array_vertices<<",\"element_indices\":"<<c.element_indices<<'}';
}
class CallCounter {
    inline static CallCounter* active_{};
    PFNGLUSEPROGRAMPROC use_=glad_glUseProgram;PFNGLBINDVERTEXARRAYPROC vao_=glad_glBindVertexArray;
    PFNGLENABLEPROC enable_=glad_glEnable;PFNGLDISABLEPROC disable_=glad_glDisable;
    PFNGLDEPTHFUNCPROC func_=glad_glDepthFunc;PFNGLDEPTHMASKPROC mask_=glad_glDepthMask;
    PFNGLGETINTEGERVPROC integer_=glad_glGetIntegerv;PFNGLGETBOOLEANVPROC boolean_=glad_glGetBooleanv;
    PFNGLGETDOUBLEVPROC double_=glad_glGetDoublev;
    PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC attachment_=glad_glGetFramebufferAttachmentParameteriv;
    PFNGLDRAWARRAYSPROC arrays_=glad_glDrawArrays;PFNGLDRAWELEMENTSPROC elements_=glad_glDrawElements;
    static void GLAD_API_PTR use(GLuint p){auto& s=*active_;if(s.tracking)++s.count.use_program;s.use_(p);}
    static void GLAD_API_PTR vao(GLuint p){auto& s=*active_;if(s.tracking)++s.count.bind_vao;s.vao_(p);}
    static void GLAD_API_PTR enable(GLenum p){auto& s=*active_;if(s.tracking)++s.count.enable;s.enable_(p);}
    static void GLAD_API_PTR disable(GLenum p){auto& s=*active_;if(s.tracking)++s.count.disable;s.disable_(p);}
    static void GLAD_API_PTR func(GLenum p){auto& s=*active_;if(s.tracking)++s.count.depth_func;s.func_(p);}
    static void GLAD_API_PTR mask(GLboolean p){auto& s=*active_;if(s.tracking)++s.count.depth_mask;s.mask_(p);}
    static void GLAD_API_PTR integer(GLenum p,GLint* v){auto& s=*active_;if(s.tracking)++s.count.get_integer;s.integer_(p,v);}
    static void GLAD_API_PTR boolean(GLenum p,GLboolean* v){auto& s=*active_;if(s.tracking)++s.count.get_boolean;s.boolean_(p,v);}
    static void GLAD_API_PTR real(GLenum p,GLdouble* v){auto& s=*active_;if(s.tracking)++s.count.get_double;s.double_(p,v);}
    static void GLAD_API_PTR attachment(GLenum t,GLenum a,GLenum p,GLint* v){auto& s=*active_;if(s.tracking)++s.count.get_attachment;s.attachment_(t,a,p,v);}
    static void GLAD_API_PTR arrays(GLenum m,GLint first,GLsizei n){auto& s=*active_;if(s.tracking){++s.count.draw_arrays;s.count.array_vertices+=static_cast<std::uint64_t>(n);}s.arrays_(m,first,n);}
    static void GLAD_API_PTR elements(GLenum m,GLsizei n,GLenum t,const void* p){auto& s=*active_;if(s.tracking){++s.count.draw_elements;s.count.element_indices+=static_cast<std::uint64_t>(n);}s.elements_(m,n,t,p);}
public:
    bool tracking{};Calls count{};
    struct Pause {
        bool previous{};
        Pause(){if(active_){previous=active_->tracking;active_->tracking=false;}}
        ~Pause(){if(active_)active_->tracking=previous;}
    };
    CallCounter() {
        if(active_)throw std::runtime_error("Nested GL call counter");active_=this;
        glad_glUseProgram=&use;glad_glBindVertexArray=&vao;glad_glEnable=&enable;glad_glDisable=&disable;
        glad_glDepthFunc=&func;glad_glDepthMask=&mask;glad_glGetIntegerv=&integer;glad_glGetBooleanv=&boolean;
        glad_glGetDoublev=&real;glad_glGetFramebufferAttachmentParameteriv=&attachment;glad_glDrawArrays=&arrays;glad_glDrawElements=&elements;
    }
    ~CallCounter(){glad_glUseProgram=use_;glad_glBindVertexArray=vao_;glad_glEnable=enable_;glad_glDisable=disable_;
        glad_glDepthFunc=func_;glad_glDepthMask=mask_;glad_glGetIntegerv=integer_;glad_glGetBooleanv=boolean_;
        glad_glGetDoublev=double_;glad_glGetFramebufferAttachmentParameteriv=attachment_;glad_glDrawArrays=arrays_;glad_glDrawElements=elements_;active_=nullptr;}
    CallCounter(const CallCounter&)=delete;CallCounter& operator=(const CallCounter&)=delete;
};
}
