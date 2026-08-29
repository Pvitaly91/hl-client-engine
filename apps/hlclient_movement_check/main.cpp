#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_environment.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_math.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/hash/sha256.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_player/local_player_spawn_selector.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace collision = hlclient::collision;
namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_collision = hlclient::goldsrc::collision;
namespace local_player = hlclient::local_player;
namespace movement = hlclient::movement;
namespace kernel = hlclient::goldsrc::movement;

constexpr std::size_t stress_command_count = 10'000U;
constexpr std::size_t maximum_settle_commands = 512U;
constexpr std::size_t radial_direction_count = 64U;
constexpr std::size_t checker_touch_limit =
    kernel::kGoldSrcMovementMaximumTouchesPerCommand;
constexpr std::size_t checker_scratch_hard_limit =
    3U * collision::kCollisionHardMaximumQueryScratchBytes;
constexpr float wall_search_distance = 4'096.0F;
constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

enum class Scenario : std::uint8_t {
    summary, spawn_settle, walk_forward, strafe_wall, jump, step, duck,
    deterministic_route, wall_contact_stress, wall_glance_stress,
    corner_contact_stress, jump_wall_stress, duck_wall_stress,
};

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::optional<std::string> game_directory;
    std::optional<std::string> virtual_map;
    std::optional<Scenario> scenario;
};

struct CommandSpec {
    std::size_t count{1U};
    float forward{0.0F};
    float side{0.0F};
    float yaw{0.0F};
    std::uint16_t buttons{0U};
};

struct WallCandidate {
    std::size_t direction_ordinal{0U};
    double distance{0.0};
    movement::PlayerMovementPlane plane{};
    movement::PlayerMovementHitIdentity hit{};
};

struct ContactSelection {
    WallCandidate wall{};
    std::optional<WallCandidate> corner_wall;
    std::optional<WallCandidate> duck_wall;
    float wall_yaw{0.0F};
    float wall_normal_yaw{0.0F};
    float corner_yaw{0.0F};
    std::uint64_t signature{fnv_offset};
};

struct RouteSummary {
    movement::PlayerMovementStatistics statistics{};
    std::uint64_t final_signature{0U};
    std::uint64_t route_signature{fnv_offset};
    std::uint64_t contact_touches{0U};
    std::uint64_t selected_wall_contact_touches{0U};
    std::uint64_t corner_wall_contact_touches{0U};
    std::uint64_t selected_wall_contact_epochs{0U};
    std::uint64_t release_recontact_epochs{0U};
    std::uint64_t positive_tangent_contact_commands{0U};
    std::uint64_t negative_tangent_contact_commands{0U};
    std::uint64_t airborne_wall_contact_touches{0U};
    std::uint64_t ducked_wall_contact_touches{0U};
    std::uint64_t restored_standing_wall_contact_touches{0U};
    std::size_t first_selected_contact_ordinal{stress_command_count};
    std::size_t last_selected_contact_ordinal{0U};
    std::uint64_t nonpenetrating_checks{0U};
    std::size_t maximum_command_touches{0U};
    std::size_t scratch_retained_bytes{0U};
    std::size_t scratch_primary_retained_bytes{0U};
    std::size_t scratch_direct_candidate_retained_bytes{0U};
    std::size_t scratch_step_candidate_retained_bytes{0U};
    std::size_t diagnostic_maximum_records{0U};
    std::size_t diagnostic_final_records{0U};
    std::uint64_t diagnostic_maximum_overwrites{0U};
    std::uint64_t diagnostic_final_overwrites{0U};
    std::size_t stress_commands{0U};
    std::size_t bootstrap_commands{0U};
    bool grounded{false};
    movement::PlayerMovementHull hull{movement::PlayerMovementHull::standing};
    movement::PlayerMovementMode mode{movement::PlayerMovementMode::airborne};
};

static_assert(checker_touch_limit <=
    kernel::kGoldSrcMovementHardMaximumTouchesPerCommand);
static_assert(checker_scratch_hard_limit /
    collision::kCollisionHardMaximumQueryScratchBytes == 3U);

struct RouteResult {
    std::optional<RouteSummary> summary;
    std::string_view error;
    std::uint32_t command{0U};
    std::uint64_t last_signature{0U};
};

template<class Value, bool = std::is_enum_v<Value>> struct RawIntegral {
    using type = Value;
};
template<class Value> struct RawIntegral<Value, true> {
    using type = std::underlying_type_t<Value>;
};

void hash_byte(std::uint64_t& hash, const std::uint8_t byte) noexcept
{
    hash ^= byte;
    hash *= fnv_prime;
}

template<class Value>
void hash_integral(std::uint64_t& hash, const Value value) noexcept
{
    if constexpr (std::is_same_v<Value, bool>) {
        hash_byte(hash, value ? 1U : 0U);
    } else {
        using Raw = typename RawIntegral<Value>::type;
        using Unsigned = std::make_unsigned_t<Raw>;
        const auto encoded = static_cast<Unsigned>(static_cast<Raw>(value));
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            hash_byte(hash,
                static_cast<std::uint8_t>(encoded >> (index * 8U)));
        }
    }
}

void hash_float(std::uint64_t& hash, const float value) noexcept
{
    hash_integral(hash, std::bit_cast<std::uint32_t>(value));
}
void hash_double(std::uint64_t& hash, const double value) noexcept
{
    hash_integral(hash, std::bit_cast<std::uint64_t>(value));
}

