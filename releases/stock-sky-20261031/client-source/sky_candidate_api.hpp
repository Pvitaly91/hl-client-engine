#pragma once
#include "background_sky_policy.hpp"
#include <array>
#include <cstdint>

enum class SkyStateFailure : std::uint32_t {
    depth_write_mask, depth_range, inherited_depth_function, polygon_mode,
    color_write_mask, enabled_capability, enabled_clip_distance, query_error,
    count
};
// Deliberately not valid GL enums: distinguish a scalar core-profile query
// from an implementation which also writes a redundant legacy second value.
inline constexpr int sky_polygon_query_unwritten_first=-19000401;
inline constexpr int sky_polygon_query_unwritten_second=-19000402;
enum class SkyPolygonQueryShape : std::uint32_t {
    unknown_profile, invalid_or_contradictory, core_one_value,
    core_redundant_pair, compatibility_pair
};
struct SkyPolygonQueryWitness {
    std::uint64_t frame{},context{};
    int context_profile_mask{},context_major{},context_minor{},raw_first{},raw_second{};
    SkyPolygonQueryShape shape{};
    unsigned query_group_error{};
    bool context_profile_query_checked{},fill_eligible{};
};
struct SkyStateWitness {
    std::uint64_t frame{};
    SkyStateFailure failure{};
    int depth_function{}, depth_write_mask{}, polygon_front{}, polygon_back{};
    std::array<int,4> color_write_mask{};
    std::array<double,2> depth_range{};
    int framebuffer{}, depth_bits{}, depth_component{}, depth_object{};
    unsigned failed_capability{}, api_error{};
    std::uint64_t context{};
    int context_profile_mask{},context_major{},context_minor{};
    SkyPolygonQueryShape polygon_query_shape{};
    bool context_profile_query_checked{};
};
// Numeric details use doubles only to expose the unchanged conservative test.
struct SkyDepthWitness {
    std::uint64_t frame{},resource_id{},resource_revision{};
    std::uint32_t object_kind{},model_index{0xffffffffU},instance_index{0xffffffffU};
    std::uint32_t first_index{},index_count{},corner{},failure{};
    std::uint32_t source_surface_index{0xffffffffU},actual_range_first{},actual_range_count{};
    std::array<float,6> bounds{};
    std::array<float,16> mvp{};
    std::array<float,3> camera_position{},camera_target{},camera_up{};
    float camera_fov{},camera_near{},camera_far{};
    int width{},height{},depth_bits{},depth_component{},framebuffer{};
    double z{},w{},q{},roundoff{},delta{},scale{},z_magnitude{},w_magnitude{};
    // Initial reproduction deliberately records the old package-union bound.
    // A command range is supplied for context, not claimed to be exact submitted bounds.
    bool whole_package_bounds{true}, exact_submitted_geometry{};
    bool recovered_by_actual_submission{};
};
struct SkyCandidateDiagnostics {
    std::array<std::uint64_t,static_cast<std::size_t>(SkyStateFailure::count)> state_failures{};
    std::uint64_t depth_failures{},state_witness_overflow{},depth_witness_overflow{};
    std::uint32_t state_witness_count{},depth_witness_count{};
    std::array<SkyStateWitness,8> state_witnesses{};
    std::uint32_t polygon_query_witness_count{};
    std::array<SkyPolygonQueryWitness,8> polygon_query_witnesses{};
    std::array<SkyDepthWitness,16> depth_witnesses{};
    std::uint64_t range_tests{},surface_refinements{},prepared_ranges{},prepared_frames{},guard_ns{},prepare_ns{};
    std::uint32_t recovered_depth_witness_count{};
    std::array<SkyDepthWitness,8> recovered_depth_witnesses{};
};
// Task-only diagnostics; no public renderer ABI/config changes.
struct SkyCandidateStats {
    std::uint64_t frames{},late_frames{},early_frames{},fallback_bits{};
    std::uint64_t bounds_tested{},gl_state_queries{};
    std::uint64_t original_frames{};
    // Bits 1,2,4,8,16: sky/resources, world identity, depth attachment,
    // unknown/unsupported GL state, conservative far-depth/bounds guard.
    std::array<std::uint64_t,5> fallback_counts{};
    // Cumulative actual draw/skip counters for the independent diagnostic
    // policies. A skip counts only an available fullscreen cubemap backdrop.
    std::uint64_t fullscreen_sky_draws{},fullscreen_sky_skipped_frames{};
    std::uint64_t finite_sky_textured_draws{},finite_sky_constant_draws{},non_sky_world_draws{};
    // Last render invocation, NOT cumulative/delta counters. The order hash
    // covers every submitted world/brush range (first/count/material/brush),
    // independent of shader/policy/resource IDs. Test-only GL tracing can also
    // compare the following entity/effect passes without production tracing.
    std::uint64_t last_frame_world_draws{},world_draw_order_hash{};
    std::array<float,4> last_frame_clear_color{};
    // Subset of finite_sky_constant_draws: missing cubemap retains original
    // base-2D alpha (and discard only for masked materials) for coverage.
    // Output RGB is constant; this variant has no cubemap sampler.
    std::uint64_t finite_sky_coverage_alpha_draws{};
    // New policy only: omit the complete finite draw for confirmed static
    // model0 BSP SKY, never a depth-only/color-only replacement. A skipped
    // draw is one selected material-homogeneous range, not one source face.
    std::uint64_t static_world_sky_skipped_draws{},static_world_sky_skipped_frames{};
    std::uint64_t unsupported_brush_sky_draws{},static_world_sky_unsupported_draws{};
    // Resource absence does not silently restore finite occlusion. The
    // unchanged early-background/clear behavior owns these frames.
    std::uint64_t static_world_sky_missing_background_frames{};
};
// Configure once before creating a renderer; the policy cannot change in-flight.
extern "C" bool hlc_background_sky_set_policy(BackgroundSkyPolicy);
extern "C" BackgroundSkyPolicy hlc_background_sky_policy();
extern "C" SkyCandidateStats hlc_background_sky_stats();
// Read only at final reporting / offline correctness boundaries, never copy this per frame.
extern "C" const SkyCandidateDiagnostics& hlc_background_sky_diagnostics();
extern "C" void hlc_background_sky_reset_stats();
