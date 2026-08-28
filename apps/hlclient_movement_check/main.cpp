#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_environment.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/hash/sha256.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_player/local_player_spawn_selector.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <locale>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace collision = hlclient::collision;
namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_collision = hlclient::goldsrc::collision;
namespace local_player = hlclient::local_player;
namespace movement = hlclient::movement;
namespace movement_kernel = hlclient::goldsrc::movement;

enum class Scenario : std::uint8_t {
    summary,
    spawn_settle,
    walk_forward,
    strafe_wall,
    jump,
    step,
    duck,
    deterministic_route,
};

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::optional<std::string> game_directory;
    std::optional<std::string> virtual_map;
    std::optional<Scenario> scenario;
};

struct CommandSpan {
    std::size_t count{0U};
    float forward{0.0F};
    float side{0.0F};
    float yaw{0.0F};
    std::uint16_t buttons{0U};
};

struct RouteSummary {
    movement::PlayerMovementStatistics statistics{};
    std::uint64_t final_signature{0U};
    bool grounded{false};
    movement::PlayerMovementHull hull{movement::PlayerMovementHull::standing};
    movement::PlayerMovementMode mode{movement::PlayerMovementMode::airborne};
};

[[nodiscard]] std::optional<std::string> narrow_printable_ascii(
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

[[nodiscard]] std::optional<Scenario> parse_scenario(
    const std::string_view value) noexcept
{
    if (value == "summary") return Scenario::summary;
    if (value == "spawn-settle") return Scenario::spawn_settle;
    if (value == "walk-forward") return Scenario::walk_forward;
    if (value == "strafe-wall") return Scenario::strafe_wall;
    if (value == "jump") return Scenario::jump;
    if (value == "step") return Scenario::step;
    if (value == "duck") return Scenario::duck;
    if (value == "deterministic-route") return Scenario::deterministic_route;
    return std::nullopt;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int argument_count,
    wchar_t* arguments[])
{
    Options options;
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument != L"--basedir" && argument != L"--game" &&
            argument != L"--map" && argument != L"--scenario") {
            return std::nullopt;
        }
        if (index + 1 >= argument_count) {
            return std::nullopt;
        }
        const std::wstring_view value{arguments[++index]};
        if (value.empty()) {
            return std::nullopt;
        }
        if (argument == L"--basedir") {
            if (options.base_directory) return std::nullopt;
            options.base_directory = std::filesystem::path{value};
            continue;
        }
        auto narrow = narrow_printable_ascii(value);
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
        !options.virtual_map || !options.scenario) {
        return std::nullopt;
    }
    return options;
}

void print_usage()
{
    std::cerr
        << "Usage: hlclient_movement_check --basedir <Half-Life root> "
           "--game <directory> --map <maps/name.bsp> --scenario "
           "<summary|spawn-settle|walk-forward|strafe-wall|jump|step|duck|"
           "deterministic-route>\n";
}

void print_failure(const std::string_view classification)
{
    std::cerr << "[movement-error] classification=" << classification << '\n';
    std::cerr << "[movement] result=failure\n";
}

[[nodiscard]] bool local_source_terminal(
    const hlclient::local_assets::LocalAssetSourceOpenState state) noexcept
{
    using State = hlclient::local_assets::LocalAssetSourceOpenState;
    return state == State::source_ready || state == State::cancelled ||
        state == State::timed_out || state == State::failed;
}

[[nodiscard]] std::optional<hlclient::local_assets::LocalAssetSource>
open_map_source(
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>& environment,
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
    constexpr std::size_t maximum_updates = 1'000'000U;
    constexpr auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < maximum_updates && !local_source_terminal(operation.state());
         ++update) {
        operation.update(now);
    }
    if (operation.state() !=
        hlclient::local_assets::LocalAssetSourceOpenState::source_ready) {
        return std::nullopt;
    }
    return operation.take_result();
}

