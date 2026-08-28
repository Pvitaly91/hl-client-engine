#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/hash/sha256.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

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
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace collision = hlclient::collision;
namespace goldsrc_collision = hlclient::goldsrc::collision;

enum class Scenario {
    summary,
    spawn_probes,
    deterministic_probes,
};

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::optional<std::string> game_directory;
    std::optional<std::string> virtual_map;
    std::optional<Scenario> scenario;
};

struct ProbeSummary {
    std::uint64_t point_probes{0U};
    std::uint64_t trace_probes{0U};
    std::uint64_t hits{0U};
    std::uint64_t start_solid{0U};
    std::uint64_t all_solid{0U};
    std::uint64_t spawn_metadata_count{0U};
    std::uint64_t spawn_probes_attempted{0U};
    std::uint64_t hull0_open_count{0U};
    std::uint64_t standing_non_solid_count{0U};
    std::uint64_t duck_non_solid_count{0U};
    std::uint64_t downward_hit_count{0U};
    std::vector<std::byte> query_evidence;
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
            if (options.base_directory) {
                return std::nullopt;
            }
            options.base_directory = std::filesystem::path{value};
            continue;
        }
        auto narrow = narrow_printable_ascii(value);
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
            if (*narrow == "summary") {
                options.scenario = Scenario::summary;
            } else if (*narrow == "spawn-probes") {
                options.scenario = Scenario::spawn_probes;
            } else if (*narrow == "deterministic-probes") {
                options.scenario = Scenario::deterministic_probes;
            } else {
                return std::nullopt;
            }
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
        << "Usage: hlclient_collision_check --basedir <Half-Life root> "
           "--game <directory> --map <maps/name.bsp> --scenario "
           "<summary|spawn-probes|deterministic-probes>\n";
}

void print_failure(const std::string_view classification)
{
    std::cerr << "[collision-error] classification=" << classification << '\n';
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
    if (!started) {
        return std::nullopt;
    }
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

template<class Value, bool IsEnum = std::is_enum_v<Value>>
struct IntegralRaw {
    using type = Value;
};

template<class Value>
struct IntegralRaw<Value, true> {
    using type = std::underlying_type_t<Value>;
};

template<class Value>
void append_integral(std::vector<std::byte>& output, Value value)
{
    using Raw = typename IntegralRaw<Value>::type;
    using Unsigned = std::make_unsigned_t<Raw>;
    const auto encoded = static_cast<Unsigned>(static_cast<Raw>(value));
    for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
        output.push_back(std::byte{static_cast<std::uint8_t>(
            encoded >> static_cast<unsigned int>(index * 8U))});
    }
}

