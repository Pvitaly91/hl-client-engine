#pragma once
#include <hlclient/assets/asset_types.hpp>
#include <array>
#include <memory>
#include <span>
namespace hlclient::audio {
inline constexpr std::size_t maximum_voices=128, maximum_static_voices=64;
inline constexpr std::uint32_t output_rate=48000;
using Vec3=assets::AssetVector3;
enum class Operation { start, stop, change, source, reset };
enum class ChannelPolicy { automatic, replace, ambient };
struct Command {
    Operation operation{};
    std::uint64_t voice{}; // Owner-generated identity, not an entity/resource index.
    std::shared_ptr<const assets::AudioAsset> asset;
    Vec3 origin{};
    float volume{1}, attenuation{1}, pitch{1};
    bool static_voice{}, local{}, loop_enabled{true};
    // Logical due time; submission latency and SDL buffering are separate.
    double scheduled_seconds{};
    bool presentation_voice{};
};
struct Listener { Vec3 origin{}, right{0,-1,0}; float master{0.35F}; bool muted{}; };
struct Statistics { std::uint64_t started{},stopped{},updated{},rejected{},frames{},presentation_started{}; };
// Single consumer; offline sink renders the exact same float stereo PCM as SDL.
class Mixer final {
public:
    void command(const Command&) noexcept;
    void listener(Listener) noexcept;
    void render(std::span<float> stereo) noexcept;
    const Statistics& statistics() const noexcept { return stats_; }
    std::size_t active() const noexcept;
private:
    struct Voice { Command state; double cursor{}; };
    std::array<Voice,maximum_voices> voices_{};
    Listener listener_{};
    Statistics stats_{};
};
}
