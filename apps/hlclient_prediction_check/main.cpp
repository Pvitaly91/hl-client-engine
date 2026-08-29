#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_environment.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/hash/sha256.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_player/local_player_spawn_selector.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>
#include <hlclient/prediction/local_prediction.hpp>
#include <hlclient/prediction/prediction_history.hpp>
#include <hlclient/prediction/prediction_reconciliation.hpp>
#include <hlclient/prediction/synthetic_authoritative_player.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
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
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace collision = hlclient::collision;
namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_collision = hlclient::goldsrc::collision;
namespace kernel = hlclient::goldsrc::movement;
namespace local_player = hlclient::local_player;
namespace movement = hlclient::movement;
namespace prediction = hlclient::prediction;

constexpr std::size_t kDefaultCommandCount = 1'000U;
constexpr std::size_t kMaximumCommandCount = 100'000U;
constexpr std::size_t kDefaultAuthorityDelayCommands = 8U;
constexpr std::size_t kRadialDirectionCount = 64U;
constexpr float kWallSearchDistance = 4'096.0F;
constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

enum class CheckerScenario : std::uint8_t {
    exact_authority,
    delayed_authority,
    small_correction,
    velocity_correction,
    large_correction,
    teleport,
    stale_duplicate,
    wall_replay,
    jump_replay,
    duck_replay,
    history_backpressure,
    hard_reset,
    mixed,
    deterministic_route,
};

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::optional<std::string> game_directory;
    std::optional<std::string> virtual_map;
    std::optional<CheckerScenario> scenario;
    std::size_t authority_delay_commands{kDefaultAuthorityDelayCommands};
    std::size_t command_count{kDefaultCommandCount};
    bool authority_delay_was_set{false};
};

struct CommandSpec {
    float forward{0.0F};
    float side{0.0F};
    float yaw{0.0F};
    std::uint16_t buttons{0U};
};

struct RunStatistics {
    std::uint64_t commands{0U};
    std::uint64_t authority_updates{0U};
    std::uint64_t acknowledgements{0U};
    std::uint64_t reconciliations{0U};
    std::uint64_t exact{0U};
    std::uint64_t replays{0U};
    std::uint64_t replayed_commands{0U};
    std::size_t maximum_replay_depth{0U};
    std::uint64_t small_corrections{0U};
    std::uint64_t snaps{0U};
    std::uint64_t stale{0U};
    std::uint64_t duplicates{0U};
    std::uint64_t history_backpressure{0U};
    std::uint64_t hard_resets{0U};
    std::uint64_t reset_discarded_commands{0U};
    std::uint64_t authoritative_commands_processed{0U};
    std::size_t history_high_water{0U};
    std::uint64_t start_solid{0U};
    std::uint64_t all_solid{0U};
    std::uint64_t jump_count{0U};
    std::uint64_t duck_enter_count{0U};
    std::uint64_t duck_exit_count{0U};
    bool wall_selected{false};
    bool wall_contact_observed{false};
};

template<class Value, bool = std::is_enum_v<Value>> struct RawIntegral {
    using type = Value;
};