[[nodiscard]] std::vector<CommandSpan> command_script(
    const Scenario scenario)
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
        return {
            {40U},
            {30U, 220.0F},
            {20U, 180.0F, 0.0F, 45.0F},
            {20U, 0.0F, 180.0F, 45.0F},
            {40U, 0.0F, 0.0F, 45.0F},
            {1U, 0.0F, 0.0F, 45.0F, jump},
            {80U, 0.0F, 0.0F, 45.0F},
            {20U, 100.0F, 0.0F, 45.0F, duck},
            {20U, 100.0F, 0.0F, 45.0F},
            {30U, 0.0F, 0.0F, 45.0F},
        };
    }
    return {};
}

[[nodiscard]] bool requires_grounded_final_state(
    const Scenario scenario) noexcept
{
    switch (scenario) {
    case Scenario::spawn_settle:
    case Scenario::walk_forward:
    case Scenario::jump:
    case Scenario::deterministic_route:
        return true;
    case Scenario::summary:
    case Scenario::strafe_wall:
    case Scenario::step:
    case Scenario::duck:
        return false;
    }
    return false;
}

void aggregate(
    movement::PlayerMovementStatistics& destination,
    const movement::PlayerMovementStatistics& source) noexcept
{
    destination.command_count += source.command_count;
    destination.substep_count += source.substep_count;
    destination.grounded_command_count += source.grounded_command_count;
    destination.airborne_command_count += source.airborne_command_count;
    destination.ground_probe_count += source.ground_probe_count;
    destination.trace_count += source.trace_count;
    destination.collision_hit_count += source.collision_hit_count;
    destination.slide_bump_count += source.slide_bump_count;
    destination.clip_plane_count += source.clip_plane_count;
    destination.step_attempt_count += source.step_attempt_count;
    destination.step_success_count += source.step_success_count;
    destination.jump_count += source.jump_count;
    destination.duck_enter_count += source.duck_enter_count;
    destination.duck_exit_count += source.duck_exit_count;
    destination.stand_blocked_count += source.stand_blocked_count;
    destination.start_solid_count += source.start_solid_count;
    destination.all_solid_count += source.all_solid_count;
    destination.total_horizontal_distance += source.total_horizontal_distance;
    destination.total_vertical_distance += source.total_vertical_distance;
}

[[nodiscard]] std::optional<RouteSummary> run_route(
    movement::LocalPlayerMovementState initial_state,
    const Scenario scenario,
    const movement_kernel::GoldSrcMovementEnvironment& environment,
    const movement_kernel::ILocalMovementCollision& collision_source)
{
    std::optional<movement::LocalPlayerMovementState> state{
        std::move(initial_state)};
    RouteSummary summary;
    movement_kernel::GoldSrcLocalMovementScratch scratch;
    std::uint32_t sequence = 0U;
    for (const auto& span : command_script(scenario)) {
        for (std::size_t command_index = 0U;
             command_index < span.count;
             ++command_index) {
            ++sequence;
            const auto sequence_value =
                goldsrc::GoldSrcUserCmdSequence::create(sequence);
            if (!sequence_value) return std::nullopt;
            auto create_info = goldsrc::goldsrc_usercmd_default_create_info(
                *sequence_value,
                static_cast<std::int64_t>(sequence) * 10'000'000);
            create_info.msec = 10U;
            create_info.sample_duration_nanoseconds = 10'000'000U;
            create_info.view_angles = {0.0F, span.yaw, 0.0F};
            create_info.forward_move = span.forward;
            create_info.side_move = span.side;
            create_info.buttons = span.buttons;
            auto command = goldsrc::GoldSrcUserCmdState::create(create_info);
            if (!command || !command.state) return std::nullopt;
            auto simulated = movement_kernel::GoldSrcLocalMovementKernel::simulate(
                *state, *command.state, environment, collision_source, scratch);
            if (!simulated || !simulated.state) {
                if (simulated.error) {
                    print_failure(movement_kernel::to_string(simulated.error->code));
                    std::cerr << "[movement-error] command=" << sequence
                              << " context=" << simulated.error->context
                              << '\n';
                }
                return std::nullopt;
            }
            aggregate(summary.statistics, simulated.statistics);
            state.emplace(std::move(*simulated.state));
        }
    }
    collision::CollisionQueryScratch position_scratch;
    const auto tested = collision_source.test_position(
        state->origin(), state->hull(), position_scratch);
    if (!tested || !tested.result ||
        tested.result->status !=
            movement_kernel::LocalMovementPositionStatus::free) {
        return std::nullopt;
    }
    summary.final_signature =
        movement::local_player_movement_state_signature(*state);
    summary.grounded = state->ground_state().grounded();
    summary.hull = state->hull();
    summary.mode = state->mode();
    return summary;
}