template<class Value>
void append_value(std::vector<std::byte>& output, const Value value)
{
    if constexpr (std::is_same_v<Value, float>) {
        append_integral(output, std::bit_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<Value, double>) {
        append_integral(output, std::bit_cast<std::uint64_t>(value));
    } else if constexpr (std::is_same_v<Value, bool>) {
        append_integral(output, static_cast<std::uint8_t>(value ? 1U : 0U));
    } else {
        append_integral(output, value);
    }
}

void append_vector(
    std::vector<std::byte>& output,
    const hlclient::assets::AssetVector3& value)
{
    append_value(output, value.x);
    append_value(output, value.y);
    append_value(output, value.z);
}

[[nodiscard]] std::vector<std::byte> structural_evidence(
    const collision::CollisionWorldPackage& package)
{
    std::vector<std::byte> output;
    output.reserve(
        package.planes().size() * 32U + package.nodes().size() * 24U +
        package.leaves().size() * 12U + package.clipnodes().size() * 32U +
        package.models().size() * 160U + 128U);
    append_value(output, package.compatibility_profile());
    append_value(output, package.evidence_profile());
    const auto& statistics = package.statistics();
    append_value(output, statistics.plane_count);
    append_value(output, statistics.node_count);
    append_value(output, statistics.leaf_count);
    append_value(output, statistics.clipnode_count);
    append_value(output, statistics.model_count);
    append_value(output, statistics.reachable_hull0_nodes);
    append_value(output, statistics.reachable_clipnodes);
    append_value(output, statistics.unreachable_clipnodes);
    append_value(output, statistics.model_hull_root_count);
    append_value(output, statistics.direct_terminal_root_count);
    append_value(output, statistics.maximum_tree_depth);
    if (package.identity().source_fingerprint) {
        append_value(output, package.identity().source_fingerprint->primary);
        append_value(output, package.identity().source_fingerprint->secondary);
    }
    append_value(output, package.identity().source_revision);
    for (const auto& plane : package.planes()) {
        append_vector(output, plane.normal);
        append_value(output, plane.distance);
        append_value(output, plane.source_plane_index);
        append_value(output, plane.source_type);
    }
    for (const auto& node : package.nodes()) {
        append_value(output, node.plane_index);
        for (const auto& child : node.children) {
            append_value(output, child.kind);
            append_value(output, child.index);
        }
    }
    for (const auto& leaf : package.leaves()) {
        append_value(output, leaf.source_leaf_index);
        append_value(output, leaf.contents.source.raw);
        append_value(output, leaf.contents.category);
    }
    for (const auto& node : package.clipnodes()) {
        append_value(output, node.plane_index);
        for (const auto& child : node.children) {
            append_value(output, child.kind);
            append_value(output, child.index);
            append_value(output, child.terminal.source.raw);
            append_value(output, child.terminal.category);
        }
    }
    for (const auto& model : package.models()) {
        append_value(output, model.source_model_index);
        append_vector(output, model.source_origin);
        append_vector(output, model.source_bounds.minimum);
        append_vector(output, model.source_bounds.maximum);
        append_value(output, model.first_source_face);
        append_value(output, model.source_face_count);
        for (const auto& hull : model.hulls) {
            append_value(output, hull.ordinal);
            append_value(output, hull.domain);
            append_value(output, hull.root.kind);
            append_value(output, hull.root.index);
            append_value(output, hull.root.terminal.source.raw);
            append_value(output, hull.root.terminal.category);
            append_vector(output, hull.profile.clip_mins);
            append_vector(output, hull.profile.clip_maxs);
        }
    }
    return output;
}

void append_point_result(
    ProbeSummary& summary,
    const collision::CollisionPointContentsResult& result)
{
    ++summary.point_probes;
    append_value(summary.query_evidence, std::uint8_t{1U});
    append_value(summary.query_evidence, result.contents.source.raw);
    append_value(summary.query_evidence, result.contents.category);
    append_value(summary.query_evidence, result.source_model_index);
    append_value(summary.query_evidence, result.hull);
    append_value(summary.query_evidence, result.traversal_depth);
}

void append_trace_result(
    ProbeSummary& summary,
    const collision::CollisionTraceResult& result)
{
    ++summary.trace_probes;
    summary.hits += static_cast<std::uint64_t>(result.hit.has_value());
    summary.start_solid += static_cast<std::uint64_t>(result.start_solid);
    summary.all_solid += static_cast<std::uint64_t>(result.all_solid);
    append_value(summary.query_evidence, std::uint8_t{2U});
    append_value(summary.query_evidence, result.all_solid);
    append_value(summary.query_evidence, result.start_solid);
    append_value(summary.query_evidence, result.in_open);
    append_value(summary.query_evidence, result.in_liquid);
    append_value(summary.query_evidence, result.trace_profile);
    append_value(summary.query_evidence, result.trace_evidence_profile);
    append_value(summary.query_evidence, result.collision_profile);
    append_value(summary.query_evidence, result.fraction);
    append_vector(summary.query_evidence, result.end_position);
    append_value(summary.query_evidence, result.start_contents.source.raw);
    append_value(summary.query_evidence, result.end_contents.source.raw);
    append_value(summary.query_evidence, result.hit.has_value());
    if (result.hit) {
        append_value(summary.query_evidence, result.hit->kind);
        append_value(summary.query_evidence, result.hit->source_model_index);
    }
    append_value(summary.query_evidence, result.collision_plane.has_value());
    if (result.collision_plane) {
        append_vector(summary.query_evidence, result.collision_plane->normal);
        append_value(summary.query_evidence, result.collision_plane->distance);
        append_value(
            summary.query_evidence,
            result.collision_plane->source_plane_index);
        append_value(
            summary.query_evidence,
            result.collision_plane->orientation);
    }
}

[[nodiscard]] bool query_point(
    const collision::CollisionWorldQuery& query,
    collision::CollisionQueryScratch& scratch,
    ProbeSummary& summary,
    const hlclient::assets::AssetVector3 point,
    const collision::CollisionHullOrdinal hull,
    const std::uint32_t source_model_index,
    collision::CollisionPointContentsResult* retained = nullptr)
{
    auto queried = query.point_contents(
        collision::CollisionPointContentsRequest{
            point, source_model_index, hull},
        scratch);
    if (!queried || !queried.result) {
        return false;
    }
    if (retained != nullptr) {
        *retained = *queried.result;
    }
    append_point_result(summary, *queried.result);
    return true;
}

[[nodiscard]] bool query_trace(
    const collision::CollisionWorldQuery& query,
    collision::CollisionQueryScratch& scratch,
    ProbeSummary& summary,
    const hlclient::assets::AssetVector3 start,
    const hlclient::assets::AssetVector3 end,
    const collision::CollisionHullOrdinal hull,
    const std::uint32_t source_model_index,
    collision::CollisionTraceResult* retained = nullptr)
{
    collision::CollisionTraceRequest request;
    request.start = start;
    request.end = end;
    request.source_model_index = source_model_index;
    request.hull = hull;
    auto queried = source_model_index == 0U
        ? query.trace_hull(request, scratch)
        : query.trace_model_hull(request, scratch);
    if (!queried || !queried.result) {
        return false;
    }
    if (retained != nullptr) {
        *retained = *queried.result;
    }
    append_trace_result(summary, *queried.result);
    return true;
}

[[nodiscard]] hlclient::assets::AssetVector3 interpolate_bounds(
    const hlclient::assets::WorldBounds& bounds,
    const float x,
    const float y,
    const float z) noexcept
{
    const auto interpolate = [](const float minimum,
                                const float maximum,
                                const float fraction) noexcept {
        return minimum + (maximum - minimum) * fraction;
    };
    return {
        interpolate(bounds.minimum.x, bounds.maximum.x, x),
        interpolate(bounds.minimum.y, bounds.maximum.y, y),
        interpolate(bounds.minimum.z, bounds.maximum.z, z),
    };
}

[[nodiscard]] bool run_bounds_corpus(
    const collision::CollisionWorldQuery& query,
    collision::CollisionQueryScratch& scratch,
    ProbeSummary& summary,
    const collision::CollisionModel& model)
{
    const auto bounds = model.source_bounds;
    constexpr std::array fractions{0.125F, 0.25F, 0.5F, 0.75F, 0.875F};
    constexpr std::array hulls{
        collision::CollisionHullOrdinal::point,
        collision::CollisionHullOrdinal::standing_32x32x72,
        collision::CollisionHullOrdinal::large_64_cube,
        collision::CollisionHullOrdinal::duck_32x32x36,
    };
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        for (const auto fraction : fractions) {
            auto coordinates = std::array{0.5F, 0.5F, 0.5F};
            coordinates[axis] = fraction;
            const auto point = interpolate_bounds(
                bounds, coordinates[0U], coordinates[1U], coordinates[2U]);
            for (const auto hull : hulls) {
                if (!query_point(
                        query,
                        scratch,
                        summary,
                        point,
                        hull,
                        model.source_model_index)) {
                    return false;
                }
            }
        }
    }

    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        auto start_coordinates = std::array{0.1F, 0.5F, 0.5F};
        auto end_coordinates = std::array{0.9F, 0.5F, 0.5F};
        std::swap(start_coordinates[0U], start_coordinates[axis]);
        std::swap(end_coordinates[0U], end_coordinates[axis]);
        const auto start = interpolate_bounds(
            bounds,
            start_coordinates[0U],
            start_coordinates[1U],
            start_coordinates[2U]);
        const auto end = interpolate_bounds(
            bounds,
            end_coordinates[0U],
            end_coordinates[1U],
            end_coordinates[2U]);
        for (const auto hull : hulls) {
            if (!query_trace(
                    query,
                    scratch,
                    summary,
                    start,
                    end,
                    hull,
                    model.source_model_index) ||
                !query_trace(
                    query,
                    scratch,
                    summary,
                    end,
                    start,
                    hull,
                    model.source_model_index)) {
                return false;
            }
        }
    }
    const auto center = interpolate_bounds(bounds, 0.5F, 0.5F, 0.5F);
    for (const auto hull : hulls) {
        if (!query_trace(
                query,
                scratch,
                summary,
                center,
                center,
                hull,
                model.source_model_index)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool run_deterministic_corpus(
    const collision::CollisionWorldQuery& query,
    ProbeSummary& summary)
{
    if (!query.package() || query.package()->models().empty() ||
        query.package()->models().size() >
            collision::kCollisionHardMaximumModels) {
        return false;
    }

    collision::CollisionQueryScratch scratch;
    for (const auto& model : query.package()->models()) {
        if (!run_bounds_corpus(query, scratch, summary, model)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<hlclient::assets::AssetVector3> parse_origin(
    const std::string_view text) noexcept
{
    std::array<float, 3U> values{};
    std::size_t cursor = 0U;
    for (std::size_t component = 0U; component < values.size(); ++component) {
        while (cursor < text.size() &&
               (text[cursor] == ' ' || text[cursor] == '\t')) {
            ++cursor;
        }
        if (cursor == text.size()) {
            return std::nullopt;
        }
        const auto begin = text.data() + cursor;
        const auto end = text.data() + text.size();
        const auto parsed = std::from_chars(
            begin, end, values[component], std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr == begin ||
            !std::isfinite(values[component])) {
            return std::nullopt;
        }
        cursor = static_cast<std::size_t>(parsed.ptr - text.data());
    }
    while (cursor < text.size() &&
           (text[cursor] == ' ' || text[cursor] == '\t')) {
        ++cursor;
    }
    if (cursor != text.size()) {
        return std::nullopt;
    }
    return hlclient::assets::AssetVector3{values[0U], values[1U], values[2U]};
}

[[nodiscard]] bool run_spawn_probes(
    const bsp::GoldSrcBspParsedDocument& document,
    const collision::CollisionWorldQuery& query,
    ProbeSummary& summary)
{
    auto parsed = bsp::GoldSrcEntityDocumentParser::parse(
        document.entity_lump_bytes);
    if (!parsed || !parsed.document) {
        return false;
    }
    collision::CollisionQueryScratch scratch;
    constexpr std::size_t maximum_spawns = 256U;
    for (const auto& entity : parsed.document->entities()) {
        const auto classname = bsp::find_interpreted_key(
            entity, bsp::GoldSrcInterpretedEntityKey::classname);
        const auto* classname_pair = classname.unique_pair(entity);
        if (classname_pair == nullptr ||
            (classname_pair->value != "info_player_start" &&
             classname_pair->value != "info_player_deathmatch")) {
            continue;
        }
        ++summary.spawn_metadata_count;
        if (summary.spawn_probes_attempted == maximum_spawns) {
            continue;
        }
        const auto origin_key = bsp::find_interpreted_key(
            entity, bsp::GoldSrcInterpretedEntityKey::origin);
        const auto* origin_pair = origin_key.unique_pair(entity);
        if (origin_pair == nullptr) {
            continue;
        }
        const auto origin = parse_origin(origin_pair->value);
        if (!origin) {
            continue;
        }
        ++summary.spawn_probes_attempted;
        collision::CollisionPointContentsResult point;
        if (!query_point(
                query,
                scratch,
                summary,
                *origin,
                collision::CollisionHullOrdinal::point,
                0U,
                &point)) {
            return false;
        }
        summary.hull0_open_count += static_cast<std::uint64_t>(
            collision::is_open_space(point.contents.category));

        for (const auto [hull, standing] : std::array{
                 std::pair{collision::CollisionHullOrdinal::standing_32x32x72,
                           true},
                 std::pair{collision::CollisionHullOrdinal::duck_32x32x36,
                           false}}) {
            auto tested = query.test_position(
                collision::CollisionPointContentsRequest{
                    *origin, 0U, hull},
                scratch);
            if (!tested || !tested.result) {
                return false;
            }
            append_value(summary.query_evidence, std::uint8_t{3U});
            append_value(summary.query_evidence, tested.result->status);
            append_value(
                summary.query_evidence, tested.result->contents.source.raw);
            if (tested.result->status ==
                collision::CollisionPositionStatus::free) {
                if (standing) {
                    ++summary.standing_non_solid_count;
                } else {
                    ++summary.duck_non_solid_count;
                }
            }
        }

        auto down = *origin;
        down.z -= 256.0F;
        collision::CollisionTraceResult trace;
        if (!query_trace(
                query,
                scratch,
                summary,
                *origin,
                down,
                collision::CollisionHullOrdinal::standing_32x32x72,
                0U,
                &trace)) {
            return false;
        }
        summary.downward_hit_count +=
            static_cast<std::uint64_t>(trace.hit.has_value());
    }
    return true;
}

void print_summary(
    const collision::CollisionWorldPackage& package,
    const ProbeSummary& probes,
    const std::string_view structural_hash,
    const std::string_view query_hash)
{
    const auto& statistics = package.statistics();
    std::cout.imbue(std::locale::classic());
    std::cout
        << "[collision] profile=valve_bsp_v30_clip_hulls_v1\n"
        << "[collision] trace-profile=project_deterministic_bsp_hull_trace_v1\n"
        << "[collision] planes=" << package.planes().size() << '\n'
        << "[collision] nodes=" << package.nodes().size() << '\n'
        << "[collision] leaves=" << package.leaves().size() << '\n'
        << "[collision] clipnodes=" << package.clipnodes().size() << '\n'
        << "[collision] models=" << package.models().size() << '\n'
        << "[collision] hull-roots=" << statistics.model_hull_root_count << '\n'
        << "[collision] reachable-nodes="
        << statistics.reachable_hull0_nodes << '\n'
        << "[collision] reachable-clipnodes="
        << statistics.reachable_clipnodes << '\n'
        << "[collision] unreachable-clipnodes="
        << statistics.unreachable_clipnodes << '\n'
        << "[collision] maximum-depth=" << statistics.maximum_tree_depth << '\n'
        << "[collision] point-probes=" << probes.point_probes << '\n'
        << "[collision] trace-probes=" << probes.trace_probes << '\n'
        << "[collision] hits=" << probes.hits << '\n'
        << "[collision] startsolid=" << probes.start_solid << '\n'
        << "[collision] allsolid=" << probes.all_solid << '\n'
        << "[collision] spawn-metadata=" << probes.spawn_metadata_count << '\n'
        << "[collision] spawn-probes=" << probes.spawn_probes_attempted << '\n'
        << "[collision] hull0-open=" << probes.hull0_open_count << '\n'
        << "[collision] standing-non-solid="
        << probes.standing_non_solid_count << '\n'
        << "[collision] duck-non-solid=" << probes.duck_non_solid_count << '\n'
        << "[collision] downward-hits=" << probes.downward_hit_count << '\n'
        << "[collision] structural-hash=" << structural_hash << '\n'
        << "[collision] query-hash=" << query_hash << '\n'
        << "[collision] result=success\n";
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
    auto resolver_limits =
        hlclient::local_resources::LocalResourceResolverLimits{};
    resolver_limits.maximum_file_size =
        hlclient::local_resources::kHardMaximumLocalResourceFileSize;
    auto created = hlclient::local_resources::LocalResourceEnvironment::create(
        std::move(*roots.roots), resolver_limits);
    if (!created || !created.environment) {
        print_failure("local_environment_failed");
        return 1;
    }
    auto environment = std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>{
        std::move(created.environment)};

    auto resolved = environment->resolver().resolve(*virtual_map.name);
    if (!resolved || !resolved.file) {
        print_failure("map_resolution_failed");
        return 1;
    }
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto source_size = resolved.file->file_size();
    resolved.file->close();
    auto locator = environment->make_locator(
        root_id, std::move(*virtual_map.name), identity, source_size);
    if (!locator || !locator.locator) {
        print_failure("map_locator_failed");
        return 1;
    }
    resolved.file.reset();

    auto source = open_map_source(environment, *locator.locator);
    if (!source) {
        print_failure("map_source_open_failed");
        return 1;
    }
    auto parsed = bsp::GoldSrcBspParser::parse(
        source->source().bytes(), {}, bsp::GoldSrcBspParseOptions{false});
    if (!parsed || !parsed.document) {
        print_failure(parsed.error
                ? bsp::to_string(parsed.error->code)
                : std::string_view{"bsp_parse_failed"});
        return 1;
    }
    auto document = std::move(*parsed.document);
    auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(document);
    if (!built || !built.package) {
        print_failure(built.error
                ? goldsrc_collision::to_string(built.error->code)
                : std::string_view{"collision_build_failed"});
        return 1;
    }
    if (built.package->clipnodes().empty() || built.package->models().empty()) {
        print_failure("incomplete_collision_package");
        return 1;
    }

    ProbeSummary probes;
    collision::CollisionWorldQuery query{built.package};
    if (*options->scenario == Scenario::deterministic_probes &&
        !run_deterministic_corpus(query, probes)) {
        print_failure("deterministic_query_failed");
        return 1;
    }
    if ((*options->scenario == Scenario::spawn_probes ||
         *options->scenario == Scenario::deterministic_probes) &&
        !run_spawn_probes(document, query, probes)) {
        print_failure("spawn_query_failed");
        return 1;
    }

    const auto structural_bytes = structural_evidence(*built.package);
    const auto structural_digest = hlclient::hash::sha256(structural_bytes);
    const auto query_digest = hlclient::hash::sha256(probes.query_evidence);
    if (!structural_digest || !query_digest) {
        print_failure("hash_failed");
        return 1;
    }
    print_summary(
        *built.package,
        probes,
        hlclient::hash::sha256_hex(*structural_digest),
        hlclient::hash::sha256_hex(*query_digest));
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