template<class Value> struct RawIntegral<Value, true> {
    using type = std::underlying_type_t<Value>;
};

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= kFnvPrime;
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

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] assets::AssetVector3 add(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] std::optional<std::string> narrow_ascii(
    const std::wstring_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if (character < 0x20 || character > 0x7e) {
            return std::nullopt;
        }
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] std::optional<std::size_t> parse_size(
    const std::wstring_view value) noexcept
{
    const auto narrow = narrow_ascii(value);
    if (!narrow || narrow->empty()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(
        narrow->data(), narrow->data() + narrow->size(), parsed, 10);
    if (result.ec != std::errc{} ||
        result.ptr != narrow->data() + narrow->size() ||
        parsed > static_cast<std::uint64_t>(
            (std::numeric_limits<std::size_t>::max)())) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] std::optional<CheckerScenario> parse_scenario(
    const std::string_view value) noexcept
{
    constexpr std::array pairs{
        std::pair{"exact-authority", CheckerScenario::exact_authority},
        std::pair{"delayed-authority", CheckerScenario::delayed_authority},
        std::pair{"small-correction", CheckerScenario::small_correction},
        std::pair{"velocity-correction", CheckerScenario::velocity_correction},
        std::pair{"large-correction", CheckerScenario::large_correction},
        std::pair{"teleport", CheckerScenario::teleport},
        std::pair{"stale-duplicate", CheckerScenario::stale_duplicate},
        std::pair{"wall-replay", CheckerScenario::wall_replay},
        std::pair{"jump-replay", CheckerScenario::jump_replay},
        std::pair{"duck-replay", CheckerScenario::duck_replay},
        std::pair{"history-backpressure",
            CheckerScenario::history_backpressure},
        std::pair{"hard-reset", CheckerScenario::hard_reset},
        std::pair{"mixed", CheckerScenario::mixed},
        std::pair{"deterministic-route", CheckerScenario::deterministic_route},
    };
    for (const auto& [name, scenario] : pairs) {
        if (value == name) {
            return scenario;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int count, wchar_t* arguments[])
{
    Options options;
    bool commands_was_set = false;
    for (int index = 1; index < count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (index + 1 >= count ||
            (argument != L"--basedir" && argument != L"--game" &&
                argument != L"--map" && argument != L"--scenario" &&
                argument != L"--authority-delay-commands" &&
                argument != L"--commands")) {
            return std::nullopt;
        }
        const std::wstring_view value{arguments[++index]};
        if (value.empty()) {
            return std::nullopt;
        }
        if (argument == L"--basedir") {
            if (options.base_directory) {
                return std::nullopt;
            }
            options.base_directory = std::filesystem::path{value};
            continue;
        }
        if (argument == L"--authority-delay-commands") {
            if (options.authority_delay_was_set) {
                return std::nullopt;
            }
            const auto parsed = parse_size(value);
            if (!parsed ||
                *parsed > prediction::kMaximumSyntheticAuthorityDelayCommands) {
                return std::nullopt;
            }
            options.authority_delay_commands = *parsed;
            options.authority_delay_was_set = true;
            continue;
        }
        if (argument == L"--commands") {
            if (commands_was_set) {
                return std::nullopt;
            }
            const auto parsed = parse_size(value);
            if (!parsed || *parsed == 0U || *parsed > kMaximumCommandCount) {
                return std::nullopt;
            }
            options.command_count = *parsed;
            commands_was_set = true;
            continue;
        }
        auto narrow = narrow_ascii(value);
        if (!narrow) {
            return std::nullopt;
        }
        if (argument == L"--game") {
            if (options.game_directory) {
                return std::nullopt;
            }
            options.game_directory = std::move(*narrow);
        } else if (argument == L"--map") {
            if (options.virtual_map) {
                return std::nullopt;
            }
            options.virtual_map = std::move(*narrow);
        } else {
            if (options.scenario) {
                return std::nullopt;
            }
            options.scenario = parse_scenario(*narrow);
            if (!options.scenario) {
                return std::nullopt;
            }
        }
    }
    if (!options.base_directory || !options.game_directory ||
        !options.virtual_map || !options.scenario) {
        return std::nullopt;
    }
    if (!options.authority_delay_was_set &&
        *options.scenario == CheckerScenario::exact_authority) {
        options.authority_delay_commands = 0U;
    }
    return options;
}

void print_usage()
{
    std::cerr << "Usage: hlclient_prediction_check --basedir <Half-Life root> "
        "--game <directory> --map <maps/name.bsp> --scenario "
        "<exact-authority|delayed-authority|small-correction|"
        "velocity-correction|large-correction|teleport|stale-duplicate|"
        "wall-replay|jump-replay|duck-replay|history-backpressure|"
        "hard-reset|mixed|deterministic-route> "
        "[--authority-delay-commands <0..64>] "
        "[--commands <1..100000>]\n";
}

void print_failure(const std::string_view classification)
{
    std::cerr << "[prediction-error] classification=" << classification << '\n'
              << "[prediction] result=failure\n";
}

void print_failure(const prediction::PredictionError& error)
{
    std::cerr << "[prediction-error] classification="
              << prediction::to_string(error.code) << '\n';
    if (error.command_sequence) {
        std::cerr << "[prediction-error] command-sequence="
                  << error.command_sequence->value() << '\n';
    }
    std::cerr << "[prediction-error] context=" << error.context << '\n'
              << "[prediction] result=failure\n";
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
    if (!started || !started.operation) {
        return std::nullopt;
    }
    auto& operation = *started.operation;
    constexpr auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < 1'000'000U && !source_terminal(operation.state());
         ++update) {
        operation.update(now);
    }
    if (operation.state() !=
        hlclient::local_assets::LocalAssetSourceOpenState::source_ready) {
        return std::nullopt;
    }
    return operation.take_result();
}

[[nodiscard]] prediction::SyntheticAuthoritativeScenario authority_scenario(
    const CheckerScenario scenario,
    const std::size_t delay) noexcept
{
    switch (scenario) {
    case CheckerScenario::exact_authority:
        return prediction::SyntheticAuthoritativeScenario::exact_authority;
    case CheckerScenario::delayed_authority:
        return prediction::SyntheticAuthoritativeScenario::delayed_authority;
    case CheckerScenario::small_correction:
        return prediction::SyntheticAuthoritativeScenario::
            small_position_correction;
    case CheckerScenario::velocity_correction:
        return prediction::SyntheticAuthoritativeScenario::velocity_correction;
    case CheckerScenario::large_correction:
        return prediction::SyntheticAuthoritativeScenario::
            large_position_correction;
    case CheckerScenario::teleport:
        return prediction::SyntheticAuthoritativeScenario::teleport;
    case CheckerScenario::stale_duplicate:
        return prediction::SyntheticAuthoritativeScenario::
            stale_and_duplicate_updates;
    case CheckerScenario::wall_replay:
        return prediction::SyntheticAuthoritativeScenario::wall_replay;
    case CheckerScenario::jump_replay:
        return prediction::SyntheticAuthoritativeScenario::jump_replay;
    case CheckerScenario::duck_replay:
        return prediction::SyntheticAuthoritativeScenario::duck_replay;
    case CheckerScenario::history_backpressure:
    case CheckerScenario::hard_reset:
        return delay == 0U
            ? prediction::SyntheticAuthoritativeScenario::exact_authority
            : prediction::SyntheticAuthoritativeScenario::delayed_authority;
    case CheckerScenario::mixed:
        return prediction::SyntheticAuthoritativeScenario::mixed;
    case CheckerScenario::deterministic_route:
        return delay == 0U
            ? prediction::SyntheticAuthoritativeScenario::exact_authority
            : prediction::SyntheticAuthoritativeScenario::delayed_authority;
    }
    return prediction::SyntheticAuthoritativeScenario::exact_authority;
}

[[nodiscard]] assets::AssetVector3 radial_direction(
    const std::size_t ordinal) noexcept
{
    constexpr double kToRadians =
        0.017453292519943295769236907684886;
    const auto angle = static_cast<double>(ordinal) *
        (360.0 / static_cast<double>(kRadialDirectionCount)) * kToRadians;
    return {static_cast<float>(std::cos(angle)),
        static_cast<float>(std::sin(angle)), 0.0F};
}

[[nodiscard]] bool same_wall_plane(
    const movement::PlayerMovementPlane& left,
    const movement::PlayerMovementPlane& right) noexcept
{
    if (!left.source_plane_index || !right.source_plane_index ||
        left.source_plane_index != right.source_plane_index ||
        !finite(left.normal) || !finite(right.normal) ||
        !std::isfinite(left.distance) || !std::isfinite(right.distance)) {
        return false;
    }
    const auto dot = static_cast<double>(left.normal.x) * right.normal.x +
        static_cast<double>(left.normal.y) * right.normal.y +
        static_cast<double>(left.normal.z) * right.normal.z;
    return dot >= 0.99999 && std::abs(left.distance - right.distance) <= 1.0e-4;
}

[[nodiscard]] bool usable_wall_trace(
    const kernel::LocalMovementTrace& trace,
    const assets::AssetVector3& direction) noexcept
{
    if (trace.start_solid || trace.all_solid || trace.fraction >= 1.0 ||
        !trace.hit || !trace.collision_plane ||
        !trace.collision_plane->source_plane_index) {
        return false;
    }
    const auto& normal = trace.collision_plane->normal;
    const auto length_squared =
        static_cast<double>(normal.x) * normal.x +
        static_cast<double>(normal.y) * normal.y +
        static_cast<double>(normal.z) * normal.z;
    const auto directional =
        static_cast<double>(direction.x) * normal.x +
        static_cast<double>(direction.y) * normal.y;
    return finite(normal) && std::isfinite(trace.collision_plane->distance) &&
        std::abs(length_squared - 1.0) <= 1.0e-4 &&
        std::abs(normal.z) <= 0.1F && directional < -1.0e-4;
}

[[nodiscard]] std::optional<float> discover_wall_yaw(
    const movement::LocalPlayerMovementState& state,
    const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& source)
{
    collision::CollisionQueryScratch scratch;
    std::optional<std::size_t> selected_ordinal;
    double selected_distance = (std::numeric_limits<double>::max)();
    for (std::size_t ordinal = 0U; ordinal < kRadialDirectionCount;
         ++ordinal) {
        const auto direction = radial_direction(ordinal);
        const auto start = state.origin();
        const assets::AssetVector3 end{
            start.x + direction.x * kWallSearchDistance,
            start.y + direction.y * kWallSearchDistance,
            start.z};
        const auto trace = source.trace_hull(start, end,
            movement::PlayerMovementHull::standing, scratch);
        if (!trace || !trace.result ||
            !usable_wall_trace(*trace.result, direction)) {
            continue;
        }
        auto high_start = start;
        high_start.z += environment.step_size() + 1.0F;
        const auto high_position = source.test_position(high_start,
            movement::PlayerMovementHull::standing, scratch);
        if (!high_position || !high_position.result ||
            high_position.result->status !=
                kernel::LocalMovementPositionStatus::free) {
            continue;
        }
        const assets::AssetVector3 high_end{
            high_start.x + direction.x * kWallSearchDistance,
            high_start.y + direction.y * kWallSearchDistance,
            high_start.z};
        const auto high_trace = source.trace_hull(high_start, high_end,
            movement::PlayerMovementHull::standing, scratch);
        if (!high_trace || !high_trace.result ||
            !usable_wall_trace(*high_trace.result, direction) ||
            *trace.result->hit != *high_trace.result->hit ||
            !same_wall_plane(*trace.result->collision_plane,
                *high_trace.result->collision_plane)) {
            continue;
        }
        const auto distance = trace.result->fraction * kWallSearchDistance;
        if (!selected_ordinal || distance < selected_distance ||
            (distance == selected_distance && ordinal < *selected_ordinal)) {
            selected_ordinal = ordinal;
            selected_distance = distance;
        }
    }
    if (!selected_ordinal) {
        return std::nullopt;
    }
    constexpr double kToDegrees =
        57.295779513082320876798154814105;
    const auto direction = radial_direction(*selected_ordinal);
    return static_cast<float>(std::atan2(
        static_cast<double>(direction.y),
        static_cast<double>(direction.x)) * kToDegrees);
}

[[nodiscard]] CommandSpec generic_route_command(
    const std::size_t ordinal,
    const bool grounded) noexcept
{
    CommandSpec command;
    if (!grounded || ordinal <= 48U) {
        return command;
    }
    const auto phase = (ordinal - 49U) % 300U;
    if (phase < 80U) {
        command.forward = 220.0F;
    } else if (phase < 120U) {
        command.forward = 180.0F;
        command.yaw = 45.0F;
    } else if (phase < 160U) {
        command.side = 180.0F;
        command.yaw = 45.0F;
    } else if (phase == 160U) {
        command.yaw = 45.0F;
        command.buttons = goldsrc::kSyntheticGoldSrcButtonJump;
    } else if (phase < 220U) {
        command.yaw = 45.0F;
    } else if (phase < 250U) {
        command.forward = 100.0F;
        command.yaw = 45.0F;
        command.buttons = goldsrc::kSyntheticGoldSrcButtonDuck;
    } else if (phase < 280U) {
        command.forward = 100.0F;
        command.yaw = 45.0F;
    }
    return command;
}

[[nodiscard]] CommandSpec command_for_scenario(
    const CheckerScenario scenario,
    const std::size_t ordinal,
    const movement::LocalPlayerMovementState& state,
    const std::optional<float> wall_yaw) noexcept
{
    if (scenario == CheckerScenario::wall_replay) {
        if (!state.ground_state().grounded() || !wall_yaw) {
            return {};
        }
        CommandSpec command;
        command.forward = 320.0F;
        command.yaw = *wall_yaw;
        const auto phase = ordinal % 240U;
        if (phase >= 80U && phase < 120U) {
            command.side = 80.0F;
        } else if (phase >= 120U && phase < 160U) {
            command.side = -80.0F;
        } else if (phase == 180U) {
            command.buttons = goldsrc::kSyntheticGoldSrcButtonJump;
        } else if (phase >= 200U && phase < 220U) {
            // Recover after the jump before the next wall-contact cycle.
            // Duck transitions have their own collision-valid replay route.
            command.forward = 0.0F;
        }
        return command;
    }
    if (scenario == CheckerScenario::jump_replay) {
        if (!state.ground_state().grounded()) {
            return {};
        }
        CommandSpec command;
        command.forward = 100.0F;
        command.buttons = ordinal % 100U == 0U
            ? goldsrc::kSyntheticGoldSrcButtonJump
            : 0U;
        return command;
    }
    if (scenario == CheckerScenario::duck_replay) {
        if (!state.ground_state().grounded()) {
            return {};
        }
        CommandSpec command;
        command.forward = 80.0F;
        command.buttons = (ordinal / 50U) % 2U == 0U
            ? goldsrc::kSyntheticGoldSrcButtonDuck
            : 0U;
        return command;
    }
    return generic_route_command(ordinal, state.ground_state().grounded());
}

[[nodiscard]] std::optional<goldsrc::GoldSrcUserCmdState> make_command(
    const std::uint32_t sequence,
    const CommandSpec& spec)
{
    const auto valid_sequence = goldsrc::GoldSrcUserCmdSequence::create(sequence);
    if (!valid_sequence) {
        return std::nullopt;
    }
    auto info = goldsrc::goldsrc_usercmd_default_create_info(
        *valid_sequence, static_cast<std::int64_t>(sequence) * 10'000'000);
    info.msec = 10U;
    info.sample_duration_nanoseconds = 10'000'000U;
    info.view_angles = {0.0F, spec.yaw, 0.0F};
    info.forward_move = spec.forward;
    info.side_move = spec.side;
    info.buttons = spec.buttons;
    auto made = goldsrc::GoldSrcUserCmdState::create(info);
    if (!made || !made.state) {
        return std::nullopt;
    }
    return std::move(*made.state);
}

[[nodiscard]] bool free_position(
    const kernel::ILocalMovementCollision& source,
    const assets::AssetVector3& origin,
    const movement::PlayerMovementHull hull,
    collision::CollisionQueryScratch& scratch)
{
    const auto tested = source.test_position(origin, hull, scratch);
    return tested && tested.result && tested.result->status ==
        kernel::LocalMovementPositionStatus::free;
}

[[nodiscard]] std::optional<assets::AssetVector3> choose_small_delta(
    const movement::LocalPlayerMovementState& initial,
    const kernel::ILocalMovementCollision& source,
    collision::CollisionQueryScratch& scratch)
{
    constexpr std::array candidates{
        assets::AssetVector3{0.5F, 0.0F, 0.0F},
        assets::AssetVector3{-0.5F, 0.0F, 0.0F},
        assets::AssetVector3{0.0F, 0.5F, 0.0F},
        assets::AssetVector3{0.0F, -0.5F, 0.0F},
        assets::AssetVector3{0.0F, 0.0F, 0.5F},
    };
    for (const auto& delta : candidates) {
        if (free_position(source, add(initial.origin(), delta),
                initial.hull(), scratch)) {
            return delta;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<assets::AssetVector3> choose_large_delta(
    const movement::LocalPlayerMovementState& initial,
    const assets::AssetVector3& base_offset,
    const kernel::ILocalMovementCollision& source,
    collision::CollisionQueryScratch& scratch)
{
    constexpr std::array radii{32.0F, 24.0F, 48.0F, 64.0F};
    const auto base = add(initial.origin(), base_offset);
    for (const auto radius : radii) {
        for (std::size_t ordinal = 0U; ordinal < kRadialDirectionCount;
             ++ordinal) {
            const auto direction = radial_direction(ordinal);
            const assets::AssetVector3 delta{
                direction.x * radius, direction.y * radius, 0.0F};
            if (free_position(source, add(base, delta), initial.hull(), scratch)) {
                return delta;
            }
        }
        const std::array vertical{
            assets::AssetVector3{0.0F, 0.0F, radius},
            assets::AssetVector3{0.0F, 0.0F, -radius},
        };
        for (const auto& delta : vertical) {
            if (free_position(source, add(base, delta), initial.hull(), scratch)) {
                return delta;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> signature_hash(
    const std::uint64_t signature)
{
    std::array<std::byte, 8U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = std::byte{static_cast<std::uint8_t>(
            signature >> (index * 8U))};
    }
    const auto digest = hlclient::hash::sha256(bytes);
    return digest ? std::optional{hlclient::hash::sha256_hex(*digest)}
                  : std::nullopt;
}

[[nodiscard]] bool observe_local_simulation(
    RunStatistics& aggregate,
    const kernel::LocalMovementSimulationResult& simulated) noexcept
{
    const auto& statistics = simulated.statistics;
    if (statistics.start_solid_count != 0U ||
        statistics.all_solid_count != 0U ||
        aggregate.start_solid > UINT64_MAX - statistics.start_solid_count ||
        aggregate.all_solid > UINT64_MAX - statistics.all_solid_count ||
        aggregate.jump_count > UINT64_MAX - statistics.jump_count ||
        aggregate.duck_enter_count > UINT64_MAX - statistics.duck_enter_count ||
        aggregate.duck_exit_count > UINT64_MAX - statistics.duck_exit_count) {
        return false;
    }
    aggregate.start_solid += statistics.start_solid_count;
    aggregate.all_solid += statistics.all_solid_count;
    aggregate.jump_count += statistics.jump_count;
    aggregate.duck_enter_count += statistics.duck_enter_count;
    aggregate.duck_exit_count += statistics.duck_exit_count;
    for (const auto& touch : simulated.touches) {
        if (finite(touch.plane.normal) &&
            std::abs(touch.plane.normal.z) <= 0.1F) {
            aggregate.wall_contact_observed = true;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::string_view> apply_authority_update(
    const prediction::AuthoritativePlayerState& authoritative,
    std::shared_ptr<const prediction::LocalPredictionHistoryState>& history,
    const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& source,
    kernel::GoldSrcLocalMovementScratch& replay_scratch,
    collision::CollisionQueryScratch& validation_scratch,
    const kernel::GoldSrcLocalMovementConfig& movement_config,
    const prediction::PredictionReconciliationConfig& reconciliation_config,
    RunStatistics& statistics,
    std::uint64_t& route_hash)
{
    if (statistics.authority_updates == UINT64_MAX) {
        return "statistics_overflow";
    }
    ++statistics.authority_updates;
    hash_integral(route_hash,
        authoritative.update_identity().update_ordinal());
    hash_integral(route_hash,
        authoritative.update_identity().discontinuity());
    hash_integral(route_hash, authoritative.state_signature());
    const auto acknowledged = authoritative.acknowledgement().sequence();
    hash_integral(route_hash, acknowledged.has_value());
    if (acknowledged) {
        hash_integral(route_hash, acknowledged->value());
    }

    auto reconciled = prediction::LocalPlayerPredictionReconciler::reconcile(
        *history, authoritative, environment, source, replay_scratch,
        movement_config, reconciliation_config);
    if (!reconciled || !reconciled.history ||
        !reconciled.corrected_current_state) {
        return reconciled.error
            ? prediction::to_string(reconciled.error->code)
            : std::string_view{"prediction_reconciliation_failed"};
    }
    if (reconciled.stale_ignored) {
        if (statistics.stale == UINT64_MAX) {
            return "statistics_overflow";
        }
        ++statistics.stale;
    } else if (reconciled.duplicate_ignored) {
        if (statistics.duplicates == UINT64_MAX) {
            return "statistics_overflow";
        }
        ++statistics.duplicates;
    } else if (reconciled.history_changed) {
        if (statistics.reconciliations == UINT64_MAX ||
            (acknowledged && statistics.acknowledgements == UINT64_MAX)) {
            return "statistics_overflow";
        }
        if (acknowledged) {
            ++statistics.acknowledgements;
        }
        ++statistics.reconciliations;
        switch (reconciled.correction_class) {
        case prediction::PredictionCorrectionClass::exact:
            ++statistics.exact;
            break;
        case prediction::PredictionCorrectionClass::small_visual_correction:
            ++statistics.small_corrections;
            break;
        case prediction::PredictionCorrectionClass::large_snap:
        case prediction::PredictionCorrectionClass::teleport_snap:
            ++statistics.snaps;
            break;
        case prediction::PredictionCorrectionClass::hard_reset:
            if (statistics.snaps == UINT64_MAX ||
                statistics.hard_resets == UINT64_MAX) {
                return "statistics_overflow";
            }
            ++statistics.snaps;
            ++statistics.hard_resets;
            break;
        case prediction::PredictionCorrectionClass::
            replay_without_visual_offset:
            break;
        }
        const auto replayed = reconciled.replay_statistics.replayed_command_count;
        if (replayed != 0U) {
            if (statistics.replays == UINT64_MAX ||
                statistics.replayed_commands > UINT64_MAX - replayed) {
                return "statistics_overflow";
            }
            ++statistics.replays;
            statistics.replayed_commands += replayed;
            statistics.maximum_replay_depth = (std::max)(
                statistics.maximum_replay_depth, replayed);
        }
    }
    const auto tested = source.test_position(
        reconciled.corrected_current_state->origin(),
        reconciled.corrected_current_state->hull(), validation_scratch,
        movement_config.collision_query);
    if (!tested || !tested.result) {
        return "collision_query_failed";
    }
    if (tested.result->status != kernel::LocalMovementPositionStatus::free) {
        return "authoritative_state_blocking";
    }
    history = std::move(reconciled.history);
    statistics.history_high_water = (std::max)(
        statistics.history_high_water,
        history->statistics().high_water_mark);
    hash_integral(route_hash, reconciled.correction_class);
    hash_integral(route_hash,
        reconciled.replay_statistics.replayed_command_count);
    hash_integral(route_hash,
        prediction::local_prediction_history_signature(*history));
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> drain_authority(
    prediction::SyntheticAuthoritativePlayerStateSource& authority,
    std::shared_ptr<const prediction::LocalPredictionHistoryState>& history,
    const kernel::GoldSrcMovementEnvironment& environment,
    const kernel::ILocalMovementCollision& source,
    kernel::GoldSrcLocalMovementScratch& replay_scratch,
    collision::CollisionQueryScratch& validation_scratch,
    const kernel::GoldSrcLocalMovementConfig& movement_config,
    const prediction::PredictionReconciliationConfig& reconciliation_config,
    RunStatistics& statistics,
    std::uint64_t& route_hash)
{
    for (;;) {
        auto polled = authority.poll_next();
        if (polled.error) {
            return prediction::to_string(polled.error->code);
        }
        if (!polled.state) {
            return std::nullopt;
        }
        if (const auto failure = apply_authority_update(*polled.state,
                history, environment, source, replay_scratch,
                validation_scratch, movement_config, reconciliation_config,
                statistics, route_hash)) {
            return failure;
        }
    }
}

void print_summary(
    const RunStatistics& statistics,
    const std::string_view final_state_hash,
    const std::string_view history_replay_hash)
{
    std::cout << "[prediction] profile=" << prediction::to_string(
            prediction::PredictionCompatibilityProfile::
                synthetic_authoritative_reconciliation_v1) << '\n'
        << "[prediction] commands=" << statistics.commands << '\n'
        << "[prediction] authority-updates=" <<
            statistics.authority_updates << '\n'
        << "[prediction] acknowledgements=" <<
            statistics.acknowledgements << '\n'
        << "[prediction] reconciliations=" <<
            statistics.reconciliations << '\n'
        << "[prediction] exact=" << statistics.exact << '\n'
        << "[prediction] replays=" << statistics.replays << '\n'
        << "[prediction] replayed-commands=" <<
            statistics.replayed_commands << '\n'
        << "[prediction] maximum-replay-depth=" <<
            statistics.maximum_replay_depth << '\n'
        << "[prediction] small-corrections=" <<
            statistics.small_corrections << '\n'
        << "[prediction] snaps=" << statistics.snaps << '\n'
        << "[prediction] stale=" << statistics.stale << '\n'
        << "[prediction] duplicates=" << statistics.duplicates << '\n'
        << "[prediction] history-backpressure=" <<
            statistics.history_backpressure << '\n'
        << "[prediction] hard-resets=" << statistics.hard_resets << '\n'
        << "[prediction] reset-discarded-commands=" <<
            statistics.reset_discarded_commands << '\n'
        << "[prediction] history-high-water=" <<
            statistics.history_high_water << '\n'
        << "[prediction] final-state-hash=" << final_state_hash << '\n'
        << "[prediction] history-replay-hash=" << history_replay_hash << '\n'
        << "[prediction] history-overflow=0\n"
        << "[prediction] startsolid=" << statistics.start_solid << '\n'
        << "[prediction] allsolid=" << statistics.all_solid << '\n'
        << "[prediction] jumps=" << statistics.jump_count << '\n'
        << "[prediction] duck-enters=" << statistics.duck_enter_count << '\n'
        << "[prediction] duck-exits=" << statistics.duck_exit_count << '\n'
        << "[prediction] network-operations=0\n"
        << "[prediction] writes-performed=0\n"
        << "[prediction] result=success\n";
}

[[nodiscard]] int run_checker(const int count, wchar_t* arguments[])
{
    std::cout.imbue(std::locale::classic());
    std::cerr.imbue(std::locale::classic());
    const auto options = parse_options(count, arguments);
    if (!options) {
        print_usage();
        return 2;
    }
    const auto synthetic_scenario = authority_scenario(
        *options->scenario, options->authority_delay_commands);
    if ((synthetic_scenario ==
            prediction::SyntheticAuthoritativeScenario::exact_authority &&
            options->authority_delay_commands != 0U) ||
        (synthetic_scenario ==
            prediction::SyntheticAuthoritativeScenario::delayed_authority &&
            options->authority_delay_commands == 0U) ||
        (*options->scenario == CheckerScenario::history_backpressure &&
            (options->authority_delay_commands == 0U ||
                options->command_count <=
                    options->authority_delay_commands)) ||
        (*options->scenario == CheckerScenario::hard_reset &&
            options->command_count <=
                options->authority_delay_commands + 1U)) {
        print_failure("invalid_configuration");
        return 1;
    }

    auto virtual_map = hlclient::local_resources::LocalVirtualResourceName::create(
        *options->virtual_map);
    if (!virtual_map || !virtual_map.name) {
        print_failure("unsafe_virtual_map");
        return 1;
    }
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        *options->base_directory, *options->game_directory);
    if (!roots || !roots.roots) {
        print_failure("invalid_local_roots");
        return 1;
    }
    hlclient::local_resources::LocalResourceResolverLimits resolver_limits;
    resolver_limits.maximum_file_size =
        hlclient::local_resources::kHardMaximumLocalResourceFileSize;
    auto created_environment =
        hlclient::local_resources::LocalResourceEnvironment::create(
            std::move(*roots.roots), resolver_limits);
    if (!created_environment || !created_environment.environment) {
        print_failure("local_environment_failed");
        return 1;
    }
    auto local_environment = std::shared_ptr<const hlclient::local_resources::
        LocalResourceEnvironment>{std::move(created_environment.environment)};
    auto resolved = local_environment->resolver().resolve(*virtual_map.name);
    if (!resolved || !resolved.file) {
        print_failure("map_resolution_failed");
        return 1;
    }
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto source_size = resolved.file->file_size();
    resolved.file->close();
    auto locator = local_environment->make_locator(root_id,
        std::move(*virtual_map.name), identity, source_size);
    if (!locator || !locator.locator) {
        print_failure("map_locator_failed");
        return 1;
    }
    resolved.file.reset();
    auto source = open_map(local_environment, *locator.locator);
    if (!source) {
        print_failure("map_source_open_failed");
        return 1;
    }
    auto parsed = bsp::GoldSrcBspParser::parse(source->source().bytes(), {},
        bsp::GoldSrcBspParseOptions{false});
    if (!parsed || !parsed.document) {
        print_failure(parsed.error ? bsp::to_string(parsed.error->code)
                                   : std::string_view{"bsp_parse_failed"});
        return 1;
    }
    auto document = std::move(*parsed.document);
    auto entities = bsp::GoldSrcEntityDocumentParser::parse(
        document.entity_lump_bytes);
    if (!entities || !entities.document) {
        print_failure("entity_document_failed");
        return 1;
    }
    auto package = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        document);
    if (!package || !package.package) {
        print_failure("collision_build_failed");
        return 1;
    }
    kernel::WorldOnlyMovementCollision collision_source{package.package};
    collision::CollisionQueryScratch spawn_scratch;
    auto spawn = local_player::LocalPlayerSpawnSelector::select(
        *entities.document, collision_source, spawn_scratch);
    if (!spawn || !spawn.descriptor) {
        print_failure(spawn.error
                ? local_player::to_string(spawn.error->code)
                : std::string_view{"spawn_selection_failed"});
        return 1;
    }
    auto movement_environment = kernel::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline();
    if (!movement_environment || !movement_environment.environment) {
        print_failure("movement_environment_failed");
        return 1;
    }
    movement::LocalPlayerMovementStateCreateInfo initial_info;
    initial_info.origin = spawn.descriptor->origin;
    initial_info.view_angles = spawn.descriptor->view_angles_degrees;
    initial_info.hull = movement::PlayerMovementHull::standing;
    initial_info.mode = movement::PlayerMovementMode::airborne;
    initial_info.view_offset = {0.0F, 0.0F,
        static_cast<float>(kernel::kGoldSrcMovementStandingViewOffset)};
    auto initial_result = movement::LocalPlayerMovementState::create(initial_info);
    if (!initial_result || !initial_result.state) {
        print_failure("initial_state_failed");
        return 1;
    }
    const auto initial_state = *initial_result.state;
    kernel::GoldSrcLocalMovementConfig movement_config;
    auto session_result = prediction::create_prediction_session_identity(
        1U, 1U, collision_source, *movement_environment.environment,
        movement_config, initial_state);
    if (!session_result || !session_result.session) {
        print_failure(session_result.error
                ? prediction::to_string(session_result.error->code)
                : std::string_view{"invalid_session_identity"});
        return 1;
    }

    prediction::LocalPredictionHistoryLimits history_limits;
    if (*options->scenario == CheckerScenario::history_backpressure) {
        history_limits.maximum_entries = options->authority_delay_commands;
        history_limits.maximum_authority_delay_commands =
            options->authority_delay_commands;
    } else {
        history_limits.maximum_entries =
            prediction::kHardMaximumPredictionHistoryEntries;
        history_limits.maximum_authority_delay_commands =
            prediction::kMaximumSyntheticAuthorityDelayCommands + 1U;
    }
    history_limits.maximum_replay_commands =
        prediction::kHardMaximumPredictionReplayCommands;
    auto history_result = prediction::LocalPredictionHistoryState::
        create_initial(initial_state, *session_result.session, history_limits);
    if (!history_result || !history_result.history) {
        print_failure(history_result.error
                ? prediction::to_string(history_result.error->code)
                : std::string_view{"prediction_history_failed"});
        return 1;
    }
    auto history = std::move(history_result.history);

    prediction::SyntheticAuthoritativePlayerConfig authority_config;
    authority_config.session = *session_result.session;
    authority_config.scenario = synthetic_scenario;
    authority_config.command_delay = options->authority_delay_commands;
    authority_config.maximum_pending_updates =
        prediction::kMaximumSyntheticAuthorityPendingUpdates;
    switch (*options->scenario) {
    case CheckerScenario::wall_replay:
        authority_config.correction_command_sequence = 72U;
        break;
    case CheckerScenario::jump_replay:
        authority_config.correction_command_sequence = 92U;
        break;
    case CheckerScenario::duck_replay:
        authority_config.correction_command_sequence = 42U;
        break;
    default:
        authority_config.correction_command_sequence = 1U;
        break;
    }
    collision::CollisionQueryScratch correction_scratch;
    const bool needs_small = synthetic_scenario ==
            prediction::SyntheticAuthoritativeScenario::
                small_position_correction ||
        synthetic_scenario == prediction::SyntheticAuthoritativeScenario::mixed;
    const bool needs_large = synthetic_scenario ==
            prediction::SyntheticAuthoritativeScenario::
                large_position_correction ||
        synthetic_scenario == prediction::SyntheticAuthoritativeScenario::mixed;
    std::optional<movement::LocalPlayerMovementState> correction_base{
        initial_state};
    if (needs_small || needs_large) {
        const auto planning_command = make_command(1U, {});
        kernel::GoldSrcLocalMovementScratch planning_scratch;
        if (!planning_command) {
            print_failure("usercmd_creation_failed");
            return 1;
        }
        auto planned = kernel::GoldSrcLocalMovementKernel::simulate(
            initial_state, *planning_command, *movement_environment.environment,
            collision_source, planning_scratch, movement_config);
        if (!planned || !planned.state) {
            print_failure(planned.error
                    ? kernel::to_string(planned.error->code)
                    : std::string_view{"correction_planning_failed"});
            return 1;
        }
        correction_base.emplace(std::move(*planned.state));
    }
    if (needs_small) {
        const auto delta = choose_small_delta(
            *correction_base, collision_source, correction_scratch);
        if (!delta) {
            print_failure("correction_destination_unavailable");
            return 1;
        }
        authority_config.small_position_delta = *delta;
    }
    if (needs_large) {
        const auto base_offset = needs_small
            ? authority_config.small_position_delta
            : assets::AssetVector3{};
        const auto delta = choose_large_delta(*correction_base, base_offset,
            collision_source, correction_scratch);
        if (!delta) {
            print_failure("correction_destination_unavailable");
            return 1;
        }
        authority_config.large_position_delta = *delta;
    }
    if (synthetic_scenario ==
            prediction::SyntheticAuthoritativeScenario::teleport ||
        synthetic_scenario == prediction::SyntheticAuthoritativeScenario::mixed) {
        authority_config.teleport_origin = initial_state.origin();
    }

    kernel::GoldSrcLocalMovementScratch authority_scratch;
    auto authority_result =
        prediction::SyntheticAuthoritativePlayerStateSource::create(
            initial_state, *movement_environment.environment, authority_config,
            collision_source, authority_scratch, movement_config);
    if (!authority_result || !authority_result.source) {
        print_failure(authority_result.error
                ? prediction::to_string(authority_result.error->code)
                : std::string_view{"synthetic_authority_failed"});
        return 1;
    }
    std::optional<prediction::SyntheticAuthoritativePlayerStateSource>
        authority;
    authority.emplace(std::move(*authority_result.source));

    prediction::PredictionReconciliationConfig reconciliation_config;
    reconciliation_config.limits.maximum_replay_commands =
        prediction::kHardMaximumPredictionReplayCommands;
    kernel::GoldSrcLocalMovementScratch prediction_scratch;
    kernel::GoldSrcLocalMovementScratch replay_scratch;
    collision::CollisionQueryScratch validation_scratch;
    RunStatistics statistics;
    std::uint64_t route_hash = kFnvOffset;
    std::optional<float> wall_yaw;
    std::uint64_t retired_authority_commands{0U};
    std::uint64_t retired_authority_polls{0U};
    std::uint32_t session_command_sequence = 1U;
    bool hard_reset_applied = false;
    const auto hard_reset_ordinal = (std::max)(
        options->authority_delay_commands + 2U,
        options->command_count / 2U + 1U);

    const auto drain_current_authority = [&]() {
        return drain_authority(*authority, history,
            *movement_environment.environment, collision_source,
            replay_scratch, validation_scratch, movement_config,
            reconciliation_config, statistics, route_hash);
    };
    const auto submit_authority_command =
        [&](const goldsrc::GoldSrcUserCmdState& command) {
            const auto submitted = authority->submit_command(
                command, collision_source, authority_scratch);
            if (!submitted) {
                if (submitted.error) {
                    print_failure(*submitted.error);
                } else {
                    print_failure("synthetic_authority_failed");
                }
                return false;
            }
            if (statistics.authoritative_commands_processed == UINT64_MAX) {
                print_failure("statistics_overflow");
                return false;
            }
            ++statistics.authoritative_commands_processed;
            return true;
        };

    for (std::size_t ordinal = 1U; ordinal <= options->command_count;
         ++ordinal) {
        if (*options->scenario == CheckerScenario::hard_reset &&
            !hard_reset_applied && ordinal == hard_reset_ordinal) {
            const auto discarded_commands = history->size();
            auto replacement_session =
                prediction::create_prediction_session_identity(
                    session_result.session->session_generation + 1U,
                    session_result.session->prediction_generation + 1U,
                    collision_source, *movement_environment.environment,
                    movement_config, initial_state);
            if (!replacement_session || !replacement_session.session) {
                print_failure(replacement_session.error
                        ? prediction::to_string(
                              replacement_session.error->code)
                        : std::string_view{"invalid_session_identity"});
                return 1;
            }
            prediction::AuthoritativePlayerUpdateIdentityCreateInfo
                reset_identity_info;
            reset_identity_info.session = *replacement_session.session;
            reset_identity_info.update_ordinal = 1U;
            reset_identity_info.acknowledgement =
                prediction::AuthoritativeCommandAcknowledgement::none();
            reset_identity_info.discontinuity = prediction::
                AuthoritativePlayerDiscontinuity::respawn_or_hard_reset;
            auto reset_identity =
                prediction::AuthoritativePlayerUpdateIdentity::create(
                    reset_identity_info);
            if (!reset_identity || !reset_identity.identity) {
                print_failure(reset_identity.error
                        ? prediction::to_string(reset_identity.error->code)
                        : std::string_view{"invalid_authoritative_state"});
                return 1;
            }
            auto reset_state =
                prediction::AuthoritativePlayerState::
                    from_synthetic_complete_state(
                        std::move(*reset_identity.identity), initial_state);
            if (!reset_state || !reset_state.state) {
                print_failure(reset_state.error
                        ? prediction::to_string(reset_state.error->code)
                        : std::string_view{"invalid_authoritative_state"});
                return 1;
            }
            if (const auto failure = apply_authority_update(
                    *reset_state.state, history,
                    *movement_environment.environment, collision_source,
                    replay_scratch, validation_scratch, movement_config,
                    reconciliation_config, statistics, route_hash)) {
                print_failure(*failure);
                return 1;
            }
            if (!history->entries().empty() ||
                history->session() != *replacement_session.session ||
                movement::local_player_movement_state_signature(
                    *history->current_predicted_state()) !=
                    movement::local_player_movement_state_signature(
                        initial_state) ||
                statistics.reset_discarded_commands >
                    UINT64_MAX - discarded_commands) {
                print_failure("hard_reset_state_invalid");
                return 1;
            }
            statistics.reset_discarded_commands += discarded_commands;
            if (retired_authority_commands > UINT64_MAX -
                    authority->simulator().statistics().
                        processed_command_count ||
                retired_authority_polls > UINT64_MAX -
                    authority->statistics().polled_update_count) {
                print_failure("statistics_overflow");
                return 1;
            }
            retired_authority_commands += authority->simulator().statistics().
                processed_command_count;
            retired_authority_polls +=
                authority->statistics().polled_update_count;
            authority_config.session = *replacement_session.session;
            authority_config.first_update_ordinal = 2U;
            auto replacement_authority = prediction::
                SyntheticAuthoritativePlayerStateSource::create(
                    initial_state, *movement_environment.environment,
                    authority_config, collision_source, authority_scratch,
                    movement_config);
            if (!replacement_authority || !replacement_authority.source) {
                print_failure(replacement_authority.error
                        ? prediction::to_string(
                              replacement_authority.error->code)
                        : std::string_view{"synthetic_authority_failed"});
                return 1;
            }
            authority.reset();
            authority.emplace(std::move(*replacement_authority.source));
            session_command_sequence = 1U;
            hard_reset_applied = true;
        }
        if (*options->scenario == CheckerScenario::wall_replay &&
            !wall_yaw &&
            history->current_predicted_state()->ground_state().grounded()) {
            wall_yaw = discover_wall_yaw(*history->current_predicted_state(),
                *movement_environment.environment, collision_source);
            statistics.wall_selected = wall_yaw.has_value();
        }
        const auto spec = command_for_scenario(*options->scenario, ordinal,
            *history->current_predicted_state(), wall_yaw);
        const auto made_command = make_command(
            session_command_sequence, spec);
        if (!made_command) {
            print_failure("usercmd_creation_failed");
            return 1;
        }
        auto command = std::make_shared<const goldsrc::GoldSrcUserCmdState>(
            std::move(*made_command));

        const bool backpressure_route = *options->scenario ==
            CheckerScenario::history_backpressure;
        if (!backpressure_route && !submit_authority_command(*command)) {
            return 1;
        }
        const auto pre_state = history->current_predicted_state();
        auto simulated = kernel::GoldSrcLocalMovementKernel::simulate(
            *pre_state, *command, *movement_environment.environment,
            collision_source, prediction_scratch, movement_config);
        if (!simulated || !simulated.state) {
            print_failure(simulated.error
                    ? kernel::to_string(simulated.error->code)
                    : std::string_view{"local_prediction_failed"});
            return 1;
        }
        if (!observe_local_simulation(statistics, simulated)) {
            print_failure("player_startsolid_or_allsolid");
            return 1;
        }
        auto post_state =
            std::make_shared<const movement::LocalPlayerMovementState>(
                std::move(*simulated.state));
        prediction::PredictedCommandAppend append;
        append.command = command;
        append.pre_command_state = pre_state;
        append.post_command_state = post_state;
        append.simulation_statistics = simulated.statistics;
        append.touch_summary = prediction::summarize_prediction_touches(
            simulated.touches,
            simulated.statistics.start_solid_count != 0U,
            simulated.statistics.all_solid_count != 0U);
        const std::array appends{append};
        auto appended = prediction::append_local_prediction_commands(
            *history, std::span<const prediction::PredictedCommandAppend>{appends});
        if (backpressure_route && !appended) {
            const auto preserved_history_signature =
                prediction::local_prediction_history_signature(*history);
            const auto preserved_state_signature =
                movement::local_player_movement_state_signature(
                    *history->current_predicted_state());
            const auto preserved_history_size = history->size();
            const auto preserved_revision = history->revision();
            if (!appended.error || appended.error->code != prediction::
                    PredictionErrorCode::prediction_history_backpressure ||
                appended.history || !appended.final_predicted_state ||
                movement::local_player_movement_state_signature(
                    *appended.final_predicted_state) !=
                    preserved_state_signature ||
                statistics.history_backpressure == UINT64_MAX) {
                print_failure("history_backpressure_not_transactional");
                return 1;
            }
            ++statistics.history_backpressure;
            const auto flushed = authority->flush_next_delayed();
            if (!flushed) {
                print_failure(flushed.error
                        ? prediction::to_string(flushed.error->code)
                        : std::string_view{
                              "synthetic_authority_flush_failed"});
                return 1;
            }
            if (const auto failure = drain_current_authority()) {
                print_failure(*failure);
                return 1;
            }
            if (prediction::local_prediction_history_signature(*history) ==
                    preserved_history_signature ||
                history->revision() <= preserved_revision ||
                history->size() >= preserved_history_size ||
                movement::local_player_movement_state_signature(
                    *history->current_predicted_state()) !=
                    preserved_state_signature) {
                print_failure("history_backpressure_resume_failed");
                return 1;
            }
            appended = prediction::append_local_prediction_commands(
                *history,
                std::span<const prediction::PredictedCommandAppend>{appends});
        }
        if (!appended || !appended.history) {
            print_failure(appended.error
                    ? prediction::to_string(appended.error->code)
                    : std::string_view{"prediction_history_failed"});
            return 1;
        }
        history = std::move(appended.history);
        if (backpressure_route && !submit_authority_command(*command)) {
            return 1;
        }
        ++statistics.commands;
        statistics.history_high_water = (std::max)(
            statistics.history_high_water,
            history->statistics().high_water_mark);
        hash_integral(route_hash, command->command_sequence().value());
        hash_integral(route_hash,
            movement::local_player_movement_state_signature(*post_state));
        hash_integral(route_hash,
            append.touch_summary.deterministic_signature);
        hash_integral(route_hash,
            prediction::local_prediction_history_signature(*history));

        if (const auto failure = drain_current_authority()) {
            print_failure(*failure);
            return 1;
        }
        ++session_command_sequence;
    }

    while (authority->simulator().pending_delayed_update_count() != 0U) {
        const auto flushed = authority->flush_next_delayed();
        if (!flushed) {
            print_failure(flushed.error
                    ? prediction::to_string(flushed.error->code)
                    : std::string_view{"synthetic_authority_flush_failed"});
            return 1;
        }
        if (const auto failure = drain_current_authority()) {
            print_failure(*failure);
            return 1;
        }
    }
    if (const auto failure = drain_current_authority()) {
        print_failure(*failure);
        return 1;
    }

    const bool hard_reset_route = *options->scenario ==
        CheckerScenario::hard_reset;
    const auto total_authority_commands = retired_authority_commands +
        authority->simulator().statistics().processed_command_count;
    const auto total_authority_polls = retired_authority_polls +
        authority->statistics().polled_update_count;
    const bool acknowledgement_accounting_valid = hard_reset_route
        ? statistics.acknowledgements +
                statistics.reset_discarded_commands == statistics.commands &&
            statistics.reconciliations ==
                statistics.acknowledgements + statistics.hard_resets
        : statistics.acknowledgements == statistics.commands &&
            statistics.reconciliations == statistics.commands;
    if (statistics.commands != options->command_count ||
        statistics.authoritative_commands_processed != statistics.commands ||
        total_authority_commands != statistics.commands ||
        !acknowledgement_accounting_valid ||
        !history->entries().empty() ||
        authority->queued_update_count() != 0U ||
        total_authority_polls + statistics.hard_resets !=
            statistics.authority_updates) {
        print_failure("incomplete_prediction_route");
        return 1;
    }
    if (movement::local_player_movement_state_signature(
            *history->current_predicted_state()) !=
        movement::local_player_movement_state_signature(
            authority->simulator().current_state())) {
        print_failure("final_state_mismatch");
        return 1;
    }
    if (*options->scenario == CheckerScenario::wall_replay &&
        options->command_count >= kDefaultCommandCount &&
        (!statistics.wall_selected || !statistics.wall_contact_observed)) {
        print_failure("wall_contact_not_observed");
        return 1;
    }
    if (*options->scenario == CheckerScenario::jump_replay &&
        options->command_count >= kDefaultCommandCount &&
        statistics.jump_count == 0U) {
        print_failure("jump_edge_not_observed");
        return 1;
    }
    if (*options->scenario == CheckerScenario::duck_replay &&
        options->command_count >= kDefaultCommandCount &&
        (statistics.duck_enter_count == 0U ||
            statistics.duck_exit_count == 0U)) {
        print_failure("duck_transition_not_observed");
        return 1;
    }
    if (*options->scenario == CheckerScenario::history_backpressure &&
        (statistics.history_backpressure == 0U ||
            statistics.history_high_water !=
                options->authority_delay_commands ||
            statistics.hard_resets != 0U ||
            statistics.reset_discarded_commands != 0U)) {
        print_failure("required_history_backpressure_not_observed");
        return 1;
    }
    if (*options->scenario == CheckerScenario::hard_reset &&
        (!hard_reset_applied || statistics.hard_resets != 1U ||
            statistics.snaps == 0U ||
            (options->authority_delay_commands != 0U &&
                statistics.reset_discarded_commands == 0U) ||
            statistics.history_backpressure != 0U)) {
        print_failure("required_hard_reset_not_observed");
        return 1;
    }
    if (*options->scenario != CheckerScenario::history_backpressure &&
        statistics.history_backpressure != 0U) {
        print_failure("unexpected_history_backpressure");
        return 1;
    }
    if (*options->scenario != CheckerScenario::hard_reset &&
        (statistics.hard_resets != 0U ||
            statistics.reset_discarded_commands != 0U)) {
        print_failure("unexpected_hard_reset");
        return 1;
    }
    const auto replay_scenario =
        *options->scenario == CheckerScenario::wall_replay ||
        *options->scenario == CheckerScenario::jump_replay ||
        *options->scenario == CheckerScenario::duck_replay;
    if (replay_scenario && options->authority_delay_commands != 0U &&
        options->command_count >= kDefaultCommandCount &&
        (statistics.replays == 0U || statistics.replayed_commands == 0U ||
            statistics.maximum_replay_depth !=
                options->authority_delay_commands)) {
        print_failure("required_command_replay_not_observed");
        return 1;
    }

    hash_integral(route_hash,
        prediction::local_prediction_history_signature(*history));
    hash_integral(route_hash,
        movement::local_player_movement_state_signature(
            *history->current_predicted_state()));
    const auto final_state_hash = signature_hash(
        movement::local_player_movement_state_signature(
            *history->current_predicted_state()));
    const auto history_replay_hash = signature_hash(route_hash);
    if (!final_state_hash || !history_replay_hash) {
        print_failure("state_hash_failed");
        return 1;
    }
    print_summary(statistics, *final_state_hash, *history_replay_hash);
    return 0;
}

} // namespace

int wmain(const int count, wchar_t* arguments[])
{
    try {
        return run_checker(count, arguments);
    } catch (const std::bad_alloc&) {
        print_failure("allocation_failed");
    } catch (const std::exception&) {
        print_failure("checker_failed");
    } catch (...) {
        print_failure("unknown_cpp_exception");
    }
    return 1;
}