[[nodiscard]] bool same_summary(
    const RouteSummary& left,
    const RouteSummary& right) noexcept
{
    return left.final_signature == right.final_signature &&
        left.grounded == right.grounded && left.hull == right.hull &&
        left.mode == right.mode &&
        left.statistics.command_count == right.statistics.command_count &&
        left.statistics.grounded_command_count ==
            right.statistics.grounded_command_count &&
        left.statistics.airborne_command_count ==
            right.statistics.airborne_command_count &&
        left.statistics.collision_hit_count ==
            right.statistics.collision_hit_count &&
        left.statistics.step_success_count ==
            right.statistics.step_success_count &&
        left.statistics.jump_count == right.statistics.jump_count &&
        left.statistics.duck_enter_count ==
            right.statistics.duck_enter_count &&
        left.statistics.duck_exit_count == right.statistics.duck_exit_count &&
        left.statistics.start_solid_count ==
            right.statistics.start_solid_count &&
        left.statistics.all_solid_count == right.statistics.all_solid_count;
}

[[nodiscard]] std::optional<std::string> state_sha256(
    const std::uint64_t signature)
{
    std::array<std::byte, sizeof(signature)> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = std::byte{static_cast<std::uint8_t>(
            signature >> static_cast<unsigned int>(index * 8U))};
    }
    const auto digest = hlclient::hash::sha256(bytes);
    return digest ? std::optional{hlclient::hash::sha256_hex(*digest)}
                  : std::nullopt;
}

void print_summary(
    const local_player::LocalPlayerSpawnSelectionResult& spawn,
    const RouteSummary& route,
    const std::string_view final_hash)
{
    const auto& statistics = route.statistics;
    std::cout.imbue(std::locale::classic());
    std::cout
        << "[movement] profile="
        << movement::to_string(
               movement::GoldSrcMovementCompatibilityProfile::
                   public_valve_pm_shared_dry_walk_subset_v1)
        << '\n'
        << "[movement] collision=world-only\n"
        << "[movement] brush-solidity=stock-evidence-pending\n"
        << "[movement] spawn-candidates="
        << spawn.statistics.supported_class_candidate_count << '\n'
        << "[movement] spawn-selected=true\n"
        << "[movement] commands=" << statistics.command_count << '\n'
        << "[movement] grounded-commands="
        << statistics.grounded_command_count << '\n'
        << "[movement] airborne-commands="
        << statistics.airborne_command_count << '\n'
        << "[movement] collision-hits="
        << statistics.collision_hit_count << '\n'
        << "[movement] step-successes="
        << statistics.step_success_count << '\n'
        << "[movement] jump-count=" << statistics.jump_count << '\n'
        << "[movement] duck-transitions="
        << statistics.duck_enter_count + statistics.duck_exit_count << '\n'
        << "[movement] grounded=" << (route.grounded ? "true" : "false")
        << '\n'
        << "[movement] hull=" << movement::to_string(route.hull) << '\n'
        << "[movement] startsolid=" << statistics.start_solid_count << '\n'
        << "[movement] allsolid=" << statistics.all_solid_count << '\n'
        << "[movement] final-state-hash=" << final_hash << '\n'
        << "[movement] network-operations=0\n"
        << "[movement] result=success\n";
}

