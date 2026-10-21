#pragma once

#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <glad/gl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace hlclient::renderer::opengl::detail {

// The renderer's one frame keeps its caller-owned destination/read state even
// when an upload or later pass throws. No platform or game policy lives here.
class SceneTargetFrame final {
public:
    SceneTargetFrame() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_);
        glGetIntegerv(GL_DRAW_BUFFER0,&draw_buffer_);
        if(draw_) {
            GLint count{};glGetIntegerv(GL_MAX_DRAW_BUFFERS,&count);
            if(count<1 || count>256)throw std::runtime_error{"Invalid OpenGL draw buffer capability"};
            draw_buffers_.resize(static_cast<std::size_t>(count));
            for(GLint i=0;i<count;++i) {
                GLint buffer{};glGetIntegerv(GL_DRAW_BUFFER0+static_cast<GLenum>(i),&buffer);
                draw_buffers_[static_cast<std::size_t>(i)]=static_cast<GLenum>(buffer);
            }
        }
        glGetIntegerv(GL_READ_BUFFER,&read_buffer_);
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer_);
        multisample_=glIsEnabled(GL_MULTISAMPLE);
        stencil_=glIsEnabled(GL_STENCIL_TEST);scissor_=glIsEnabled(GL_SCISSOR_TEST);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write_);glGetBooleanv(GL_COLOR_WRITEMASK,color_write_.data());
        // The scene owns the complete viewport, not a caller's clipping mask.
        // In particular an uninitialized MSAA stencil must never gate draws.
        glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }
    ~SceneTargetFrame() noexcept {
        restore_bindings();
        if(multisample_)glEnable(GL_MULTISAMPLE);else glDisable(GL_MULTISAMPLE);
        if(stencil_)glEnable(GL_STENCIL_TEST);else glDisable(GL_STENCIL_TEST);
        if(scissor_)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        glDepthMask(depth_write_);glColorMask(color_write_[0],color_write_[1],color_write_[2],color_write_[3]);
    }
    SceneTargetFrame(const SceneTargetFrame&)=delete;
    SceneTargetFrame& operator=(const SceneTargetFrame&)=delete;
    [[nodiscard]] bool default_back_output() const noexcept {
        return draw_==0 && (draw_buffer_==GL_BACK || draw_buffer_==GL_BACK_LEFT);
    }
    void restore_bindings() const noexcept {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(draw_));
        if(draw_buffers_.empty())glDrawBuffer(static_cast<GLenum>(draw_buffer_));
        else glDrawBuffers(static_cast<GLsizei>(draw_buffers_.size()),draw_buffers_.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(read_));
        glReadBuffer(static_cast<GLenum>(read_buffer_));
        glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(renderbuffer_));
    }
private:
    GLint draw_{},read_{},draw_buffer_{},read_buffer_{},renderbuffer_{};
    GLboolean multisample_{},stencil_{},scissor_{},depth_write_{};
    std::array<GLboolean,4> color_write_{};
    std::vector<GLenum> draw_buffers_;
};