[[nodiscard]] bool finite(const hlclient::assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool is_stress(const Scenario value) noexcept
{
    return value == Scenario::wall_contact_stress ||
        value == Scenario::wall_glance_stress ||
        value == Scenario::corner_contact_stress ||
        value == Scenario::jump_wall_stress ||
        value == Scenario::duck_wall_stress;
}

[[nodiscard]] std::string_view scenario_name(const Scenario value) noexcept
{
    switch (value) {
    case Scenario::summary: return "summary";
    case Scenario::spawn_settle: return "spawn-settle";
    case Scenario::walk_forward: return "walk-forward";
    case Scenario::strafe_wall: return "strafe-wall";
    case Scenario::jump: return "jump";
    case Scenario::step: return "step";
    case Scenario::duck: return "duck";
    case Scenario::deterministic_route: return "deterministic-route";
    case Scenario::wall_contact_stress: return "wall-contact-stress";
    case Scenario::wall_glance_stress: return "wall-glance-stress";
    case Scenario::corner_contact_stress: return "corner-contact-stress";
    case Scenario::jump_wall_stress: return "jump-wall-stress";
    case Scenario::duck_wall_stress: return "duck-wall-stress";
    }
    return "unknown";
}

[[nodiscard]] std::optional<Scenario> parse_scenario(
    const std::string_view value) noexcept
{
    constexpr std::array pairs{
        std::pair{"summary", Scenario::summary},
        std::pair{"spawn-settle", Scenario::spawn_settle},
        std::pair{"walk-forward", Scenario::walk_forward},
        std::pair{"strafe-wall", Scenario::strafe_wall},
        std::pair{"jump", Scenario::jump},
        std::pair{"step", Scenario::step},
        std::pair{"duck", Scenario::duck},
        std::pair{"deterministic-route", Scenario::deterministic_route},
        std::pair{"wall-contact-stress", Scenario::wall_contact_stress},
        std::pair{"wall-glance-stress", Scenario::wall_glance_stress},
        std::pair{"corner-contact-stress", Scenario::corner_contact_stress},
        std::pair{"jump-wall-stress", Scenario::jump_wall_stress},
        std::pair{"duck-wall-stress", Scenario::duck_wall_stress},
    };
    for (const auto& [name, scenario] : pairs) {
        if (value == name) return scenario;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> narrow_ascii(
    const std::wstring_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if (character < 0x20 || character > 0x7e) return std::nullopt;
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int count, wchar_t* arguments[])
{
    Options options;
    for (int index = 1; index < count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (index + 1 >= count || (argument != L"--basedir" &&
            argument != L"--game" && argument != L"--map" &&
            argument != L"--scenario")) return std::nullopt;
        const std::wstring_view value{arguments[++index]};
        if (value.empty()) return std::nullopt;
        if (argument == L"--basedir") {
            if (options.base_directory) return std::nullopt;
            options.base_directory = std::filesystem::path{value};
            continue;
        }
        auto narrow = narrow_ascii(value);
        if (!narrow) return std::nullopt;
        if (argument == L"--game") {
            if (options.game_directory) return std::nullopt;
            options.game_directory = std::move(*narrow);
        } else if (argument == L"--map") {
            if (options.virtual_map) return std::nullopt;
            options.virtual_map = std::move(*narrow);
        } else {
            if (options.scenario) return std::nullopt;
            options.scenario = parse_scenario(*narrow);
            if (!options.scenario) return std::nullopt;
        }
    }
    if (!options.base_directory || !options.game_directory ||
        !options.virtual_map || !options.scenario) return std::nullopt;
    return options;
}

void print_usage()
{
    std::cerr << "Usage: hlclient_movement_check --basedir <Half-Life root> "
        "--game <directory> --map <maps/name.bsp> --scenario "
        "<summary|spawn-settle|walk-forward|strafe-wall|jump|step|duck|"
        "deterministic-route|wall-contact-stress|wall-glance-stress|"
        "corner-contact-stress|jump-wall-stress|duck-wall-stress>\n";
}

void print_failure(const std::string_view classification)
{
    std::cerr << "[movement-error] classification=" << classification << '\n'
              << "[movement] result=failure\n";
}

[[nodiscard]] bool source_terminal(
    const hlclient::local_assets::LocalAssetSourceOpenState state) noexcept
{
    using State = hlclient::local_assets::LocalAssetSourceOpenState;
    return state == State::source_ready || state == State::cancelled ||
        state == State::timed_out || state == State::failed;
}

[[nodiscard]] std::optional<hlclient::local_assets::LocalAssetSource> open_map(
    const std::shared_ptr<const hlclient::local_resources::
        LocalResourceEnvironment>& environment,
    const hlclient::local_resources::LocalResourceLocator& locator)
{
    hlclient::local_assets::LocalAssetSourceOpenLimits limits;
    limits.maximum_source_bytes = bsp::kGoldSrcBspDefaultMaximumSourceBytes;
    limits.maximum_chunks_per_update = 1U;
    limits.maximum_open_sources = 1U;
    hlclient::local_assets::LocalAssetSourceOpener opener;
    auto started = opener.begin(locator, environment, limits);
    if (!started) return std::nullopt;
    auto& operation = *started.operation;
    constexpr auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U; update < 1'000'000U &&
         !source_terminal(operation.state()); ++update) operation.update(now);
    if (operation.state() != hlclient::local_assets::
        LocalAssetSourceOpenState::source_ready) return std::nullopt;
    return operation.take_result();
}

[[nodiscard]] std::vector<CommandSpec> legacy_script(const Scenario scenario)
{
    constexpr auto jump = goldsrc::kSyntheticGoldSrcButtonJump;
    constexpr auto duck = goldsrc::kSyntheticGoldSrcButtonDuck;
    switch (scenario) {
    case Scenario::summary: return {{1U}};
    case Scenario::spawn_settle: return {{100U}};
    case Scenario::walk_forward: return {{40U}, {60U, 240.0F}, {30U}};
    case Scenario::strafe_wall:
        return {{40U}, {100U, 240.0F, 240.0F}, {30U}};
    case Scenario::jump:
        return {{40U}, {1U, 0.0F, 0.0F, 0.0F, jump}, {160U}};
    case Scenario::step: return {{40U}, {180U, 320.0F}, {30U}};
    case Scenario::duck:
        return {{40U}, {30U, 0.0F, 0.0F, 0.0F, duck}, {30U}};
    case Scenario::deterministic_route:
        return {{40U}, {30U, 220.0F}, {20U, 180.0F, 0.0F, 45.0F},
            {20U, 0.0F, 180.0F, 45.0F}, {40U, 0.0F, 0.0F, 45.0F},
            {1U, 0.0F, 0.0F, 45.0F, jump},
            {80U, 0.0F, 0.0F, 45.0F},
            {20U, 100.0F, 0.0F, 45.0F, duck},
            {20U, 100.0F, 0.0F, 45.0F}, {30U, 0.0F, 0.0F, 45.0F}};
    default: return {};
    }
}

[[nodiscard]] bool checked_add(std::uint64_t& target,
    const std::uint64_t value) noexcept
{
    if (value > std::numeric_limits<std::uint64_t>::max() - target) return false;
    target += value;
    return true;
}

[[nodiscard]] bool checked_add(double& target, const double value) noexcept
{
    const auto sum = target + value;
    if (!std::isfinite(value) || !std::isfinite(sum)) return false;
    target = sum;
    return true;
}

struct ScratchRetainedBytes {
    std::size_t primary{0U};
    std::size_t direct_candidate{0U};
    std::size_t step_candidate{0U};
    std::size_t total{0U};
};

[[nodiscard]] std::optional<ScratchRetainedBytes> scratch_retained_bytes(
    const kernel::GoldSrcLocalMovementScratch& scratch) noexcept
{
    ScratchRetainedBytes result;
    result.primary = scratch.collision.retained_bytes();
    result.direct_candidate = scratch.direct_candidate_collision.retained_bytes();
    result.step_candidate = scratch.step_candidate_collision.retained_bytes();
    if (result.direct_candidate > std::numeric_limits<std::size_t>::max() -
            result.primary) {
        return std::nullopt;
    }
    result.total = result.primary + result.direct_candidate;
    if (result.step_candidate > std::numeric_limits<std::size_t>::max() -
            result.total) {
        return std::nullopt;
    }
    result.total += result.step_candidate;
    return result;
}

[[nodiscard]] bool observe_scratch(RouteSummary& summary,
    const kernel::GoldSrcLocalMovementScratch& scratch) noexcept
{
    const auto retained = scratch_retained_bytes(scratch);
    if (!retained || retained->total > checker_scratch_hard_limit) {
        return false;
    }
    summary.scratch_retained_bytes = std::max(
        summary.scratch_retained_bytes, retained->total);
    summary.scratch_primary_retained_bytes = std::max(
        summary.scratch_primary_retained_bytes, retained->primary);
    summary.scratch_direct_candidate_retained_bytes = std::max(
        summary.scratch_direct_candidate_retained_bytes,
        retained->direct_candidate);
    summary.scratch_step_candidate_retained_bytes = std::max(
        summary.scratch_step_candidate_retained_bytes,
        retained->step_candidate);
    summary.diagnostic_maximum_records = std::max(
        summary.diagnostic_maximum_records, scratch.diagnostics.size());
    summary.diagnostic_maximum_overwrites = std::max(
        summary.diagnostic_maximum_overwrites,
        scratch.diagnostics.overwrite_count());
    return scratch.diagnostics.size() <= scratch.diagnostics.capacity();
}

[[nodiscard]] bool aggregate(movement::PlayerMovementStatistics& target,
    const movement::PlayerMovementStatistics& value) noexcept
{
    auto staged = target;
    const bool valid = checked_add(staged.command_count, value.command_count) &&
        checked_add(staged.substep_count, value.substep_count) &&
        checked_add(staged.grounded_command_count, value.grounded_command_count) &&
        checked_add(staged.airborne_command_count, value.airborne_command_count) &&
        checked_add(staged.ground_probe_count, value.ground_probe_count) &&
        checked_add(staged.trace_count, value.trace_count) &&
        checked_add(staged.collision_hit_count, value.collision_hit_count) &&
        checked_add(staged.slide_bump_count, value.slide_bump_count) &&
        checked_add(staged.clip_plane_count, value.clip_plane_count) &&
        checked_add(staged.step_attempt_count, value.step_attempt_count) &&
        checked_add(staged.step_success_count, value.step_success_count) &&
        checked_add(staged.jump_count, value.jump_count) &&
        checked_add(staged.duck_enter_count, value.duck_enter_count) &&
        checked_add(staged.duck_exit_count, value.duck_exit_count) &&
        checked_add(staged.stand_blocked_count, value.stand_blocked_count) &&
        checked_add(staged.start_solid_count, value.start_solid_count) &&
        checked_add(staged.all_solid_count, value.all_solid_count) &&
        checked_add(staged.total_horizontal_distance,
            value.total_horizontal_distance) &&
        checked_add(staged.total_vertical_distance, value.total_vertical_distance);
    if (valid) target = staged;
    return valid;
}

[[nodiscard]] bool same_statistics(const movement::PlayerMovementStatistics& a,
    const movement::PlayerMovementStatistics& b) noexcept
{
    return a.command_count == b.command_count && a.substep_count == b.substep_count &&
        a.grounded_command_count == b.grounded_command_count &&
        a.airborne_command_count == b.airborne_command_count &&
        a.ground_probe_count == b.ground_probe_count && a.trace_count == b.trace_count &&
        a.collision_hit_count == b.collision_hit_count &&
        a.slide_bump_count == b.slide_bump_count &&
        a.clip_plane_count == b.clip_plane_count &&
        a.step_attempt_count == b.step_attempt_count &&
        a.step_success_count == b.step_success_count && a.jump_count == b.jump_count &&
        a.duck_enter_count == b.duck_enter_count &&
        a.duck_exit_count == b.duck_exit_count &&
        a.stand_blocked_count == b.stand_blocked_count &&
        a.start_solid_count == b.start_solid_count &&
        a.all_solid_count == b.all_solid_count &&
        a.total_horizontal_distance == b.total_horizontal_distance &&
        a.total_vertical_distance == b.total_vertical_distance;
}

[[nodiscard]] std::optional<goldsrc::GoldSrcUserCmdState> make_command(
    const std::uint32_t sequence, const CommandSpec& spec)
{
    const auto valid_sequence = goldsrc::GoldSrcUserCmdSequence::create(sequence);
    if (!valid_sequence) return std::nullopt;
    auto info = goldsrc::goldsrc_usercmd_default_create_info(*valid_sequence,
        static_cast<std::int64_t>(sequence) * 10'000'000);
    info.msec = 10U;
    info.sample_duration_nanoseconds = 10'000'000U;
    info.view_angles = {0.0F, spec.yaw, 0.0F};
    info.forward_move = spec.forward;
    info.side_move = spec.side;
    info.buttons = spec.buttons;
    auto made = goldsrc::GoldSrcUserCmdState::create(info);
    if (!made || !made.state) return std::nullopt;
    return std::move(*made.state);
}

[[nodiscard]] RouteResult failed(const std::string_view error,
    const std::uint32_t command,
    const movement::LocalPlayerMovementState& state) noexcept
{
    return {std::nullopt, error, command,
        movement::local_player_movement_state_signature(state)};
}

[[nodiscard]] std::optional<std::pair<movement::LocalPlayerMovementState,
    std::size_t>> settle(movement::LocalPlayerMovementState state,
    const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& collision_source)
{
    std::optional<movement::LocalPlayerMovementState> current{std::move(state)};
    kernel::GoldSrcLocalMovementScratch scratch;
    if (scratch.diagnostics.capacity() !=
        kernel::kPlayerMovementDiagnosticCapacity)
        return std::nullopt;
    for (std::size_t index = 0U; index < maximum_settle_commands; ++index) {
        if (current->ground_state().grounded()) return std::pair{*current, index};
        const auto sequence = current->source_command_sequence() + 1U;
        const auto command = make_command(sequence, {});
        if (!command) return std::nullopt;
        auto result = kernel::GoldSrcLocalMovementKernel::simulate(*current,
            *command, environment, collision_source, scratch);
        if (!result || !result.state) return std::nullopt;
        current.emplace(std::move(*result.state));
    }
    if (!current->ground_state().grounded()) return std::nullopt;
    return std::pair{*current, maximum_settle_commands};
}

[[nodiscard]] hlclient::assets::AssetVector3 radial_direction(
    const std::size_t ordinal) noexcept
{
    constexpr double to_radians = 0.017453292519943295769236907684886;
    const auto angle = static_cast<double>(ordinal) *
        (360.0 / static_cast<double>(radial_direction_count)) * to_radians;
    return {static_cast<float>(std::cos(angle)),
        static_cast<float>(std::sin(angle)), 0.0F};
}

[[nodiscard]] bool equivalent_wall_geometry(
    const movement::PlayerMovementPlane& a,
    const movement::PlayerMovementPlane& b) noexcept
{
    if (!finite(a.normal) || !finite(b.normal) ||
        !std::isfinite(a.distance) || !std::isfinite(b.distance)) {
        return false;
    }
    const auto a_length2 = static_cast<double>(a.normal.x) * a.normal.x +
        static_cast<double>(a.normal.y) * a.normal.y +
        static_cast<double>(a.normal.z) * a.normal.z;
    const auto b_length2 = static_cast<double>(b.normal.x) * b.normal.x +
        static_cast<double>(b.normal.y) * b.normal.y +
        static_cast<double>(b.normal.z) * b.normal.z;
    if (std::abs(a_length2 - 1.0) > 1.0e-4 ||
        std::abs(b_length2 - 1.0) > 1.0e-4) {
        return false;
    }
    const auto dot = static_cast<double>(a.normal.x) * b.normal.x +
        static_cast<double>(a.normal.y) * b.normal.y +
        static_cast<double>(a.normal.z) * b.normal.z;
    return dot >= 0.99999 && std::abs(a.distance - b.distance) <= 1.0e-4;
}

[[nodiscard]] bool same_source_wall(
    const movement::PlayerMovementPlane& a,
    const movement::PlayerMovementPlane& b) noexcept
{
    return a.source_plane_index && b.source_plane_index &&
        a.source_plane_index == b.source_plane_index &&
        equivalent_wall_geometry(a, b);
}

[[nodiscard]] bool matches_selected_wall(
    const movement::PlayerMovementTouch& touch,
    const WallCandidate& selected) noexcept
{
    return touch.hit == selected.hit &&
        same_source_wall(touch.plane, selected.plane);
}

[[nodiscard]] bool usable_wall(const kernel::LocalMovementTrace& trace,
    const hlclient::assets::AssetVector3& direction) noexcept
{
    if (trace.start_solid || trace.all_solid || trace.fraction >= 1.0 ||
        !trace.hit || !trace.collision_plane ||
        !trace.collision_plane->source_plane_index) return false;
    const auto& normal = trace.collision_plane->normal;
    const auto length2 = static_cast<double>(normal.x) * normal.x +
        static_cast<double>(normal.y) * normal.y +
        static_cast<double>(normal.z) * normal.z;
    const auto directional = static_cast<double>(direction.x) * normal.x +
        static_cast<double>(direction.y) * normal.y;
    return finite(normal) && std::isfinite(trace.collision_plane->distance) &&
        std::abs(length2 - 1.0) <= 1.0e-4 && std::abs(normal.z) <= 0.1F &&
        directional < -1.0e-4;
}

[[nodiscard]] std::vector<WallCandidate> discover_walls(
    const movement::LocalPlayerMovementState& state,
    const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& source)
{
    std::vector<WallCandidate> found;
    found.reserve(radial_direction_count);
    collision::CollisionQueryScratch scratch;
    for (std::size_t ordinal = 0U; ordinal < radial_direction_count; ++ordinal) {
        const auto direction = radial_direction(ordinal);
        const auto start = state.origin();
        const hlclient::assets::AssetVector3 end{start.x + direction.x *
            wall_search_distance, start.y + direction.y * wall_search_distance,
            start.z};
        const auto trace = source.trace_hull(start, end,
            movement::PlayerMovementHull::standing, scratch);
        if (!trace || !trace.result || !usable_wall(*trace.result, direction)) continue;
        auto high_start = start;
        high_start.z += environment.step_size() + 1.0F;
        const auto high_position = source.test_position(high_start,
            movement::PlayerMovementHull::standing, scratch);
        if (!high_position || !high_position.result || high_position.result->status !=
            kernel::LocalMovementPositionStatus::free) continue;
        const hlclient::assets::AssetVector3 high_end{high_start.x + direction.x *
            wall_search_distance, high_start.y + direction.y * wall_search_distance,
            high_start.z};
        const auto high_trace = source.trace_hull(high_start, high_end,
            movement::PlayerMovementHull::standing, scratch);
        if (!high_trace || !high_trace.result ||
            !usable_wall(*high_trace.result, direction) ||
            *trace.result->hit != *high_trace.result->hit ||
            !same_source_wall(*trace.result->collision_plane,
                *high_trace.result->collision_plane)) continue;
        found.push_back({ordinal, trace.result->fraction * wall_search_distance,
            *trace.result->collision_plane, *trace.result->hit});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.direction_ordinal != b.direction_ordinal)
            return a.direction_ordinal < b.direction_ordinal;
        return *a.plane.source_plane_index < *b.plane.source_plane_index;
    });
    std::vector<WallCandidate> distinct;
    distinct.reserve(found.size());
    for (const auto& candidate : found) {
        if (std::none_of(distinct.begin(), distinct.end(), [&](const auto& prior) {
                return equivalent_wall_geometry(candidate.plane, prior.plane);
            })) distinct.push_back(candidate);
    }
    return distinct;
}

