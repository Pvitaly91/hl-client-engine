#pragma once

#include <hlclient/client/client_world_state.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::app {

enum class RuntimeReplayVisualBinding : std::uint8_t {
    diagnostic_fixture,
};

enum class RuntimeReplayVisualAssetSource : std::uint8_t {
    project_generated,
};

enum class RuntimeReplayStockModelBinding : std::uint8_t {
    not_verified,
};

struct RuntimeReplayVisualProjectionLimits final {
    std::size_t maximum_entities{64U};
    double maximum_absolute_coordinate{65'536.0};
};

enum class RuntimeReplayVisualProjectionErrorCode : std::uint8_t {
    invalid_configuration,
    asset_build_failed,
    package_build_failed,
    observation_missing,
    generation_mismatch,
    projection_limit_exceeded,
    incomplete_transform,
    non_finite_transform,
    frame_revision_overflow,
    frame_build_failed,
    publication_failed,
    unable_to_retain_candidate,
};

struct RuntimeReplayVisualProjectionError final {
    RuntimeReplayVisualProjectionErrorCode code{
        RuntimeReplayVisualProjectionErrorCode::invalid_configuration};
    std::optional<std::uint32_t> entity_number;
    std::optional<entity_render::EntityRenderFrameErrorCode> frame_error;
    std::string context;
};

struct RuntimeReplayVisualProjectionResult final {
    bool succeeded{false};
    bool frame_changed{false};
    std::optional<RuntimeReplayVisualProjectionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return succeeded && !error.has_value();
    }
};

struct RuntimeReplayVisualProjectionSummary final {
    RuntimeReplayVisualBinding visual_binding{
        RuntimeReplayVisualBinding::diagnostic_fixture};
    RuntimeReplayVisualAssetSource asset_source{
        RuntimeReplayVisualAssetSource::project_generated};
    RuntimeReplayStockModelBinding stock_model_binding{
        RuntimeReplayStockModelBinding::not_verified};
    std::uint64_t generation{0U};
    std::uint64_t presented_runtime_revision{0U};
    std::uint64_t visual_frame_revision{0U};
    std::uint64_t static_resource_id{0U};
    std::uint64_t static_resource_revision{0U};
    std::size_t projected_frame_count{0U};
    std::size_t unchanged_record_count{0U};
    std::size_t resource_build_count{0U};
};

struct RuntimeReplayVisualProjectionCreateResult final {
    std::unique_ptr<class RuntimeReplayVisualProjection> projection;
    std::optional<RuntimeReplayVisualProjectionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return projection != nullptr && !error.has_value();
    }
};

// Application-owned projection of one committed neutral runtime observation
// into the existing immutable entity-render contract. The project-generated
// model is a diagnostic binding, not a modelindex/precache resolver.
class RuntimeReplayVisualProjection final {
public:
    RuntimeReplayVisualProjection(const RuntimeReplayVisualProjection&) = delete;
    RuntimeReplayVisualProjection& operator=(
        const RuntimeReplayVisualProjection&) = delete;
    RuntimeReplayVisualProjection(RuntimeReplayVisualProjection&&) = delete;
    RuntimeReplayVisualProjection& operator=(
        RuntimeReplayVisualProjection&&) = delete;
    ~RuntimeReplayVisualProjection() = default;

    [[nodiscard]] static RuntimeReplayVisualProjectionCreateResult create(
        RuntimeReplayVisualProjectionLimits limits = {});

    [[nodiscard]] RuntimeReplayVisualProjectionResult reset_generation(
        const client::RuntimeClientObservationState& observation,
        client::ClientWorldState& target);
    [[nodiscard]] RuntimeReplayVisualProjectionResult project_entities(
        const client::RuntimeClientObservationState& observation,
        client::ClientWorldState& target);
    [[nodiscard]] RuntimeReplayVisualProjectionResult acknowledge_unchanged(
        const client::RuntimeClientObservationState& observation) noexcept;

    [[nodiscard]] const std::shared_ptr<
        const entity_render::EntitySceneRenderPackage>& package()
        const noexcept;
    [[nodiscard]] RuntimeReplayVisualProjectionSummary summary() const noexcept;

private:
    RuntimeReplayVisualProjection(
        RuntimeReplayVisualProjectionLimits limits,
        std::shared_ptr<const entity_render::EntitySceneRenderPackage> package)
        noexcept;

    RuntimeReplayVisualProjectionLimits limits_;
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> package_;
    std::uint64_t generation_{0U};
    std::uint64_t presented_runtime_revision_{0U};
    std::uint64_t visual_frame_revision_{0U};
    std::size_t projected_frame_count_{0U};
    std::size_t unchanged_record_count_{0U};
};

[[nodiscard]] bool valid_runtime_replay_visual_projection_limits(
    const RuntimeReplayVisualProjectionLimits& limits) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplayVisualBinding binding) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplayVisualAssetSource source) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplayStockModelBinding binding) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplayVisualProjectionErrorCode code) noexcept;

} // namespace hlclient::app