[[nodiscard]] int run_checker(const int argument_count, wchar_t* arguments[])
{
    const auto options = parse_options(argument_count, arguments);
    if (!options) {
        print_usage();
        return 2;
    }
    auto virtual_map =
        hlclient::local_resources::LocalVirtualResourceName::create(
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
    auto created = hlclient::local_resources::LocalResourceEnvironment::create(
        std::move(*roots.roots), resolver_limits);
    if (!created || !created.environment) {
        print_failure("local_environment_failed");
        return 1;
    }
    auto local_environment = std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>{
        std::move(created.environment)};
    auto resolved = local_environment->resolver().resolve(*virtual_map.name);
    if (!resolved || !resolved.file) {
        print_failure("map_resolution_failed");
        return 1;
    }
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto source_size = resolved.file->file_size();
    resolved.file->close();
    auto locator = local_environment->make_locator(
        root_id, std::move(*virtual_map.name), identity, source_size);
    if (!locator || !locator.locator) {
        print_failure("map_locator_failed");
        return 1;
    }
    resolved.file.reset();
    auto source = open_map_source(local_environment, *locator.locator);
    if (!source) {
        print_failure("map_source_open_failed");
        return 1;
    }
    auto parsed = bsp::GoldSrcBspParser::parse(
        source->source().bytes(), {}, bsp::GoldSrcBspParseOptions{false});
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
    auto collision_package =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(document);
    if (!collision_package || !collision_package.package) {
        print_failure("collision_build_failed");
        return 1;
    }
    movement_kernel::WorldOnlyMovementCollision collision_source{
        collision_package.package};
    collision::CollisionQueryScratch spawn_scratch;
    auto spawn = local_player::LocalPlayerSpawnSelector::select(
        *entities.document, collision_source, spawn_scratch);
    if (!spawn || !spawn.descriptor) {
        print_failure(spawn.error
                ? local_player::to_string(spawn.error->code)
                : std::string_view{"spawn_selection_failed"});
        return 1;
    }
    auto environment = movement_kernel::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline();
    if (!environment || !environment.environment) {
        print_failure("movement_environment_failed");
        return 1;
    }
    movement::LocalPlayerMovementStateCreateInfo state_info;
    state_info.origin = spawn.descriptor->origin;
    state_info.view_angles = spawn.descriptor->view_angles_degrees;
    state_info.hull = movement::PlayerMovementHull::standing;
    state_info.mode = movement::PlayerMovementMode::airborne;
    state_info.view_offset = {
        0.0F,
        0.0F,
        static_cast<float>(
            movement_kernel::kGoldSrcMovementStandingViewOffset),
    };
    auto initial_state = movement::LocalPlayerMovementState::create(state_info);
    if (!initial_state || !initial_state.state) {
        print_failure("initial_state_failed");
        return 1;
    }
    auto first = run_route(
        *initial_state.state, *options->scenario, *environment.environment,
        collision_source);
    auto second = run_route(
        *initial_state.state, *options->scenario, *environment.environment,
        collision_source);
    if (!first || !second || !same_summary(*first, *second)) {
        print_failure("deterministic_route_mismatch");
        return 1;
    }
    if (requires_grounded_final_state(*options->scenario) &&
        !first->grounded) {
        print_failure("required_scenario_not_grounded");
        return 1;
    }
    const auto final_hash = state_sha256(first->final_signature);
    if (!final_hash) {
        print_failure("state_hash_failed");
        return 1;
    }
    print_summary(spawn, *first, *final_hash);
    return 0;
}

} // namespace

int wmain(const int argument_count, wchar_t* arguments[])
{
    try {
        return run_checker(argument_count, arguments);
    } catch (const std::bad_alloc&) {
        print_failure("allocation_failed");
    } catch (const std::exception&) {
        print_failure("checker_failed");
    }
    return 1;
}