[[nodiscard]] float wall_route_yaw(const WallCandidate& wall) noexcept
{
    constexpr double to_degrees = 57.295779513082320876798154814105;
    const auto direction = radial_direction(wall.direction_ordinal);
    return static_cast<float>(std::atan2(static_cast<double>(direction.y),
        static_cast<double>(direction.x)) * to_degrees);
}

[[nodiscard]] float wall_inward_normal_yaw(
    const WallCandidate& wall) noexcept
{
    constexpr double to_degrees = 57.295779513082320876798154814105;
    return static_cast<float>(std::atan2(
        -static_cast<double>(wall.plane.normal.y),
        -static_cast<double>(wall.plane.normal.x)) * to_degrees);
}

struct Corner { WallCandidate first; WallCandidate second; double distance;
    float yaw; };

[[nodiscard]] std::optional<Corner> discover_corner(
    const movement::LocalPlayerMovementState& state,
    const std::vector<WallCandidate>& walls,
    const kernel::ILocalMovementCollision& source)
{
    std::vector<Corner> found;
    found.reserve(walls.size());
    collision::CollisionQueryScratch scratch;
    constexpr float clearance = 16.0F;
    const auto origin = state.origin();
    for (std::size_t i = 0U; i < walls.size(); ++i) {
        for (std::size_t j = i + 1U; j < walls.size(); ++j) {
            const auto& a = walls[i]; const auto& b = walls[j];
            const auto determinant = static_cast<double>(a.plane.normal.x) *
                b.plane.normal.y - static_cast<double>(a.plane.normal.y) *
                b.plane.normal.x;
            if (std::abs(determinant) < 0.1) continue;
            const auto x = (a.plane.distance * b.plane.normal.y -
                static_cast<double>(a.plane.normal.y) * b.plane.distance) /
                determinant;
            const auto y = (static_cast<double>(a.plane.normal.x) *
                b.plane.distance - a.plane.distance * b.plane.normal.x) /
                determinant;
            const auto dx = x - origin.x; const auto dy = y - origin.y;
            const auto distance = std::hypot(dx, dy);
            if (!std::isfinite(distance) || distance <= 1.0 ||
                distance > wall_search_distance ||
                dx * -a.plane.normal.x + dy * -a.plane.normal.y <= 1.0 ||
                dx * -b.plane.normal.x + dy * -b.plane.normal.y <= 1.0) continue;
            const hlclient::assets::AssetVector3 open{
                static_cast<float>(x) + clearance *
                    (a.plane.normal.x + b.plane.normal.x),
                static_cast<float>(y) + clearance *
                    (a.plane.normal.y + b.plane.normal.y), origin.z};
            const auto free = source.test_position(open,
                movement::PlayerMovementHull::standing, scratch);
            if (!free || !free.result || free.result->status !=
                kernel::LocalMovementPositionStatus::free) continue;
            const hlclient::assets::AssetVector3 through_a{open.x -
                a.plane.normal.x * clearance * 4.0F, open.y -
                a.plane.normal.y * clearance * 4.0F, open.z};
            const hlclient::assets::AssetVector3 through_b{open.x -
                b.plane.normal.x * clearance * 4.0F, open.y -
                b.plane.normal.y * clearance * 4.0F, open.z};
            const auto trace_a = source.trace_hull(open, through_a,
                movement::PlayerMovementHull::standing, scratch);
            const auto trace_b = source.trace_hull(open, through_b,
                movement::PlayerMovementHull::standing, scratch);
            if (!trace_a || !trace_a.result || !trace_a.result->collision_plane ||
                !trace_a.result->hit || *trace_a.result->hit != a.hit ||
                !trace_b || !trace_b.result || !trace_b.result->collision_plane ||
                !trace_b.result->hit || *trace_b.result->hit != b.hit ||
                !same_source_wall(a.plane, *trace_a.result->collision_plane) ||
                !same_source_wall(b.plane, *trace_b.result->collision_plane)) {
                continue;
            }
            constexpr double to_degrees = 57.295779513082320876798154814105;
            found.push_back({a, b, distance,
                static_cast<float>(std::atan2(dy, dx) * to_degrees)});
        }
    }
    if (found.empty()) return std::nullopt;
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.first.direction_ordinal != b.first.direction_ordinal)
            return a.first.direction_ordinal < b.first.direction_ordinal;
        if (a.second.direction_ordinal != b.second.direction_ordinal)
            return a.second.direction_ordinal < b.second.direction_ordinal;
        if (*a.first.plane.source_plane_index != *b.first.plane.source_plane_index)
            return *a.first.plane.source_plane_index < *b.first.plane.source_plane_index;
        return *a.second.plane.source_plane_index < *b.second.plane.source_plane_index;
    });
    return found.front();
}

