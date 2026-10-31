#pragma once
// Isolated synthetic fixture observer, not production instrumentation.
// Install after CallCounter, destroy before it. Use ordinary gpu.render(), NOT
// render_with_gpu_timing(): this observer owns the one GL_TIME_ELAPSED target.
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <glad/gl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sky_fixture {
class GpuQueries final {
public:
    using Timing=hlclient::renderer::opengl::OpenGlGpuFrameTiming;
    using Status=hlclient::renderer::opengl::OpenGlGpuTimingStatus;
    struct Frame {
        std::uint64_t frame_number{};
        std::uint64_t sky_draws{},static_draws{},other_draws{},blits{};
        bool begun{},ended{},world_end_at_blit{},world_end_at_render{},invalid{};
        // No absent scope is represented by a valid zero duration.
        Status status{Status::not_observed};
    };
    struct Statistics {
        std::uint64_t submitted{},completed{},capacity_drops{},unavailable{};
        std::size_t pending{},allocated_queries{};
        std::uint64_t invalid_results{},timeout_results{};
        std::uint64_t sky_draws{},static_draws{},other_draws{},blits{};
        std::uint64_t world_end_at_blit{},world_end_at_render{};
        GLint elapsed_counter_bits{},timestamp_counter_bits{};
        bool pass_timestamps_supported{};
        bool disjoint_detection_supported{false}; // No extension/disjoint observation is claimed.
        GLenum query_result_gl_error{GL_NO_ERROR};
    };

    explicit GpuQueries(unsigned frames_total,unsigned msaa,bool expect_fullscreen=true,unsigned expected_world_draws=1,
        bool static_only_map_control=false):
        msaa_(msaa),expect_fullscreen_(expect_fullscreen),expected_world_draws_(expected_world_draws),
        static_only_map_control_(static_only_map_control) {
        require(frames_total>0 && frames_total<=720,"GpuQueries requires 1..720 total frames");
        require(msaa==0 || msaa==4,"GpuQueries supports synthetic AA0/MSAA4 only");
        require(expected_world_draws>=1 && expected_world_draws<=(static_only_map_control?65536U:2U),
            "GpuQueries expected static draws outside the declared control");
        require(owner_==nullptr,"Only one GpuQueries hook owner is supported");
        require(glad_glGenQueries && glad_glBeginQuery && glad_glEndQuery && glad_glQueryCounter &&
            glad_glGetQueryiv && glad_glGetQueryObjectiv && glad_glGetQueryObjectui64v,
            "OpenGL 3.3 timer-query functions unavailable");
        GLint current{};
        glGetQueryiv(GL_TIME_ELAPSED,GL_CURRENT_QUERY,&current);
        require(current==0,"A foreign elapsed query is already active");
        glGetQueryiv(GL_TIME_ELAPSED,GL_QUERY_COUNTER_BITS,&stats_.elapsed_counter_bits);
        glGetQueryiv(GL_TIMESTAMP,GL_QUERY_COUNTER_BITS,&stats_.timestamp_counter_bits);
        // At least48 bits gives >3days of timestamp range. This is a declared
        // capability gate, not an attempted guess through multiple wraps.
        if(stats_.elapsed_counter_bits<48 || stats_.timestamp_counter_bits<48)
            throw std::runtime_error("Insufficient timer-query precision: elapsed_bits="+
                std::to_string(stats_.elapsed_counter_bits)+" timestamp_bits="+
                std::to_string(stats_.timestamp_counter_bits)+" required=48");
        require(glGetError()==GL_NO_ERROR,"GPU timer capability queries failed");
        old_arrays_=glad_glDrawArrays;old_elements_=glad_glDrawElements;old_blit_=glad_glBlitFramebuffer;
        require(old_arrays_ && old_elements_ && old_blit_,"Required synthetic draw functions unavailable");
        frames_.resize(frames_total);issued_.resize(frames_total);
        queries_.resize(static_cast<std::size_t>(frames_total)*7U);
        glGenQueries(static_cast<GLsizei>(queries_.size()),queries_.data());
        if(glGetError()!=GL_NO_ERROR || std::any_of(queries_.begin(),queries_.end(),[](GLuint q){return q==0;})) {
            glDeleteQueries(static_cast<GLsizei>(queries_.size()),queries_.data());
            queries_.clear();throw std::runtime_error("Unable to allocate bounded GPU query set");
        }
        stats_.allocated_queries=queries_.size();stats_.pass_timestamps_supported=true;
        owner_=this;glad_glDrawArrays=&draw_arrays;glad_glDrawElements=&draw_elements;glad_glBlitFramebuffer=&blit;
    }
    ~GpuQueries() noexcept {
        // Exception cleanup ends only the elapsed query which begin() owns.
        if(active_!=none) {glEndQuery(GL_TIME_ELAPSED);active_=none;}
        if(owner_==this) {
            if(glad_glDrawArrays==&draw_arrays)glad_glDrawArrays=old_arrays_;
            if(glad_glDrawElements==&draw_elements)glad_glDrawElements=old_elements_;
            if(glad_glBlitFramebuffer==&blit)glad_glBlitFramebuffer=old_blit_;
            owner_=nullptr;
        }
        if(!queries_.empty())glDeleteQueries(static_cast<GLsizei>(queries_.size()),queries_.data());
    }
    GpuQueries(const GpuQueries&)=delete;
    GpuQueries& operator=(const GpuQueries&)=delete;
    GpuQueries(GpuQueries&&)=delete;
    GpuQueries& operator=(GpuQueries&&)=delete;

    void begin(std::uint64_t frame_number) {
        require(!drained_ && active_==none,"GpuQueries begin while active or after drain");
        require(frame_number>=1 && frame_number<=frames_.size(),"GPU frame number outside preallocated range");
        const auto i=static_cast<std::size_t>(frame_number-1);
        require(!frames_[i].begun,"GPU frame identity reused");
        active_=i;auto& f=frames_[i];f.frame_number=frame_number;f.begun=true;f.status=Status::pending;
        ++stats_.submitted;++stats_.pending;
        glBeginQuery(GL_TIME_ELAPSED,query(i,elapsed));issued_[i]|=1U<<elapsed;
        stamp(world_start);
    }
    void end() {
        require(active_!=none,"GpuQueries end without begin");
        auto& f=frames_[active_];
        if(!f.world_end_at_blit) {
            stamp(world_end);f.world_end_at_render=true;++stats_.world_end_at_render;
            // AA0 has no scene-resolve blit. Its explicit world-end boundary
            // is render-call end. This is intentionally not relabelled as the
            // pre-resolve boundary used by MSAA4.
            if(msaa_!=0)f.invalid=true;
        }
        glEndQuery(GL_TIME_ELAPSED);f.ended=true;
        if(f.sky_draws!=(expect_fullscreen_?1U:0U) || f.static_draws!=expected_world_draws_ || f.other_draws!=0)f.invalid=true;
        if(msaa_==0 && f.world_end_at_blit)f.invalid=true;
        active_=none;
    }

    // Call once only AFTER all timed render/swap iterations. All availability
    // and result queries are confined to this method. No glFinish/fence is used.
    [[nodiscard]] std::vector<Timing> drain(
        std::chrono::milliseconds timeout=std::chrono::milliseconds{30000}) {
        require(active_==none && !drained_,"GpuQueries drain requires ended frames and single invocation");
        require(timeout.count()>0 && timeout<=std::chrono::milliseconds{30000},"GPU drain timeout exceeds 30 seconds");
        drained_=true;
        std::vector<Timing> result;result.reserve(static_cast<std::size_t>(stats_.submitted));
        std::vector<bool> ready(frames_.size());
        for(std::size_t i=0;i<frames_.size();++i) {
            auto& f=frames_[i];
            if(!f.begun)continue;
            if(!f.ended || f.invalid || issued_[i]!=required_issued()) {
                f.invalid=true;f.status=Status::invalid;ready[i]=true;
                ++stats_.invalid_results;--stats_.pending;
            }
        }
        const auto deadline=std::chrono::steady_clock::now()+timeout;
        while(stats_.pending) {
            for(std::size_t i=0;i<frames_.size();++i) {
                auto& f=frames_[i];if(!f.begun || ready[i])continue;
                bool complete=true;
                for(unsigned slot=0;slot<7;++slot) {
                    if((issued_[i]&(1U<<slot))==0)continue;
                    GLint available{};glGetQueryObjectiv(query(i,slot),GL_QUERY_RESULT_AVAILABLE,&available);
                    if(!available){complete=false;break;}
                }
                if(complete) {ready[i]=true;f.status=Status::ready;++stats_.completed;--stats_.pending;}
            }
            if(!stats_.pending || std::chrono::steady_clock::now()>=deadline)break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        for(std::size_t i=0;i<frames_.size();++i) {
            auto& f=frames_[i];if(!f.begun)continue;
            Timing t;t.frame_number=f.frame_number;t.status=f.status;
            if(!ready[i]) {
                // A bounded timeout is explicitly unavailable, never zero GPU.
                f.status=t.status=Status::not_observed;++stats_.timeout_results;++stats_.unavailable;
            } else if(f.status==Status::ready) {
                std::array<GLuint64,7> value{};
                for(unsigned slot=0;slot<7;++slot)if(issued_[i]&(1U<<slot))glGetQueryObjectui64v(query(i,slot),GL_QUERY_RESULT,&value[slot]);
                if(!ordered(value)) {
                    f.invalid=true;f.status=t.status=Status::invalid;
                    ++stats_.invalid_results;--stats_.completed;
                } else {
                    t.elapsed_ms=static_cast<double>(value[elapsed])*1e-6;
                    t.pass_elapsed_ms[0]=static_cast<float>(static_cast<double>(value[world_end]-value[world_start])*1e-6);
                    if(expect_fullscreen_)t.world_subpass_elapsed_ms[1]=static_cast<float>(static_cast<double>(value[sky_end]-value[sky_start])*1e-6);
                    t.world_subpass_elapsed_ms[2]=static_cast<float>(static_cast<double>(value[static_end]-value[static_start])*1e-6);
                    t.valid_pass_mask=1;t.valid_world_subpass_mask=expect_fullscreen_?6:4;
                }
            }
            result.push_back(t);
        }
        stats_.query_result_gl_error=glGetError();
        if(stats_.query_result_gl_error!=GL_NO_ERROR) {
            // No result is accepted if query submission/poll/read produced a GL
            // error. Do not disguise unsupported/disjoint behavior as zero.
            for(auto& t:result)if(t.status==Status::ready) {
                auto& f=frames_[static_cast<std::size_t>(t.frame_number-1)];
                f.invalid=true;f.status=Status::invalid;
                t=Timing{};t.frame_number=f.frame_number;t.status=Status::invalid;
                ++stats_.invalid_results;--stats_.completed;
            }
        }
        return result;
    }
    [[nodiscard]] const Statistics& statistics() const noexcept {return stats_;}
    [[nodiscard]] const std::vector<Frame>& frames() const noexcept {return frames_;}

private:
    static constexpr std::size_t none=static_cast<std::size_t>(-1);
    enum Slot:unsigned {elapsed,world_start,world_end,sky_start,sky_end,static_start,static_end};
    static constexpr unsigned all_issued=(1U<<7U)-1U;
    inline static GpuQueries* owner_{};
    unsigned msaa_{};std::size_t active_{none};bool drained_{};
    bool expect_fullscreen_{true};unsigned expected_world_draws_{1};
    // Explicit immutable real-map control: no Studio/sprites/decals/indexed
    // postpasses are allowed. Cold untimed observation supplies the exact
    // expected count; the old one/two-quad fixture path remains the default.
    bool static_only_map_control_{};
    Statistics stats_;std::vector<Frame> frames_;std::vector<unsigned> issued_;std::vector<GLuint> queries_;
    PFNGLDRAWARRAYSPROC old_arrays_{};PFNGLDRAWELEMENTSPROC old_elements_{};PFNGLBLITFRAMEBUFFERPROC old_blit_{};
    static void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
    [[nodiscard]] GLuint query(std::size_t frame,unsigned slot) const noexcept {return queries_[frame*7U+slot];}
    void stamp(unsigned slot) noexcept {glQueryCounter(query(active_,slot),GL_TIMESTAMP);issued_[active_]|=1U<<slot;}
    [[nodiscard]] unsigned required_issued() const noexcept {return expect_fullscreen_?all_issued:all_issued&~((1U<<sky_start)|(1U<<sky_end));}
    [[nodiscard]] bool ordered(const std::array<GLuint64,7>& v) const noexcept {
        constexpr GLuint64 maximum_interval_ns=60ULL*1000ULL*1000ULL*1000ULL;
        // Counter rollover, reversal and implausibly long individual frames
        // are invalid, not repaired by guessed wrapping arithmetic. Elapsed
        // and timestamp interval are separate queries: equality is NOT assumed.
        if(v[world_end]<v[world_start] || v[world_end]-v[world_start]>maximum_interval_ns ||
            v[elapsed]>maximum_interval_ns)return false;
        const auto inside=[&](unsigned begin,unsigned end) {
            return v[begin]>=v[world_start] && v[end]>=v[begin] && v[end]<=v[world_end];
        };
        return (!expect_fullscreen_ || inside(sky_start,sky_end)) && inside(static_start,static_end);
    }
    static void GLAD_API_PTR draw_arrays(GLenum mode,GLint first,GLsizei count) {
        auto& self=*owner_;const bool tracked=self.active_!=none;
        const bool sky=tracked && mode==GL_TRIANGLES && first==0 && count==3;
        if(sky) {++self.frames_[self.active_].sky_draws;++self.stats_.sky_draws;self.stamp(sky_start);}
        else if(tracked) {++self.frames_[self.active_].other_draws;++self.stats_.other_draws;}
        self.old_arrays_(mode,first,count);
        if(sky)self.stamp(sky_end);
    }
    static void GLAD_API_PTR draw_elements(GLenum mode,GLsizei count,GLenum type,const void* indices) {
        auto& self=*owner_;const bool tracked=self.active_!=none;
        const bool world=tracked && mode==GL_TRIANGLES &&
            (self.static_only_map_control_?count>0:count==6) && type==GL_UNSIGNED_INT;
        if(world) {++self.frames_[self.active_].static_draws;++self.stats_.static_draws;
            if(self.frames_[self.active_].static_draws==1)self.stamp(static_start);}
        else if(tracked) {++self.frames_[self.active_].other_draws;++self.stats_.other_draws;}
        self.old_elements_(mode,count,type,indices);
        if(world && self.frames_[self.active_].static_draws==self.expected_world_draws_)self.stamp(static_end);
    }
    static void GLAD_API_PTR blit(GLint sx0,GLint sy0,GLint sx1,GLint sy1,
        GLint dx0,GLint dy0,GLint dx1,GLint dy1,GLbitfield mask,GLenum filter) {
        auto& self=*owner_;
        if(self.active_!=none) {
            auto& f=self.frames_[self.active_];++f.blits;++self.stats_.blits;
            if(!f.world_end_at_blit) {self.stamp(world_end);f.world_end_at_blit=true;++self.stats_.world_end_at_blit;}
        }
        self.old_blit_(sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1,mask,filter);
    }
};
} // namespace sky_fixture