class SceneTarget final {
    struct Storage final {
        GLuint multisample{},resolve{},color{},depth{},texture{};
        Storage()=default;
        Storage(const Storage&)=delete;
        Storage& operator=(const Storage&)=delete;
        Storage(Storage&& other) noexcept { *this=std::move(other); }
        Storage& operator=(Storage&& other) noexcept {
            if(this!=&other) {
                reset();
                multisample=std::exchange(other.multisample,0);
                resolve=std::exchange(other.resolve,0);
                color=std::exchange(other.color,0);
                depth=std::exchange(other.depth,0);
                texture=std::exchange(other.texture,0);
            }
            return *this;
        }
        ~Storage() noexcept {reset();}
        void reset() noexcept {
            if(multisample)glDeleteFramebuffers(1,&multisample);
            if(resolve)glDeleteFramebuffers(1,&resolve);
            if(color)glDeleteRenderbuffers(1,&color);
            if(depth)glDeleteRenderbuffers(1,&depth);
            if(texture)glDeleteTextures(1,&texture);
            multisample=resolve=color=depth=texture=0;
        }
    };
public:
    // Borrowed for the remainder of this render only. The target owns the
    // texture and invalidates it at the next begin(), resize or session reset.
    struct ResolvedColorInput final {
        GLuint texture{};
        RenderExtent extent{};
    };
    void reset_session_resources() noexcept {
        release();statistics_={};allocated_extent_={};initialized_=false;
    }
    void begin(unsigned requested,RenderExtent extent,const SceneTargetFrame& frame) {
        statistics_.scene_extent=extent;
        statistics_.resolve_extent=extent;
        statistics_.hud_extent=extent;
        if(!initialized_ || requested!=statistics_.requested_samples || extent!=allocated_extent_) {
            initialized_=true;
            release();
            statistics_.requested_samples=requested;
            allocated_extent_=extent;
            GLint maximum{},maximum_size{};
            glGetIntegerv(GL_MAX_SAMPLES,&maximum);
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maximum_size);
            statistics_.maximum_samples=static_cast<unsigned>(std::max(maximum,0));
            statistics_.fallback_reason=requested ? OpenGlMsaaFallback::unsupported_count : OpenGlMsaaFallback::disabled;
            if(requested && extent.width>0 && extent.height>0 &&
               extent.width<=maximum_size && extent.height<=maximum_size) {
                // At most 512 MiB of owned color/depth/resolve attachments.
                // GL3.3 has no per-format count enumeration; bounded trial
                // allocation + actual sample queries + completeness establish it.
                constexpr std::uint64_t budget=512ULL*1024ULL*1024ULL;
                const auto pixels=static_cast<std::uint64_t>(extent.width)*static_cast<std::uint64_t>(extent.height);
                for(const unsigned samples:{8U,4U,2U}) {
                    if(samples>requested || samples>statistics_.maximum_samples)continue;
                    if(pixels>budget/(8ULL*samples+4ULL)) {
                        statistics_.fallback_reason=OpenGlMsaaFallback::memory_budget;continue;
                    }
                    if(allocate(samples,extent)) {
                        statistics_.actual_samples=samples;
                        statistics_.framebuffer_complete=true;
                        statistics_.active_framebuffers=2;
                        statistics_.active_attachments=3;
                        ++statistics_.storage_allocations;
                        if(samples==requested)statistics_.fallback_reason=OpenGlMsaaFallback::none;
                        break;
                    }
                }
            }
        }
        frame.restore_bindings();
        if(storage_.multisample) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,storage_.multisample);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glEnable(GL_MULTISAMPLE);
        }
    }
    [[nodiscard]] std::optional<ResolvedColorInput> resolve(
        const SceneTargetFrame& frame,bool wants_adjustment) {
        if(!storage_.multisample)return std::nullopt;
        // Both blits retain exact scene pixel dimensions. The first is the
        // only multisample resolve. For a proven 8-bit linear default back
        // buffer the adjustment shader may sample that owned resolve texture
        // directly, omitting the otherwise necessary output round-trip.
        const auto scissor=glIsEnabled(GL_SCISSOR_TEST);
        const auto srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,storage_.multisample);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,storage_.resolve);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        blit();
        frame.restore_bindings();
        const bool direct=wants_adjustment && frame.default_back_output() &&
            compatible_default_back_output();
        if(!direct) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER,storage_.resolve);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            blit();
            ++statistics_.output_color_blits;
        } else ++statistics_.direct_adjustment_frames;
        frame.restore_bindings();
        if(scissor)glEnable(GL_SCISSOR_TEST);
        if(srgb)glEnable(GL_FRAMEBUFFER_SRGB);
        ++statistics_.resolved_frames;
        if(direct)return ResolvedColorInput{storage_.texture,allocated_extent_};
        return std::nullopt;
    }
    void observe_viewport() noexcept {glGetIntegerv(GL_VIEWPORT,statistics_.viewport.data());}
    [[nodiscard]] const OpenGlSceneTargetStatistics& statistics() const noexcept {return statistics_;}