[[nodiscard]] std::optional<WallCandidate> discover_duck_wall(
    const movement::LocalPlayerMovementState& state,
    const WallCandidate& standing_wall,
    const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& source)
{
    CommandSpec duck_command;
    duck_command.buttons = goldsrc::kSyntheticGoldSrcButtonDuck;
    const auto command = make_command(
        state.source_command_sequence() + 1U, duck_command);
    if (!command) return std::nullopt;
    kernel::GoldSrcLocalMovementScratch movement_scratch;
    auto ducked = kernel::GoldSrcLocalMovementKernel::simulate(state, *command,
        environment, source, movement_scratch);
    if (!ducked || !ducked.state ||
        ducked.state->hull() != movement::PlayerMovementHull::ducked) {
        return std::nullopt;
    }

    const auto direction = radial_direction(standing_wall.direction_ordinal);
    const auto start = ducked.state->origin();
    const hlclient::assets::AssetVector3 end{
        start.x + direction.x * wall_search_distance,
        start.y + direction.y * wall_search_distance,
        start.z};
    collision::CollisionQueryScratch collision_scratch;
    const auto trace = source.trace_hull(start, end,
        movement::PlayerMovementHull::ducked, collision_scratch);
    if (!trace || !trace.result || !usable_wall(*trace.result, direction) ||
        *trace.result->hit != standing_wall.hit ||
        !equivalent_wall_geometry(*trace.result->collision_plane,
            standing_wall.plane)) {
        return std::nullopt;
    }
    return WallCandidate{standing_wall.direction_ordinal,
        trace.result->fraction * wall_search_distance,
        *trace.result->collision_plane, *trace.result->hit};
}

[[nodiscard]] std::optional<ContactSelection> select_contacts(
    const movement::LocalPlayerMovementState& state,
    const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& source, const bool need_corner,
    const bool need_duck_wall)
{
    const auto walls = discover_walls(state, environment, source);
    if (walls.empty()) return std::nullopt;
    ContactSelection result;
    result.wall = walls.front();
    result.wall_yaw = wall_route_yaw(result.wall);
    result.wall_normal_yaw = wall_inward_normal_yaw(result.wall);
    if (need_corner) {
        const auto corner = discover_corner(state, walls, source);
        if (!corner) return std::nullopt;
        result.wall = corner->first; result.corner_wall = corner->second;
        result.wall_yaw = wall_route_yaw(result.wall);
        result.wall_normal_yaw = wall_inward_normal_yaw(result.wall);
        result.corner_yaw = corner->yaw;
    }
    if (need_duck_wall) {
        result.duck_wall = discover_duck_wall(
            state, result.wall, environment, source);
        if (!result.duck_wall) return std::nullopt;
    }
    hash_integral(result.signature, result.wall.direction_ordinal);
    hash_integral(result.signature, *result.wall.plane.source_plane_index);
    hash_double(result.signature, result.wall.distance);
    hash_float(result.signature, result.wall.plane.normal.x);
    hash_float(result.signature, result.wall.plane.normal.y);
    hash_float(result.signature, result.wall.plane.normal.z);
    if (result.corner_wall) {
        hash_integral(result.signature, result.corner_wall->direction_ordinal);
        hash_integral(result.signature,
            *result.corner_wall->plane.source_plane_index);
        hash_double(result.signature, result.corner_wall->distance);
    }
    if (result.duck_wall) {
        hash_integral(result.signature,
            result.duck_wall->direction_ordinal);
        hash_integral(result.signature,
            *result.duck_wall->plane.source_plane_index);
        hash_double(result.signature, result.duck_wall->distance);
        hash_float(result.signature, result.duck_wall->plane.normal.x);
        hash_float(result.signature, result.duck_wall->plane.normal.y);
        hash_float(result.signature, result.duck_wall->plane.normal.z);
    }
    return result;
}

[[nodiscard]] CommandSpec stress_command(const Scenario scenario,
    const std::size_t ordinal, const ContactSelection& contact,
    const bool selected_wall_contacted) noexcept
{
    CommandSpec result;
    result.yaw = scenario == Scenario::corner_contact_stress ?
        contact.corner_yaw : selected_wall_contacted ?
            contact.wall_normal_yaw : contact.wall_yaw;
    if (scenario == Scenario::wall_contact_stress) {
        const auto phase = ordinal % 1'000U;
        result.forward = phase < 600U ? 2'047.0F : phase < 700U ? 0.0F :
            phase < 800U ? -320.0F : 320.0F;
    } else if (scenario == Scenario::wall_glance_stress) {
        const auto phase = ordinal % 400U;
        if (phase < 100U) result.forward = 320.0F;
        else if (phase < 200U) { result.forward = 320.0F; result.side = 56.424F; }
        else if (phase < 300U) { result.forward = 320.0F; result.side = -56.424F; }
        else if (phase < 350U) { result.forward = 320.0F; result.side = 320.0F; }
        else result.side = 320.0F;
    } else if (scenario == Scenario::corner_contact_stress) {
        result.forward = 320.0F;
    } else if (scenario == Scenario::jump_wall_stress) {
        result.forward = 320.0F;
        if (ordinal % 50U == 0U) result.buttons = goldsrc::kSyntheticGoldSrcButtonJump;
    } else if (scenario == Scenario::duck_wall_stress) {
        result.forward = 320.0F;
        if ((ordinal / 50U) % 2U == 0U)
            result.buttons = goldsrc::kSyntheticGoldSrcButtonDuck;
    }
    return result;
}

