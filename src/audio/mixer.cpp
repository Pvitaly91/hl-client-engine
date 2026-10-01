#include <hlclient/audio/mixer.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::audio {
namespace {
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
bool valid(const Command& c) {
    if(!c.asset || !finite(c.origin) || !std::isfinite(c.volume) || c.volume<0 || c.volume>1 ||
        !std::isfinite(c.attenuation) || c.attenuation<0 || c.attenuation>4 ||
        !std::isfinite(c.pitch) || c.pitch<=0 || c.pitch>4) return false;
    const auto& a=*c.asset;
    if(a.sample_rate<4000 || a.sample_rate>192000 || (a.channel_count!=1 && a.channel_count!=2) ||
        a.interleaved_samples.empty() || a.interleaved_samples.size()%a.channel_count) return false;
    const auto n=a.interleaved_samples.size()/a.channel_count;
    return !a.loop || (a.loop->begin<a.loop->end && a.loop->end<=n);
}
}
void Mixer::listener(Listener l) noexcept {
    if(!finite(l.origin)||!finite(l.right)||!std::isfinite(l.master)) return;
    l.master=std::clamp(l.master,0.0F,1.0F); listener_=l;
}
void Mixer::command(const Command& c) noexcept {
    if(c.operation==Operation::reset) { for(auto& v:voices_) v={}; return; }
    for(auto& v:voices_) if(v.state.asset && v.state.voice==c.voice) {
        if(c.operation==Operation::stop) { v={}; ++stats_.stopped; return; }
        if(c.operation==Operation::source) { if(finite(c.origin)) {v.state.origin=c.origin; v.state.local=c.local;} return; }
        if(c.operation==Operation::change) {
            auto next=v.state; next.volume=c.volume; next.pitch=c.pitch;
            if(valid(next)) {v.state=std::move(next); ++stats_.updated;} else ++stats_.rejected;
            return;
        }
        v={}; break;
    }
    if(c.operation!=Operation::start) return;
    if(!valid(c)) {++stats_.rejected; return;}
    if(c.static_voice && std::count_if(voices_.begin(),voices_.end(),[](const auto& v){return v.state.asset && v.state.static_voice;})>=maximum_static_voices) {++stats_.rejected; return;}
    for(auto& v:voices_) if(!v.state.asset) {v={c,0}; ++stats_.started; if(c.presentation_voice) ++stats_.presentation_started; return;}
    ++stats_.rejected; // deterministic drop-new; never steal unrelated looping channels
}
std::size_t Mixer::active() const noexcept {
    return static_cast<std::size_t>(std::count_if(voices_.begin(),voices_.end(),[](const auto& v){return !!v.state.asset;}));
}
void Mixer::render(std::span<float> out) noexcept {
    std::fill(out.begin(),out.end(),0.0F);
    const auto frames=out.size()/2; stats_.frames+=frames;
    for(auto& v:voices_) {
        if(!v.state.asset) continue;
        const auto& a=*v.state.asset;
        const auto count=a.interleaved_samples.size()/a.channel_count;
        const bool looping=a.loop.has_value() && v.state.loop_enabled;
        const auto end=looping ? a.loop->end : count;
        const auto dx=v.state.origin.x-listener_.origin.x, dy=v.state.origin.y-listener_.origin.y, dz=v.state.origin.z-listener_.origin.z;
        const auto distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        const auto pan=v.state.local || v.state.attenuation==0 || distance==0 ? 0.0F : std::clamp((dx*listener_.right.x+dy*listener_.right.y+dz*listener_.right.z)/distance,-1.0F,1.0F);
        const auto gain=listener_.muted ? 0.0F : listener_.master*v.state.volume*(v.state.local ? 1.0F : std::max(0.0F,1.0F-distance*v.state.attenuation/1000.0F));
        const float gains[2]={gain*(1-pan),gain*(1+pan)};
        const double step=static_cast<double>(a.sample_rate)*v.state.pitch/output_rate;
        for(std::size_t f=0;f<frames;++f) {
            if(v.cursor>=static_cast<double>(end)) {
                if(!looping) {v={}; break;}
                v.cursor=a.loop->begin+std::fmod(v.cursor-a.loop->begin,static_cast<double>(end-a.loop->begin));
            }
            const auto i=static_cast<std::size_t>(v.cursor);
            const auto j=i+1<end ? i+1 : looping ? a.loop->begin : i;
            const auto fraction=static_cast<float>(v.cursor-static_cast<double>(i));
            for(std::size_t ch=0;ch<2;++ch) {
                const auto ac=a.channel_count==1 ? 0 : ch;
                const auto x=a.interleaved_samples[i*a.channel_count+ac];
                const auto y=a.interleaved_samples[j*a.channel_count+ac];
                const auto sample=(x+(y-x)*fraction)*gains[ch];
                if(std::isfinite(sample)) out[f*2+ch]+=sample;
            }
            v.cursor+=step;
        }
    }
    for(auto& sample:out) sample=std::isfinite(sample) ? std::clamp(sample,-1.0F,1.0F) : 0.0F;
}
}