private:
    [[nodiscard]] bool compatible_default_back_output() {
        if(default_back_compatibility_)return *default_back_compatibility_;
        // Query once per target allocation/context. Custom FBO attachments can
        // be replaced by their caller, so they intentionally use the old path.
        // RGBA8 -> 8-bit RGB(A) preserves the intermediate quantization. An
        // sRGB-capable default attachment is also byte-equivalent here: both
        // the old blit and the adjustment draw disable FRAMEBUFFER_SRGB, and
        // framebuffer pixel-path copies do not perform sRGB conversion.
        GLint samples{},encoding{},component{},red{},green{},blue{},alpha{};
        glGetIntegerv(GL_SAMPLES,&samples);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_BACK_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,&encoding);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_BACK_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE,&component);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_BACK_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE,&red);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_BACK_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE,&green);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_BACK_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE,&blue);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_BACK_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE,&alpha);
        default_back_compatibility_=samples==0 && (encoding==GL_LINEAR || encoding==GL_SRGB) &&
            component==GL_UNSIGNED_NORMALIZED && red==8 && green==8 && blue==8 &&
            (alpha==0 || alpha==8);
        statistics_.cached_default_back_output_compatible=default_back_compatibility_;
        return *default_back_compatibility_;
    }
    void blit() const {
        glBlitFramebuffer(0,0,allocated_extent_.width,allocated_extent_.height,
            0,0,allocated_extent_.width,allocated_extent_.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    void release() noexcept {
        if(storage_.multisample)++statistics_.storage_releases;
        storage_.reset();statistics_.actual_samples=0;statistics_.framebuffer_complete=false;
        statistics_.active_framebuffers=statistics_.active_attachments=0;
        default_back_compatibility_.reset();
        statistics_.cached_default_back_output_compatible.reset();
    }
    [[nodiscard]] bool allocation_error() noexcept {
        bool error=false;
        for(unsigned i=0;i<8;++i) {
            const auto value=glGetError();if(value==GL_NO_ERROR)break;
            error=true;
        }
        if(error)statistics_.fallback_reason=OpenGlMsaaFallback::allocation_failed;
        return error;
    }
    [[nodiscard]] bool allocate(unsigned samples,RenderExtent extent) {
        Storage next;
        glGenFramebuffers(1,&next.multisample);glGenFramebuffers(1,&next.resolve);
        glGenRenderbuffers(1,&next.color);glGenRenderbuffers(1,&next.depth);
        glGenTextures(1,&next.texture);
        if(allocation_error() || !next.multisample || !next.resolve || !next.color || !next.depth || !next.texture) {
            statistics_.fallback_reason=OpenGlMsaaFallback::allocation_failed;return false;
        }
        glBindRenderbuffer(GL_RENDERBUFFER,next.color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,static_cast<GLsizei>(samples),GL_RGBA8,extent.width,extent.height);
        if(allocation_error())return false;
        GLint actual_color{},actual_depth{};
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,&actual_color);
        glBindRenderbuffer(GL_RENDERBUFFER,next.depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,static_cast<GLsizei>(samples),GL_DEPTH24_STENCIL8,extent.width,extent.height);
        if(allocation_error())return false;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,&actual_depth);
        if(actual_color!=static_cast<GLint>(samples) || actual_depth!=actual_color) {
            statistics_.fallback_reason=OpenGlMsaaFallback::attachment_samples_mismatch;return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER,next.multisample);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,next.color);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,next.depth);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        const auto multisample_complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        if(allocation_error())return false;
        if(!multisample_complete) {
            statistics_.fallback_reason=OpenGlMsaaFallback::framebuffer_incomplete;return false;
        }
        GLint prior_texture{},prior_unpack{};
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&prior_texture);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&prior_unpack);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glBindTexture(GL_TEXTURE_2D,next.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent.width,extent.height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(prior_texture));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,static_cast<GLuint>(prior_unpack));
        if(allocation_error())return false;
        glBindFramebuffer(GL_FRAMEBUFFER,next.resolve);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,next.texture,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        const auto complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        if(allocation_error())return false;
        if(!complete){statistics_.fallback_reason=OpenGlMsaaFallback::framebuffer_incomplete;return false;}
        storage_=std::move(next);return true;
    }
    Storage storage_;
    RenderExtent allocated_extent_{};
    bool initialized_{};
    std::optional<bool> default_back_compatibility_;
    OpenGlSceneTargetStatistics statistics_;
};
} // namespace hlclient::renderer::opengl::detail