void hash_result(std::uint64_t& hash, const CommandSpec& command,
    const kernel::LocalMovementSimulationResult& result) noexcept
{
    hash_float(hash, command.forward); hash_float(hash, command.side);
    hash_float(hash, command.yaw); hash_integral(hash, command.buttons);
    hash_integral(hash, result.deterministic_state_signature);
    hash_integral(hash, result.touches.size());
    hash_integral(hash, result.statistics.trace_count);
    hash_integral(hash, result.statistics.collision_hit_count);
    hash_integral(hash, result.statistics.slide_bump_count);
    hash_integral(hash, result.statistics.clip_plane_count);
    for (const auto& touch : result.touches) {
        hash_integral(hash, touch.hit.kind);
        hash_integral(hash, touch.hit.source_model_index);
        hash_integral(hash, touch.plane.source_plane_index.has_value());
        if (touch.plane.source_plane_index)
            hash_integral(hash, *touch.plane.source_plane_index);
        hash_integral(hash, touch.phase); hash_double(hash, touch.fraction);
    }
}

struct SelectedContactMatches {
    std::uint64_t selected_wall{0U};
    std::uint64_t standing_wall{0U};
    std::uint64_t duck_wall{0U};
    std::uint64_t corner_wall{0U};
};

[[nodiscard]] SelectedContactMatches contact_touches(
    const kernel::LocalMovementSimulationResult& result,
    const ContactSelection& contact) noexcept
{
    SelectedContactMatches matches;
    for (const auto& touch : result.touches) {
        const bool standing_match = matches_selected_wall(touch, contact.wall);
        const bool duck_match = contact.duck_wall &&
            matches_selected_wall(touch, *contact.duck_wall);
        if (standing_match) ++matches.standing_wall;
        if (duck_match) ++matches.duck_wall;
        if (standing_match || duck_match) ++matches.selected_wall;
        if (contact.corner_wall &&
            matches_selected_wall(touch, *contact.corner_wall)) {
            ++matches.corner_wall;
        }
    }
    return matches;
}

void print_stress_phase_failure_metrics(const RouteSummary& summary,
    const ContactSelection& contact,
    const std::size_t normal_alignment_commands)
{
    std::cerr << "[movement-error] phase-metrics contact-epochs=" <<
        summary.selected_wall_contact_epochs << " recontacts=" <<
        summary.release_recontact_epochs << " selected-touches=" <<
        summary.selected_wall_contact_touches << " first-contact-ordinal=" <<
        summary.first_selected_contact_ordinal << " last-contact-ordinal=" <<
        summary.last_selected_contact_ordinal << " tangent-positive=" <<
        summary.positive_tangent_contact_commands << " tangent-negative=" <<
        summary.negative_tangent_contact_commands << " jump-count=" <<
        summary.statistics.jump_count << " airborne-wall-touches=" <<
        summary.airborne_wall_contact_touches << " duck-enter=" <<
        summary.statistics.duck_enter_count << " duck-exit=" <<
        summary.statistics.duck_exit_count << " ducked-wall-touches=" <<
        summary.ducked_wall_contact_touches << " restored-wall-touches=" <<
        summary.restored_standing_wall_contact_touches << '\n';
    std::cerr << "[movement-error] normal-alignment-commands=" <<
        normal_alignment_commands << '\n';
    if (contact.duck_wall) {
        std::cerr << "[movement-error] phase-wall-planes standing=" <<
            *contact.wall.plane.source_plane_index << " ducked=" <<
            *contact.duck_wall->plane.source_plane_index << '\n';
    }
}

