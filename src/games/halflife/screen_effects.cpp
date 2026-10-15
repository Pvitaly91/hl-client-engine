#include <hlclient/games/halflife/screen_effects.hpp>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace hlclient::games::halflife {
namespace {
bool fresh(const client::RuntimeObservationSource& source,
    std::optional<client::RuntimeObservationSource>& high_water) noexcept {
    if(!source.record_identity || !source.record_ordinal || source.end_bit_offset<=source.start_bit_offset)return false;
    // Identity is opaque, not a timeline. Only the accepted record ordinal is
    // monotonic (replay may use arbitrary unique record identities).
    const auto key=[](const auto& s){return std::tuple{s.record_ordinal,s.start_bit_offset,s.end_bit_offset};};
    if(high_water && key(source)<=key(*high_water))return false;
    high_water=source;return true;
}
}
void HalfLifeScreenEffects::committed(const game_api::GameRecordState& state) noexcept {
    // No presentation clock means no delayed playback on an unrelated frame.
    const bool clock=state.screen_effect_seconds && std::isfinite(*state.screen_effect_seconds) &&
        *state.screen_effect_seconds>=0;
    for(const auto& event:state.screen_shakes) {
        if(!fresh(event.source,shake_high_water_) || !clock)continue;
        const double now=*state.screen_effect_seconds;
        if(event.duration_seconds<=0 || event.frequency_hz<=0 || event.amplitude<=0)continue;
        // One bounded rumble. Overlaps retain the stronger amplitude and later
        // end; they cannot accumulate unbounded offsets or restart on replay.
        auto value=event;
        if(shake_ && now<shake_->start+shake_->value.duration_seconds) {
            value.amplitude=std::max(value.amplitude,shake_->value.amplitude);
            value.duration_seconds=std::max(value.duration_seconds,
                shake_->start+shake_->value.duration_seconds-now);
        }
        shake_=Shake{value,now};
    }
    for(const auto& event:state.screen_fades) {
        if(!fresh(event.source,fade_high_water_) || !clock)continue;
        fade_=Fade{event,*state.screen_effect_seconds};
    }
}
game_api::ScreenEffectsFrame HalfLifeScreenEffects::frame(double now) const noexcept {
    game_api::ScreenEffectsFrame out;
    if(!std::isfinite(now) || now<0)return out;
    if(shake_ && now>=shake_->start) {
        const auto& value=shake_->value;
        const double age=now-shake_->start;
        if(age<value.duration_seconds) {
            // Independently implemented local-compatible smooth rumble, not
            // proprietary engine noise. Receipt-relative time makes it frame-
            // rate independent; fading envelope returns exactly to baseline.
            const double envelope=1-age/value.duration_seconds;
            const double amplitude=value.amplitude*envelope*envelope;
            const double phase=age*value.frequency_hz*6.283185307179586;
            out.translation={static_cast<float>(amplitude*std::sin(phase)),
                static_cast<float>(amplitude*std::sin(phase*.79+1.1)),
                static_cast<float>(amplitude*std::sin(phase*1.17+2.3))};
            out.roll_degrees=amplitude*.2*std::sin(phase*.67+.4);
        }
    }
    if(fade_ && now>=fade_->start) {
        const auto& value=fade_->value;
        const double age=now-fade_->start;
        double factor{};
        if(value.stay_out)factor=1;
        else if(value.fade_out) {
            if(age<value.duration_seconds+value.hold_seconds)
                factor=value.duration_seconds>0 ? std::min(1.,age/value.duration_seconds) : 1.;
        } else if(age<value.hold_seconds)factor=1;
        else if(value.duration_seconds>0)
            factor=std::clamp(1.-(age-value.hold_seconds)/value.duration_seconds,0.,1.);
        out.overlay_rgba=value.rgba;
        out.overlay_rgba[3]*=static_cast<float>(factor);
        out.modulate=value.modulate;
    }
    return out;
}
}
