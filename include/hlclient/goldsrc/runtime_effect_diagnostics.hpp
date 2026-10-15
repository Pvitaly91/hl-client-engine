#pragma once
#include <hlclient/goldsrc/runtime_control_decoder.hpp>
#include <type_traits>

namespace hlclient::goldsrc {
// Numeric-only committed wire evidence. Not a rendering/audio success claim.
// detail is TE subtype, sound slot, or zero for svc_particle.
struct RuntimeEffectDiagnostic {
    std::uint32_t opcode{}, detail{}, color{}, amount{}, entity{}, flags{};
};
inline std::optional<RuntimeEffectDiagnostic> runtime_effect_diagnostic(
    const RuntimeControlEvent& event) noexcept {
    RuntimeEffectDiagnostic row;
    row.opcode=static_cast<std::uint8_t>(event.opcode);
    bool observed=true;
    std::visit([&](const auto& body) {
        using T=std::decay_t<decltype(body)>;
        if constexpr(std::is_same_v<T,RuntimeControlSound>) {
            row.detail=body.sound_reference;row.entity=body.entity_reference;
            row.amount=body.volume.value_or(255);row.flags=body.field_mask;
        } else if constexpr(std::is_same_v<T,RuntimeControlExactFixedBody>) {
            if(event.opcode==RuntimeControlOpcode::svc_particle && body.particle_count_color) {
                row.amount=(*body.particle_count_color)[0];row.color=(*body.particle_count_color)[1];
            } else observed=false;
        } else if constexpr(std::is_same_v<T,RuntimeControlBloodSprite>) {
            row.detail=115;row.color=body.palette_color;row.amount=body.scale;
        } else if constexpr(std::is_same_v<T,RuntimeControlExplosion>) {
            row.detail=3;row.amount=body.scale_tenths;row.flags=body.flags;
        } else if constexpr(std::is_same_v<T,RuntimeControlSmoke>) {
            row.detail=5;row.amount=body.scale_tenths;
        } else if constexpr(std::is_same_v<T,RuntimeControlSparks>) row.detail=9;
        else if constexpr(std::is_same_v<T,RuntimeControlBeamFollow>) row.detail=22;
        else if constexpr(std::is_same_v<T,RuntimeControlBubbles>) {
            row.detail=body.trail?114:113;row.amount=body.count;
        } else if constexpr(std::is_same_v<T,RuntimeControlBspDecal> || std::is_same_v<T,RuntimeControlDecal>) {
            row.detail=body.temporary_entity_type;
        } else observed=false;
    },event.body);
    return observed?std::optional{row}:std::nullopt;
}
// Only an approved manifest's virtual name is fingerprinted, never its native
// locator. This permits offline sample correlation without logging paths.
inline std::uint64_t effect_sample_fingerprint(std::string_view name) noexcept {
    std::uint64_t hash=14695981039346656037ULL;
    for(const unsigned char c:name) {hash^=c;hash*=1099511628211ULL;}
    return hash;
}
}