[[nodiscard]] RouteResult run_stress(movement::LocalPlayerMovementState state,
    const Scenario scenario, const ContactSelection& contact,
    const std::size_t bootstrap, const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& source)
{
    std::optional<movement::LocalPlayerMovementState> current{std::move(state)};
    RouteSummary summary; summary.stress_commands = stress_command_count;
    summary.bootstrap_commands = bootstrap;
    kernel::GoldSrcLocalMovementScratch scratch;
    if (scratch.diagnostics.capacity() !=
        kernel::kPlayerMovementDiagnosticCapacity)
        return failed("diagnostic_capacity_mismatch", 0U, *current);
    collision::CollisionQueryScratch position_scratch;
    bool selected_contact_active = false;
    bool selected_contact_seen = false;
    bool explicit_release_observed = false;
    bool normal_alignment_complete = false;
    bool actual_jump_observed = false;
    bool actual_duck_enter_observed = false;
    bool actual_duck_exit_observed = false;
    std::size_t normal_alignment_commands = 0U;
    for (std::size_t ordinal = 0U; ordinal < stress_command_count; ++ordinal) {
        const auto sequence = current->source_command_sequence() + 1U;
        auto spec = stress_command(
            scenario, ordinal, contact, selected_contact_seen);
        bool tangent_alignment_command = false;
        if (selected_contact_seen &&
            scenario != Scenario::corner_contact_stress &&
            !normal_alignment_complete) {
            const auto tangent_speed =
                -static_cast<double>(contact.wall.plane.normal.y) *
                    current->velocity().x +
                static_cast<double>(contact.wall.plane.normal.x) *
                    current->velocity().y;
            constexpr float command_duration_seconds = 0.010F;
            auto braking_tangent_speed = tangent_speed;
            if (current->mode() != movement::PlayerMovementMode::airborne) {
                const auto friction = kernel::apply_horizontal_ground_friction(
                    current->velocity(), environment.stop_speed(),
                    environment.friction(), current->friction_multiplier(),
                    command_duration_seconds);
                if (friction && friction.value) {
                    braking_tangent_speed =
                        -static_cast<double>(contact.wall.plane.normal.y) *
                            friction.value->x +
                        static_cast<double>(contact.wall.plane.normal.x) *
                            friction.value->y;
                }
            }
            if (std::abs(braking_tangent_speed) <=
                kernel::kGoldSrcMovementStopEpsilon) {
                normal_alignment_complete = true;
            } else {
                const auto acceleration = current->mode() ==
                        movement::PlayerMovementMode::airborne ?
                    environment.air_acceleration() : environment.acceleration();
                const auto acceleration_per_wish_speed =
                    static_cast<double>(acceleration) *
                    command_duration_seconds *
                    current->friction_multiplier();
                if (std::isfinite(acceleration_per_wish_speed) &&
                    acceleration_per_wish_speed > 0.0) {
                    const auto maximum_speed =
                        static_cast<double>(environment.maximum_speed());
                    const auto normal_pressure = maximum_speed * 0.1;
                    const auto maximum_brake_speed = std::sqrt(std::max(0.0,
                        maximum_speed * maximum_speed -
                            normal_pressure * normal_pressure));
                    const auto brake_speed = std::min(maximum_brake_speed,
                        std::abs(braking_tangent_speed) /
                            acceleration_per_wish_speed);
                    spec.forward = static_cast<float>(normal_pressure);
                    spec.side = static_cast<float>(std::copysign(
                        brake_speed, -braking_tangent_speed));
                    spec.buttons = 0U;
                    tangent_alignment_command = true;
                    ++normal_alignment_commands;
                }
            }
        }
        const auto command = make_command(sequence, spec);
        if (!command) return failed("command_creation_failed", sequence, *current);
        const auto prior_origin = current->origin();
        const auto prior_mode = current->mode();
        const auto prior_hull = current->hull();
        auto result = kernel::GoldSrcLocalMovementKernel::simulate(*current, *command,
            environment, source, scratch);
        if (!result || !result.state) return failed(result.error ?
            kernel::to_string(result.error->code) :
            std::string_view{"movement_simulation_failed"}, sequence, *current);
        if (result.touches.size() > checker_touch_limit)
            return failed("touch_limit_exceeded", sequence, *current);
        if (!finite(result.state->origin()) || !finite(result.state->velocity()) ||
            !finite(result.state->view_angles()))
            return failed("non_finite_successor", sequence, *current);
        const auto position = source.test_position(result.state->origin(),
            result.state->hull(), position_scratch);
        if (!position || !position.result)
            return failed("position_validation_failed", sequence, *current);
        if (position.result->status != kernel::LocalMovementPositionStatus::free)
            return failed("successor_penetrated_world", sequence, *current);
        if (!aggregate(summary.statistics, result.statistics))
            return failed("statistics_overflow", sequence, *current);
        const auto contacts = contact_touches(result, contact);
        if (!checked_add(summary.contact_touches,
                contacts.selected_wall + contacts.corner_wall)) {
            return failed("contact_counter_overflow", sequence, *current);
        }
        if (!checked_add(summary.selected_wall_contact_touches,
                contacts.selected_wall)) {
            return failed("wall_contact_counter_overflow", sequence, *current);
        }
        if (!checked_add(summary.corner_wall_contact_touches,
                contacts.corner_wall)) {
            return failed("corner_contact_counter_overflow", sequence, *current);
        }
        const bool selected_contact = contacts.selected_wall != 0U;
        if (selected_contact && !selected_contact_seen) {
            summary.first_selected_contact_ordinal = ordinal;
        }
        if (selected_contact) summary.last_selected_contact_ordinal = ordinal;
        bool physical_release = false;
        bool release_phase = false;
        if (scenario == Scenario::wall_contact_stress) {
            const auto phase = ordinal % 1'000U;
            release_phase = phase >= 600U && phase < 800U;
            if (release_phase && selected_contact_seen) {
                const auto successor_origin = result.state->origin();
                constexpr float release_probe_distance = 64.0F;
                const hlclient::assets::AssetVector3 release_probe_end{
                    successor_origin.x - contact.wall.plane.normal.x *
                        release_probe_distance,
                    successor_origin.y - contact.wall.plane.normal.y *
                        release_probe_distance,
                    successor_origin.z - contact.wall.plane.normal.z *
                        release_probe_distance};
                const auto release_probe = source.trace_hull(successor_origin,
                    release_probe_end, movement::PlayerMovementHull::standing,
                    position_scratch);
                constexpr double physical_release_distance = 1.0;
                physical_release = release_probe && release_probe.result &&
                    usable_wall(*release_probe.result,
                        {-contact.wall.plane.normal.x,
                            -contact.wall.plane.normal.y,
                            -contact.wall.plane.normal.z}) &&
                    *release_probe.result->hit == contact.wall.hit &&
                    same_source_wall(*release_probe.result->collision_plane,
                        contact.wall.plane) &&
                    release_probe.result->fraction * release_probe_distance >
                        physical_release_distance;
            }
            if (release_phase && selected_contact_seen &&
                (physical_release || !selected_contact)) {
                explicit_release_observed = true;
            }
        }

        if (physical_release) {
            selected_contact_active = false;
        } else {
            if (selected_contact && !selected_contact_active &&
                !checked_add(summary.selected_wall_contact_epochs, 1U)) {
                return failed("contact_epoch_counter_overflow", sequence,
                    *current);
            }
            selected_contact_active = selected_contact;
        }

        if (scenario == Scenario::wall_contact_stress && selected_contact &&
            explicit_release_observed && !release_phase) {
            if (!checked_add(summary.release_recontact_epochs, 1U)) {
                return failed("recontact_counter_overflow", sequence,
                    *current);
            }
            explicit_release_observed = false;
        }
        if (selected_contact) selected_contact_seen = true;

        if (scenario == Scenario::wall_glance_stress && selected_contact &&
            !tangent_alignment_command && spec.side != 0.0F) {
            const auto successor_origin = result.state->origin();
            const auto delta_x = static_cast<double>(successor_origin.x) -
                prior_origin.x;
            const auto delta_y = static_cast<double>(successor_origin.y) -
                prior_origin.y;
            const auto tangent_progress =
                -static_cast<double>(contact.wall.plane.normal.y) * delta_x +
                static_cast<double>(contact.wall.plane.normal.x) * delta_y;
            constexpr double measurable_tangent_progress = 1.0e-5;
            if (tangent_progress > measurable_tangent_progress &&
                !checked_add(summary.positive_tangent_contact_commands, 1U)) {
                return failed("tangent_counter_overflow", sequence, *current);
            }
            if (tangent_progress < -measurable_tangent_progress &&
                !checked_add(summary.negative_tangent_contact_commands, 1U)) {
                return failed("tangent_counter_overflow", sequence, *current);
            }
        }

        if (result.statistics.jump_count != 0U) actual_jump_observed = true;
        if (scenario == Scenario::jump_wall_stress && selected_contact &&
            actual_jump_observed &&
            (prior_mode == movement::PlayerMovementMode::airborne ||
                result.state->mode() == movement::PlayerMovementMode::airborne) &&
            !checked_add(summary.airborne_wall_contact_touches,
                contacts.selected_wall)) {
            return failed("airborne_contact_counter_overflow", sequence,
                *current);
        }

        if (prior_hull == movement::PlayerMovementHull::standing &&
            result.state->hull() == movement::PlayerMovementHull::ducked) {
            actual_duck_enter_observed = true;
        }
        if (actual_duck_enter_observed &&
            prior_hull == movement::PlayerMovementHull::ducked &&
            result.state->hull() == movement::PlayerMovementHull::standing) {
            actual_duck_exit_observed = true;
        }
        if (scenario == Scenario::duck_wall_stress && selected_contact &&
            actual_duck_enter_observed &&
            result.state->hull() == movement::PlayerMovementHull::ducked &&
            !checked_add(summary.ducked_wall_contact_touches,
                contacts.duck_wall)) {
            return failed("ducked_contact_counter_overflow", sequence,
                *current);
        }
        if (scenario == Scenario::duck_wall_stress && selected_contact &&
            actual_duck_exit_observed &&
            result.state->hull() == movement::PlayerMovementHull::standing &&
            !checked_add(summary.restored_standing_wall_contact_touches,
                contacts.standing_wall)) {
            return failed("standing_contact_counter_overflow", sequence,
                *current);
        }
        if (!checked_add(summary.nonpenetrating_checks, 1U))
            return failed("position_counter_overflow", sequence, *current);
        summary.maximum_command_touches = std::max(summary.maximum_command_touches,
            result.touches.size());
        if (!observe_scratch(summary, scratch))
            return failed("scratch_bound_exceeded", sequence, *current);
        hash_result(summary.route_signature, spec, result);
        current.emplace(std::move(*result.state));
    }
    if (summary.selected_wall_contact_touches == 0U)
        return failed("selected_wall_not_contacted",
            current->source_command_sequence(), *current);
    if (scenario == Scenario::corner_contact_stress &&
        summary.corner_wall_contact_touches == 0U) {
        return failed("selected_corner_wall_not_contacted",
            current->source_command_sequence(), *current);
    }
    if (scenario == Scenario::wall_contact_stress &&
        (summary.selected_wall_contact_epochs < 2U ||
            summary.release_recontact_epochs == 0U)) {
        print_stress_phase_failure_metrics(
            summary, contact, normal_alignment_commands);
        return failed("release_recontact_not_observed",
            current->source_command_sequence(), *current);
    }
    if (scenario == Scenario::wall_glance_stress &&
        (summary.positive_tangent_contact_commands == 0U ||
            summary.negative_tangent_contact_commands == 0U)) {
        print_stress_phase_failure_metrics(
            summary, contact, normal_alignment_commands);
        return failed("bidirectional_tangent_contact_not_observed",
            current->source_command_sequence(), *current);
    }
    if (scenario == Scenario::jump_wall_stress &&
        (summary.statistics.jump_count == 0U ||
            summary.airborne_wall_contact_touches == 0U)) {
        print_stress_phase_failure_metrics(
            summary, contact, normal_alignment_commands);
        return failed("airborne_wall_contact_not_observed",
            current->source_command_sequence(), *current);
    }
    if (scenario == Scenario::duck_wall_stress &&
        (summary.statistics.duck_enter_count == 0U ||
            summary.statistics.duck_exit_count == 0U ||
            summary.ducked_wall_contact_touches == 0U ||
            summary.restored_standing_wall_contact_touches == 0U)) {
        print_stress_phase_failure_metrics(
            summary, contact, normal_alignment_commands);
        return failed("duck_wall_transition_contact_not_observed",
            current->source_command_sequence(), *current);
    }
    summary.final_signature = movement::local_player_movement_state_signature(*current);
    summary.diagnostic_final_records = scratch.diagnostics.size();
    summary.diagnostic_final_overwrites = scratch.diagnostics.overwrite_count();
    summary.grounded = current->ground_state().grounded();
    summary.hull = current->hull(); summary.mode = current->mode();
    return {summary, {}, 0U, 0U};
}

