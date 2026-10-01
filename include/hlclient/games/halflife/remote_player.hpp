#pragma once

#include <hlclient/game_api/remote_player.hpp>

#include <array>

namespace hlclient::games::halflife {

// Visual policy only. Anchors advance on committed source identities, never on
// render calls. There is no player simulation, networking or asset ownership.
class HalfLifeRemotePlayerPresentation final {
public:
    [[nodiscard]] static game_api::RemotePlayerPresentationPolicy policy() noexcept;
    void reset() noexcept;
    [[nodiscard]] game_api::RemotePlayerPresentationIntent sample(
        const game_api::RemotePlayerPresentationContext&) noexcept;

private:
    struct ModelProfile final {
        bool used{}, supported{};
        std::uint64_t resource_id{};
        assets::AssetSourceFingerprint revision{};
        std::size_t bone_count{};
        std::array<std::uint8_t, game_api::kMaximumRemotePlayerBones> lower_mask{};
        std::array<float, 4U> controller_start{}, controller_end{};
    };
    struct FrameAnchor final {
        std::uint32_t sequence{};
        double wire_frame{}, animation_time{}, frame_rate{};
        std::int32_t body{};
        std::uint32_t skin{};
        std::array<std::uint8_t, 4U> controllers{};
        std::array<std::uint8_t, 2U> blending{};
        friend bool operator==(const FrameAnchor&, const FrameAnchor&) = default;
    };
    struct PlayerState final {
        bool used{};
        std::uint64_t network{}, map{}, lifetime{}, model{}, record{}, ordinal{};
        assets::AssetSourceFingerprint revision{};
        double current_time{}, previous_time{};
        double previous_phase{}, current_phase{}, previous_yaw{}, current_yaw{};
        double movement_yaw{}, segment_movement{};
        std::array<double,3U> current_origin{};
        bool moving{}, backwards{};
        std::uint32_t gait_sequence{};
        FrameAnchor main;
        std::optional<game_api::RemotePlayerStudioSample> transition_previous;
        double transition_time{};
        std::uint64_t transition_identity{};
    };
    [[nodiscard]] ModelProfile* profile(const game_api::RemotePlayerPresentationContext&) noexcept;
    static bool derive_profile(const assets::SkeletalModelAssetData&, ModelProfile&) noexcept;
    [[nodiscard]] static std::optional<FrameAnchor> anchor(
        const client::RuntimePacketEntityObservation&, const assets::SkeletalModelAssetData&) noexcept;
    [[nodiscard]] static game_api::RemotePlayerStudioSample main_sample(
        const FrameAnchor&, const assets::SkeletalModelAssetData&, double) noexcept;
    std::array<ModelProfile, game_api::kMaximumRemotePlayers> models_{};
    std::array<PlayerState, game_api::kMaximumRemotePlayers> players_{};
};

} // namespace hlclient::games::halflife
