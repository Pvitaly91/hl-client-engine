#pragma once
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/client/temporary_effect.hpp>
#include <hlclient/game_api/beam_effects.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <map>
namespace hlclient::app {
// Generic committed-entity mechanism. No game names, rocket physics, resource
// locators or player-life ownership. Samples only already-presented origins.
class EntityFollowPresentation final {
public:
    struct Segment {
        assets::AssetVector3 start{},end{};
        std::uint16_t sprite_slot{};
        float width{},opacity{};
        std::array<float,3> color{};
        bool puff{};
        float age_fraction{};
    };
    struct Statistics { std::uint64_t received{},accepted{},duplicate{},invalid{},capacity{},detached{},discontinuity{},sampled{},light_submitted{}; };
    void reset(std::uint64_t) noexcept;
    void observe(const client::RuntimeClientObservationState&);
    void consume(const client::CommittedTemporaryEffect&,double now,
        std::optional<game_api::EntityTrailProfile> = {},std::uint16_t head_sprite_slot=0) noexcept;
    void present(std::span<const client::RuntimePacketEntityObservation>,renderer::RenderScene&,double now) noexcept;
    [[nodiscard]] std::span<const Segment> segments() const noexcept { return {segments_.data(),segment_count_}; }
    [[nodiscard]] std::span<const Segment> heads() const noexcept { return {heads_.data(),head_count_}; }
    [[nodiscard]] const Statistics& statistics() const noexcept { return stats_; }
    [[nodiscard]] std::uint64_t lifetime(std::uint32_t entity) const noexcept;
    [[nodiscard]] std::uint64_t lifetime(std::uint32_t entity,std::uint64_t event_record_ordinal) const noexcept;
private:
    struct Life { std::uint64_t id{};std::optional<std::uint32_t> model;std::uint64_t born_record_ordinal{}; };
    struct Point { assets::AssetVector3 origin{};double time{}; };
    struct Trail {
        bool used{},attached{};std::uint32_t entity{};std::uint64_t lifetime{};
        client::CommittedTemporaryEffect event;
        std::array<Point,256> points{};std::size_t count{};double sampled_at{-1};
        std::optional<game_api::EntityTrailProfile> profile;
        std::uint16_t head_sprite_slot{};
        assets::AssetVector3 last_origin{};
        float spacing_carry{};std::uint64_t sample_ordinal{};bool anchored{};
    };
    std::uint64_t generation_{},next_life_{},ordinal_{};
    std::map<std::uint32_t,Life> lives_;
    std::array<Trail,32> trails_{};
    std::array<std::array<std::uint64_t,3>,64> seen_{};std::size_t next_seen_{};
    std::array<std::uint64_t,3> high_{};
    std::array<Segment,512> segments_{};std::size_t segment_count_{};
    std::array<Segment,32> heads_{};std::size_t head_count_{};
    Statistics stats_;
};
}