[[nodiscard]] RouteResult run_legacy(movement::LocalPlayerMovementState state,
    const Scenario scenario, const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& source)
{
    std::optional<movement::LocalPlayerMovementState> current{std::move(state)};
    RouteSummary summary; kernel::GoldSrcLocalMovementScratch scratch;
    if (scratch.diagnostics.capacity() !=
        kernel::kPlayerMovementDiagnosticCapacity)
        return failed("diagnostic_capacity_mismatch", 0U, *current);
    collision::CollisionQueryScratch position_scratch;
    for (const auto& span : legacy_script(scenario)) {
        for (std::size_t index = 0U; index < span.count; ++index) {
            const auto sequence = current->source_command_sequence() + 1U;
            const auto command = make_command(sequence, span);
            if (!command) return failed("command_creation_failed", sequence, *current);
            auto result = kernel::GoldSrcLocalMovementKernel::simulate(*current,
                *command, environment, source, scratch);
            if (!result || !result.state) return failed(result.error ?
                kernel::to_string(result.error->code) :
                std::string_view{"movement_simulation_failed"}, sequence, *current);
            if (!aggregate(summary.statistics, result.statistics))
                return failed("statistics_overflow", sequence, *current);
            if (!observe_scratch(summary, scratch))
                return failed("scratch_bound_exceeded", sequence, *current);
            hash_result(summary.route_signature, span, result);
            current.emplace(std::move(*result.state));
        }
    }
    const auto position = source.test_position(current->origin(), current->hull(),
        position_scratch);
    if (!position || !position.result || position.result->status !=
        kernel::LocalMovementPositionStatus::free)
        return failed("final_position_invalid", current->source_command_sequence(), *current);
    summary.nonpenetrating_checks = 1U;
    summary.final_signature = movement::local_player_movement_state_signature(*current);
    summary.diagnostic_final_records = scratch.diagnostics.size();
    summary.diagnostic_final_overwrites = scratch.diagnostics.overwrite_count();
    summary.grounded = current->ground_state().grounded();
    summary.hull = current->hull(); summary.mode = current->mode();
    return {summary, {}, 0U, 0U};
}

[[nodiscard]] bool same_summary(const RouteSummary& a,
    const RouteSummary& b) noexcept
{
    return a.final_signature == b.final_signature &&
        a.route_signature == b.route_signature &&
        a.contact_touches == b.contact_touches &&
        a.selected_wall_contact_touches == b.selected_wall_contact_touches &&
        a.corner_wall_contact_touches == b.corner_wall_contact_touches &&
        a.selected_wall_contact_epochs == b.selected_wall_contact_epochs &&
        a.release_recontact_epochs == b.release_recontact_epochs &&
        a.positive_tangent_contact_commands ==
            b.positive_tangent_contact_commands &&
        a.negative_tangent_contact_commands ==
            b.negative_tangent_contact_commands &&
        a.airborne_wall_contact_touches ==
            b.airborne_wall_contact_touches &&
        a.ducked_wall_contact_touches == b.ducked_wall_contact_touches &&
        a.restored_standing_wall_contact_touches ==
            b.restored_standing_wall_contact_touches &&
        a.first_selected_contact_ordinal ==
            b.first_selected_contact_ordinal &&
        a.last_selected_contact_ordinal == b.last_selected_contact_ordinal &&
        a.nonpenetrating_checks == b.nonpenetrating_checks &&
        a.maximum_command_touches == b.maximum_command_touches &&
        a.scratch_retained_bytes == b.scratch_retained_bytes &&
        a.scratch_primary_retained_bytes == b.scratch_primary_retained_bytes &&
        a.scratch_direct_candidate_retained_bytes ==
            b.scratch_direct_candidate_retained_bytes &&
        a.scratch_step_candidate_retained_bytes ==
            b.scratch_step_candidate_retained_bytes &&
        a.diagnostic_maximum_records == b.diagnostic_maximum_records &&
        a.diagnostic_final_records == b.diagnostic_final_records &&
        a.diagnostic_maximum_overwrites == b.diagnostic_maximum_overwrites &&
        a.diagnostic_final_overwrites == b.diagnostic_final_overwrites &&
        a.stress_commands == b.stress_commands &&
        a.bootstrap_commands == b.bootstrap_commands &&
        a.grounded == b.grounded && a.hull == b.hull && a.mode == b.mode &&
        same_statistics(a.statistics, b.statistics);
}

[[nodiscard]] std::optional<std::string> signature_hash(
    const std::uint64_t signature)
{
    std::array<std::byte, 8U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index)
        bytes[index] = std::byte{static_cast<std::uint8_t>(signature >> (index * 8U))};
    const auto digest = hlclient::hash::sha256(bytes);
    return digest ? std::optional{hlclient::hash::sha256_hex(*digest)} :
        std::nullopt;
}

void print_summary(const local_player::LocalPlayerSpawnSelectionResult& spawn,
    const Scenario scenario, const RouteSummary& route,
    const std::string_view final_hash, const std::string_view route_hash,
    const std::optional<ContactSelection>& contact,
    const std::optional<std::string>& contact_hash)
{
    const auto& stats = route.statistics;
    std::cout.imbue(std::locale::classic());
    std::cout << "[movement] profile=" << movement::to_string(
        movement::GoldSrcMovementCompatibilityProfile::
            public_valve_pm_shared_dry_walk_subset_v1) << '\n'
        << "[movement] collision=world-only\n"
        << "[movement] brush-solidity=stock-evidence-pending\n"
        << "[movement] scenario=" << scenario_name(scenario) << '\n'
        << "[movement] spawn-candidates=" <<
            spawn.statistics.supported_class_candidate_count << '\n'
        << "[movement] spawn-selected=true\n"
        << "[movement] bootstrap-commands=" << route.bootstrap_commands << '\n'
        << "[movement] stress-commands=" << route.stress_commands << '\n';
    if (contact) {
        std::cout << "[movement] wall-found=true\n"
            << "[movement] corner-found=" <<
                (contact->corner_wall ? "true" : "false") << '\n'
            << "[movement] wall-direction-ordinal=" <<
                contact->wall.direction_ordinal << '\n'
            << "[movement] wall-source-plane-index=" <<
                *contact->wall.plane.source_plane_index << '\n'
            << "[movement] wall-selection-hash=" << *contact_hash << '\n';
    } else {
        std::cout << "[movement] wall-found=false\n"
            << "[movement] corner-found=false\n";
    }
    std::cout << "[movement] commands=" << stats.command_count << '\n'
        << "[movement] grounded-commands=" << stats.grounded_command_count << '\n'
        << "[movement] airborne-commands=" << stats.airborne_command_count << '\n'
        << "[movement] collision-hits=" << stats.collision_hit_count << '\n'
        << "[movement] contact-touches=" << route.contact_touches << '\n'
        << "[movement] selected-wall-contact-touches=" <<
            route.selected_wall_contact_touches << '\n'
        << "[movement] corner-wall-contact-touches=" <<
            route.corner_wall_contact_touches << '\n'
        << "[movement] selected-wall-contact-epochs=" <<
            route.selected_wall_contact_epochs << '\n'
        << "[movement] release-recontact-epochs=" <<
            route.release_recontact_epochs << '\n'
        << "[movement] positive-tangent-contact-commands=" <<
            route.positive_tangent_contact_commands << '\n'
        << "[movement] negative-tangent-contact-commands=" <<
            route.negative_tangent_contact_commands << '\n'
        << "[movement] airborne-wall-contact-touches=" <<
            route.airborne_wall_contact_touches << '\n'
        << "[movement] ducked-wall-contact-touches=" <<
            route.ducked_wall_contact_touches << '\n'
        << "[movement] restored-standing-wall-contact-touches=" <<
            route.restored_standing_wall_contact_touches << '\n'
        << "[movement] nonpenetrating-checks=" << route.nonpenetrating_checks << '\n'
        << "[movement] maximum-command-touches=" << route.maximum_command_touches << '\n'
        << "[movement] touch-limit=" << checker_touch_limit << '\n'
        << "[movement] touch-hard-limit=" <<
            kernel::kGoldSrcMovementHardMaximumTouchesPerCommand << '\n'
        << "[movement] scratch-retained-bytes=" << route.scratch_retained_bytes << '\n'
        << "[movement] scratch-primary-retained-bytes=" <<
            route.scratch_primary_retained_bytes << '\n'
        << "[movement] scratch-direct-candidate-retained-bytes=" <<
            route.scratch_direct_candidate_retained_bytes << '\n'
        << "[movement] scratch-step-candidate-retained-bytes=" <<
            route.scratch_step_candidate_retained_bytes << '\n'
        << "[movement] scratch-hard-limit-bytes=" <<
            checker_scratch_hard_limit << '\n'
        << "[movement] diagnostic-ring-capacity=" <<
            kernel::kPlayerMovementDiagnosticCapacity << '\n'
        << "[movement] diagnostic-ring-maximum-records=" <<
            route.diagnostic_maximum_records << '\n'
        << "[movement] diagnostic-ring-final-records=" <<
            route.diagnostic_final_records << '\n'
        << "[movement] diagnostic-ring-maximum-overwrites=" <<
            route.diagnostic_maximum_overwrites << '\n'
        << "[movement] diagnostic-ring-final-overwrites=" <<
            route.diagnostic_final_overwrites << '\n'
        << "[movement] step-successes=" << stats.step_success_count << '\n'
        << "[movement] jump-count=" << stats.jump_count << '\n'
        << "[movement] duck-enter-count=" << stats.duck_enter_count << '\n'
        << "[movement] duck-exit-count=" << stats.duck_exit_count << '\n'
        << "[movement] duck-transitions=" <<
            stats.duck_enter_count + stats.duck_exit_count << '\n'
        << "[movement] grounded=" << (route.grounded ? "true" : "false") << '\n'
        << "[movement] hull=" << movement::to_string(route.hull) << '\n'
        << "[movement] startsolid=" << stats.start_solid_count << '\n'
        << "[movement] allsolid=" << stats.all_solid_count << '\n'
        << "[movement] final-state-hash=" << final_hash << '\n'
        << "[movement] route-hash=" << route_hash << '\n'
        << "[movement] network-operations=0\n"
        << "[movement] writes-performed=0\n"
        << "[movement] result=success\n";
}

[[nodiscard]] int run_checker(const int count, wchar_t* arguments[])
{
    const auto options = parse_options(count, arguments);
    if (!options) { print_usage(); return 2; }
    auto virtual_map = hlclient::local_resources::LocalVirtualResourceName::create(
        *options->virtual_map);
    if (!virtual_map || !virtual_map.name) { print_failure("unsafe_virtual_map"); return 1; }
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        *options->base_directory, *options->game_directory);
    if (!roots || !roots.roots) { print_failure("invalid_local_roots"); return 1; }
    hlclient::local_resources::LocalResourceResolverLimits resolver_limits;
    resolver_limits.maximum_file_size =
        hlclient::local_resources::kHardMaximumLocalResourceFileSize;
    auto created = hlclient::local_resources::LocalResourceEnvironment::create(
        std::move(*roots.roots), resolver_limits);
    if (!created || !created.environment) {
        print_failure("local_environment_failed"); return 1;
    }
    auto local_environment = std::shared_ptr<const hlclient::local_resources::
        LocalResourceEnvironment>{std::move(created.environment)};
    auto resolved = local_environment->resolver().resolve(*virtual_map.name);
    if (!resolved || !resolved.file) { print_failure("map_resolution_failed"); return 1; }
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto source_size = resolved.file->file_size(); resolved.file->close();
    auto locator = local_environment->make_locator(root_id,
        std::move(*virtual_map.name), identity, source_size);
    if (!locator || !locator.locator) { print_failure("map_locator_failed"); return 1; }
    resolved.file.reset();
    auto source = open_map(local_environment, *locator.locator);
    if (!source) { print_failure("map_source_open_failed"); return 1; }
    auto parsed = bsp::GoldSrcBspParser::parse(source->source().bytes(), {},
        bsp::GoldSrcBspParseOptions{false});
    if (!parsed || !parsed.document) {
        print_failure(parsed.error ? bsp::to_string(parsed.error->code) :
            std::string_view{"bsp_parse_failed"}); return 1;
    }
    auto document = std::move(*parsed.document);
    auto entities = bsp::GoldSrcEntityDocumentParser::parse(document.entity_lump_bytes);
    if (!entities || !entities.document) { print_failure("entity_document_failed"); return 1; }
    auto package = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(document);
    if (!package || !package.package) { print_failure("collision_build_failed"); return 1; }
    kernel::WorldOnlyMovementCollision collision_source{package.package};
    collision::CollisionQueryScratch spawn_scratch;
    auto spawn = local_player::LocalPlayerSpawnSelector::select(*entities.document,
        collision_source, spawn_scratch);
    if (!spawn || !spawn.descriptor) {
        print_failure(spawn.error ? local_player::to_string(spawn.error->code) :
            std::string_view{"spawn_selection_failed"}); return 1;
    }
    auto environment = kernel::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline();
    if (!environment || !environment.environment) {
        print_failure("movement_environment_failed"); return 1;
    }
    movement::LocalPlayerMovementStateCreateInfo state_info;
    state_info.origin = spawn.descriptor->origin;
    state_info.view_angles = spawn.descriptor->view_angles_degrees;
    state_info.hull = movement::PlayerMovementHull::standing;
    state_info.mode = movement::PlayerMovementMode::airborne;
    state_info.view_offset = {0.0F, 0.0F,
        static_cast<float>(kernel::kGoldSrcMovementStandingViewOffset)};
    auto initial = movement::LocalPlayerMovementState::create(state_info);
    if (!initial || !initial.state) { print_failure("initial_state_failed"); return 1; }
    std::optional<movement::LocalPlayerMovementState> route_initial{*initial.state};
    std::size_t bootstrap = 0U;
    std::optional<ContactSelection> contact;
    if (is_stress(*options->scenario)) {
        auto settled = settle(*route_initial, *environment.environment,
            collision_source);
        if (!settled) { print_failure("spawn_settle_failed"); return 1; }
        route_initial.emplace(std::move(settled->first)); bootstrap = settled->second;
        contact = select_contacts(*route_initial, *environment.environment,
            collision_source,
            *options->scenario == Scenario::corner_contact_stress,
            *options->scenario == Scenario::duck_wall_stress);
        if (!contact) {
            const auto classification =
                *options->scenario == Scenario::corner_contact_stress ?
                    std::string_view{"corner_not_found"} :
                *options->scenario == Scenario::duck_wall_stress ?
                    std::string_view{"duck_wall_not_found"} :
                    std::string_view{"wall_not_found"};
            print_failure(classification); return 1;
        }
    }
    auto execute = [&]() {
        return is_stress(*options->scenario) ? run_stress(*route_initial,
            *options->scenario, *contact, bootstrap, *environment.environment,
            collision_source) : run_legacy(*route_initial, *options->scenario,
            *environment.environment, collision_source);
    };
    auto first = execute();
    if (!first.summary) {
        std::cerr << "[movement-error] classification=" << first.error << '\n'
            << "[movement-error] command=" << first.command <<
            " last-valid-state-signature=" << first.last_signature << '\n';
        if (contact) {
            std::cerr << "[movement-error] selected-wall-direction=" <<
                contact->wall.direction_ordinal <<
                " selected-wall-source-plane=" <<
                *contact->wall.plane.source_plane_index << '\n';
        }
        std::cerr
            << "[movement] result=failure\n"; return 1;
    }
    auto second = execute();
    if (!second.summary) {
        std::cerr << "[movement-error] classification=" << second.error << '\n'
            << "[movement-error] command=" << second.command <<
            " last-valid-state-signature=" << second.last_signature << '\n';
        if (contact) {
            std::cerr << "[movement-error] selected-wall-direction=" <<
                contact->wall.direction_ordinal <<
                " selected-wall-source-plane=" <<
                *contact->wall.plane.source_plane_index << '\n';
        }
        std::cerr
            << "[movement] result=failure\n"; return 1;
    }
    if (!same_summary(*first.summary, *second.summary)) {
        print_failure("deterministic_route_mismatch"); return 1;
    }
    const bool needs_ground = *options->scenario == Scenario::spawn_settle ||
        *options->scenario == Scenario::walk_forward ||
        *options->scenario == Scenario::jump ||
        *options->scenario == Scenario::deterministic_route;
    if (needs_ground && !first.summary->grounded) {
        print_failure("required_scenario_not_grounded"); return 1;
    }
    const auto final_hash = signature_hash(first.summary->final_signature);
    const auto route_hash = signature_hash(first.summary->route_signature);
    const auto contact_hash = contact ? signature_hash(contact->signature) :
        std::optional<std::string>{};
    if (!final_hash || !route_hash || (contact && !contact_hash)) {
        print_failure("state_hash_failed"); return 1;
    }
    print_summary(spawn, *options->scenario, *first.summary, *final_hash,
        *route_hash, contact, contact_hash);
    return 0;
}

} // namespace

int wmain(const int count, wchar_t* arguments[])
{
    try { return run_checker(count, arguments); }
    catch (const std::bad_alloc&) { print_failure("allocation_failed"); }
    catch (const std::exception&) { print_failure("checker_failed"); }
    catch (...) { print_failure("unknown_cpp_exception"); }
    return 1;
}
