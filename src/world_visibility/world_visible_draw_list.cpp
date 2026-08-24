#include <hlclient/world_visibility/world_visible_draw_list.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace hlclient::world_visibility {
namespace {

constexpr std::uint64_t kSignatureFnvOffsetBasis =
    14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kSignatureFnvPrime = 1'099'511'628'211ULL;

class DrawSignatureHasher final {
public:
    void add(const bool value) noexcept
    {
        add(static_cast<std::uint8_t>(value ? 1U : 0U));
    }

    template <typename Integer>
        requires std::is_integral_v<Integer> &&
            (!std::is_same_v<std::remove_cv_t<Integer>, bool>)
    void add(const Integer value) noexcept
    {
        using Unsigned = std::make_unsigned_t<Integer>;
        auto remaining = static_cast<Unsigned>(value);
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            value_ ^= static_cast<std::uint8_t>(remaining & 0xFFU);
            value_ *= kSignatureFnvPrime;
            remaining >>= 8U;
        }
    }

    template <typename Enumeration>
        requires std::is_enum_v<Enumeration>
    void add(const Enumeration value) noexcept
    {
        add(static_cast<std::underlying_type_t<Enumeration>>(value));
    }

    void add_float(const float value) noexcept
    {
        add(std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? kSignatureFnvOffsetBasis : value_;
    }

private:
    std::uint64_t value_{kSignatureFnvOffsetBasis};
};

void hash_vector(
    DrawSignatureHasher& hasher,
    const assets::AssetVector3& value) noexcept
{
    hasher.add_float(value.x);
    hasher.add_float(value.y);
    hasher.add_float(value.z);
}

void hash_surface(
    DrawSignatureHasher& hasher,
    const WorldVisibleSurfaceInput& surface) noexcept
{
    hasher.add(surface.source_surface_index);
    hasher.add(surface.first_index);
    hasher.add(surface.index_count);
    hasher.add(static_cast<std::uint64_t>(surface.render_material_index));
    hash_vector(hasher, surface.bounds.minimum);
    hash_vector(hasher, surface.bounds.maximum);
    hasher.add(surface.alpha_mode);
    hasher.add(surface.lightmap_mode);
    hasher.add(surface.lightmap_atlas_page_index.has_value());
    hasher.add(static_cast<std::uint64_t>(
        surface.lightmap_atlas_page_index.value_or(0U)));
}

[[nodiscard]] std::uint64_t draw_input_signature_impl(
    const WorldVisibleDrawListBuildInput& input) noexcept
{
    DrawSignatureHasher hasher;
    hasher.add(static_cast<std::uint32_t>(0x44534947U));
    hasher.add(static_cast<std::uint64_t>(input.world_surfaces.size()));
    hasher.add(static_cast<std::uint64_t>(
        input.world_index_buffer_index_count));
    hasher.add(static_cast<std::uint64_t>(
        input.world_render_material_count));
    for (const auto& surface : input.world_surfaces) {
        hash_surface(hasher, surface);
    }
    hasher.add(static_cast<std::uint64_t>(input.brush_models.size()));
    for (const auto& model : input.brush_models) {
        hasher.add(model.source_model_index);
        hasher.add(static_cast<std::uint64_t>(
            model.index_buffer_index_count));
        hasher.add(static_cast<std::uint64_t>(model.render_material_count));
        hasher.add(static_cast<std::uint64_t>(model.surfaces.size()));
        for (const auto& surface : model.surfaces) {
            hash_surface(hasher, surface);
        }
    }
    hasher.add(static_cast<std::uint64_t>(input.brush_instances.size()));
    for (const auto& instance : input.brush_instances) {
        hasher.add(instance.source_instance_index);
        hasher.add(instance.source_model_index);
        for (const auto value : instance.model_transform.values) {
            hasher.add_float(value);
        }
    }
    return hasher.value();
}

[[nodiscard]] WorldVisibleDrawListBuildResult fail(
    const WorldVisibleDrawListErrorCode code,
    const std::optional<std::size_t> index,
    std::string message)
{
    return {
        std::nullopt,
        WorldVisibleDrawListError{code, index, std::move(message)},
    };
}

[[nodiscard]] bool valid_alpha_mode(
    const assets::WorldTextureAlphaMode mode) noexcept
{
    return mode == assets::WorldTextureAlphaMode::opaque ||
        mode == assets::WorldTextureAlphaMode::masked_index_255;
}

[[nodiscard]] bool valid_lightmap_mode(
    const world_render::WorldRenderLightmapMode mode) noexcept
{
    return mode == world_render::WorldRenderLightmapMode::atlas ||
        mode == world_render::WorldRenderLightmapMode::unlit_white;
}

[[nodiscard]] bool valid_range(
    const WorldVisibleSurfaceInput& surface,
    const std::size_t index_count,
    const std::size_t material_count) noexcept
{
    if (surface.index_count == 0U || surface.index_count % 3U != 0U ||
        surface.render_material_index >= material_count) {
        return false;
    }
    const auto first = static_cast<std::size_t>(surface.first_index);
    const auto count = static_cast<std::size_t>(surface.index_count);
    return first <= index_count && count <= index_count - first;
}

[[nodiscard]] bool valid_material_profile(
    const WorldVisibleSurfaceInput& surface) noexcept
{
    if (!valid_alpha_mode(surface.alpha_mode) ||
        !valid_lightmap_mode(surface.lightmap_mode)) {
        return false;
    }
    return (surface.lightmap_mode == world_render::WorldRenderLightmapMode::atlas &&
               surface.lightmap_atlas_page_index.has_value()) ||
        (surface.lightmap_mode == world_render::WorldRenderLightmapMode::unlit_white &&
            !surface.lightmap_atlas_page_index.has_value());
}

[[nodiscard]] bool has_overlapping_ranges(
    std::vector<const WorldVisibleSurfaceInput*> surfaces)
{
    std::ranges::sort(surfaces, {}, [](const WorldVisibleSurfaceInput* surface) {
        return surface->first_index;
    });
    for (std::size_t index = 1U; index < surfaces.size(); ++index) {
        const auto& previous = *surfaces[index - 1U];
        const auto& current = *surfaces[index];
        const auto previous_end =
            static_cast<std::uint64_t>(previous.first_index) +
            previous.index_count;
        if (previous_end > current.first_index) {
            return true;
        }
    }
    return false;
}

template <typename Type, typename Key>
[[nodiscard]] bool has_duplicate_key(
    const std::span<const Type> values,
    Key key)
{
    std::vector<const Type*> ordered;
    ordered.reserve(values.size());
    for (const auto& value : values) {
        ordered.push_back(&value);
    }
    std::ranges::sort(ordered, {}, [&](const Type* value) {
        return key(*value);
    });
    return std::adjacent_find(ordered.begin(), ordered.end(),
               [&](const Type* left, const Type* right) {
                   return key(*left) == key(*right);
               }) != ordered.end();
}

template <typename Type>
struct SortedLookupEntry {
    std::uint32_t source_index{0U};
    const Type* value{nullptr};
};

template <typename Type, typename Key>
[[nodiscard]] std::vector<SortedLookupEntry<Type>> make_sorted_lookup(
    const std::span<const Type> values,
    Key key)
{
    std::vector<SortedLookupEntry<Type>> lookup;
    lookup.reserve(values.size());
    for (const auto& value : values) {
        lookup.push_back({key(value), &value});
    }
    std::ranges::sort(lookup, {}, [](const SortedLookupEntry<Type>& entry) {
        return entry.source_index;
    });
    return lookup;
}

template <typename Type>
[[nodiscard]] bool has_duplicate_source_index(
    const std::vector<SortedLookupEntry<Type>>& lookup) noexcept
{
    return std::adjacent_find(lookup.begin(), lookup.end(),
               [](const auto& left, const auto& right) {
                   return left.source_index == right.source_index;
               }) != lookup.end();
}

template <typename Type>
[[nodiscard]] const Type* find_in_lookup(
    const std::vector<SortedLookupEntry<Type>>& lookup,
    const std::uint32_t wanted) noexcept
{
    const auto found = std::ranges::lower_bound(
        lookup,
        wanted,
        {},
        [](const SortedLookupEntry<Type>& entry) {
            return entry.source_index;
        });
    return found != lookup.end() && found->source_index == wanted
        ? found->value
        : nullptr;
}

[[nodiscard]] WorldVisibleDrawCommand make_command(
    const WorldVisibleObjectKind kind,
    const WorldVisibleSurfaceInput& surface,
    const renderer::RenderMatrix4& transform,
    const std::optional<std::uint32_t> model,
    const std::optional<std::uint32_t> instance) noexcept
{
    return {
        kind,
        surface.first_index,
        surface.index_count,
        surface.render_material_index,
        transform,
        surface.source_surface_index,
        model,
        instance,
        surface.alpha_mode,
        surface.lightmap_mode,
        surface.lightmap_atlas_page_index,
    };
}

[[nodiscard]] std::size_t group_rank(
    const WorldVisibleDrawCommand& command) noexcept
{
    const auto masked = command.alpha_mode ==
        assets::WorldTextureAlphaMode::masked_index_255;
    if (command.object_kind == WorldVisibleObjectKind::world_surface) {
        return masked ? 1U : 0U;
    }
    return masked ? 3U : 2U;
}

[[nodiscard]] auto ordering_key(const WorldVisibleDrawCommand& command) noexcept
{
    // Material index is the package's deterministic material key. Object and
    // source-surface ordinals provide stable order within that material.
    const auto object_ordinal = command.source_instance_index.value_or(0U);
    return std::tuple{
        group_rank(command),
        command.render_material_index,
        command.lightmap_mode,
        command.lightmap_atlas_page_index.value_or(0U),
        object_ordinal,
        command.source_surface_index,
    };
}

} // namespace

std::uint64_t world_visible_draw_input_signature(
    const WorldVisibleDrawListBuildInput& input) noexcept
{
    return draw_input_signature_impl(input);
}

std::string_view to_string(const WorldVisibleDrawListErrorCode code) noexcept
{
    switch (code) {
    case WorldVisibleDrawListErrorCode::invalid_input:
        return "invalid_input";
    case WorldVisibleDrawListErrorCode::duplicate_surface_range:
        return "duplicate_surface_range";
    case WorldVisibleDrawListErrorCode::duplicate_brush_model:
        return "duplicate_brush_model";
    case WorldVisibleDrawListErrorCode::duplicate_brush_instance:
        return "duplicate_brush_instance";
    case WorldVisibleDrawListErrorCode::visible_surface_not_found:
        return "visible_surface_not_found";
    case WorldVisibleDrawListErrorCode::visible_brush_instance_not_found:
        return "visible_brush_instance_not_found";
    case WorldVisibleDrawListErrorCode::brush_model_not_found:
        return "brush_model_not_found";
    case WorldVisibleDrawListErrorCode::invalid_index_range:
        return "invalid_index_range";
    case WorldVisibleDrawListErrorCode::invalid_material_reference:
        return "invalid_material_reference";
    case WorldVisibleDrawListErrorCode::invalid_material_profile:
        return "invalid_material_profile";
    case WorldVisibleDrawListErrorCode::non_finite_transform:
        return "non_finite_transform";
    case WorldVisibleDrawListErrorCode::command_limit_exceeded:
        return "command_limit_exceeded";
    case WorldVisibleDrawListErrorCode::unable_to_retain_draw_list:
        return "unable_to_retain_draw_list";
    }
    return "unknown";
}

WorldVisibleDrawList::WorldVisibleDrawList(
    std::vector<WorldVisibleDrawCommand> commands,
    const WorldVisibleDrawListStatistics statistics,
    const std::uint64_t visibility_revision,
    const WorldVisibilitySceneIdentity scene_identity,
    const WorldVisibilityResultSignature result_signature) noexcept
    : commands_{std::move(commands)},
      statistics_{statistics},
      visibility_revision_{visibility_revision},
      scene_identity_{scene_identity},
      result_signature_{result_signature}
{
}

WorldVisibleDrawList::WorldVisibleDrawList(
    WorldVisibleDrawList&& other) noexcept
    : commands_{std::move(other.commands_)},
      statistics_{other.statistics_},
      visibility_revision_{other.visibility_revision_},
      scene_identity_{other.scene_identity_},
      result_signature_{other.result_signature_}
{
    other.visibility_revision_ = 0U;
    other.scene_identity_ = {};
    other.result_signature_ = {};
}

std::span<const WorldVisibleDrawCommand> WorldVisibleDrawList::commands() const noexcept
{
    return commands_;
}

const WorldVisibleDrawListStatistics& WorldVisibleDrawList::statistics() const noexcept
{
    return statistics_;
}

std::uint64_t WorldVisibleDrawList::visibility_revision() const noexcept
{
    return visibility_revision_;
}

WorldVisibilitySceneIdentity WorldVisibleDrawList::scene_identity() const noexcept
{
    return scene_identity_;
}

WorldVisibilityResultSignature WorldVisibleDrawList::result_signature() const noexcept
{
    return result_signature_;
}

WorldVisibleDrawListBuildResult WorldVisibleDrawListBuilder::build(
    const WorldVisibleDrawListBuildInput& input,
    const WorldVisibleDrawListLimits& limits) const
{
    try {
        if (input.visibility == nullptr || limits.maximum_draw_commands == 0U) {
            return fail(WorldVisibleDrawListErrorCode::invalid_input,
                std::nullopt,
                "Draw-list input or limits are invalid");
        }
        if (!input.visibility->result_signature()) {
            return fail(WorldVisibleDrawListErrorCode::invalid_input,
                std::nullopt,
                "Visibility result has no valid pairing signature");
        }
        if (input.visibility->scene_identity().draw_input_signature != 0U &&
            input.visibility->scene_identity().draw_input_signature !=
                world_visible_draw_input_signature(input)) {
            return fail(WorldVisibleDrawListErrorCode::invalid_input,
                std::nullopt,
                "Draw adapters do not match the bound scene identity");
        }
        const auto world_surface_lookup = make_sorted_lookup(
            input.world_surfaces,
            [](const WorldVisibleSurfaceInput& surface) {
                return surface.source_surface_index;
            });
        if (has_duplicate_source_index(world_surface_lookup)) {
            return fail(WorldVisibleDrawListErrorCode::duplicate_surface_range,
                std::nullopt,
                "World surface source ordinals must be unique");
        }
        std::vector<const WorldVisibleSurfaceInput*> all_world_ranges;
        all_world_ranges.reserve(input.world_surfaces.size());
        for (const auto& surface : input.world_surfaces) {
            if (!valid_range(surface,
                    input.world_index_buffer_index_count,
                    input.world_render_material_count)) {
                const auto code = surface.render_material_index >=
                        input.world_render_material_count
                    ? WorldVisibleDrawListErrorCode::invalid_material_reference
                    : WorldVisibleDrawListErrorCode::invalid_index_range;
                return fail(code, surface.source_surface_index,
                    "World surface render range is invalid");
            }
            if (!valid_material_profile(surface)) {
                return fail(WorldVisibleDrawListErrorCode::invalid_material_profile,
                    surface.source_surface_index,
                    "World surface alpha/lightmap profile is invalid");
            }
            all_world_ranges.push_back(&surface);
        }
        if (has_overlapping_ranges(std::move(all_world_ranges))) {
            return fail(WorldVisibleDrawListErrorCode::invalid_index_range,
                std::nullopt,
                "World surface index ranges overlap");
        }
        const auto brush_model_lookup = make_sorted_lookup(
            input.brush_models,
            [](const WorldVisibleBrushModelInput& model) {
                return model.source_model_index;
            });
        if (has_duplicate_source_index(brush_model_lookup)) {
            return fail(WorldVisibleDrawListErrorCode::duplicate_brush_model,
                std::nullopt,
                "Brush source model ordinals must be unique");
        }
        const auto brush_instance_lookup = make_sorted_lookup(
            input.brush_instances,
            [](const WorldVisibleBrushInstanceDrawInput& instance) {
                return instance.source_instance_index;
            });
        if (has_duplicate_source_index(brush_instance_lookup)) {
            return fail(WorldVisibleDrawListErrorCode::duplicate_brush_instance,
                std::nullopt,
                "Brush source instance ordinals must be unique");
        }

        for (const auto& model : input.brush_models) {
            if (model.source_model_index == 0U || model.surfaces.empty()) {
                return fail(WorldVisibleDrawListErrorCode::invalid_input,
                    model.source_model_index,
                    "Brush render model must be non-world and contain surfaces");
            }
            if (has_duplicate_key(model.surfaces,
                    [](const WorldVisibleSurfaceInput& surface) {
                        return surface.source_surface_index;
                    })) {
                return fail(WorldVisibleDrawListErrorCode::duplicate_surface_range,
                    model.source_model_index,
                    "Brush model surfaces must have unique source ordinals");
            }
            std::vector<const WorldVisibleSurfaceInput*> all_model_ranges;
            all_model_ranges.reserve(model.surfaces.size());
            for (const auto& surface : model.surfaces) {
                if (!valid_range(surface, model.index_buffer_index_count,
                        model.render_material_count)) {
                    const auto code = surface.render_material_index >=
                            model.render_material_count
                        ? WorldVisibleDrawListErrorCode::invalid_material_reference
                        : WorldVisibleDrawListErrorCode::invalid_index_range;
                    return fail(code, model.source_model_index,
                        "Brush surface render range is invalid");
                }
                if (!valid_material_profile(surface)) {
                    return fail(WorldVisibleDrawListErrorCode::invalid_material_profile,
                        model.source_model_index,
                        "Brush surface alpha/lightmap profile is invalid");
                }
                all_model_ranges.push_back(&surface);
            }
            if (has_overlapping_ranges(std::move(all_model_ranges))) {
                return fail(WorldVisibleDrawListErrorCode::invalid_index_range,
                    model.source_model_index,
                    "Brush model surface index ranges overlap");
            }
        }

        std::vector<WorldVisibleDrawCommand> commands;
        const auto visible_world = input.visibility->visible_world_surface_indices();
        const auto visible_brush = input.visibility->visible_brush_instance_indices();
        if (visible_world.size() > limits.maximum_draw_commands ||
            visible_brush.size() > limits.maximum_draw_commands) {
            return fail(WorldVisibleDrawListErrorCode::command_limit_exceeded,
                std::nullopt,
                "Visible object cardinality exceeds the draw-command limit");
        }
        commands.reserve(std::min(limits.maximum_draw_commands,
            visible_world.size() + visible_brush.size()));

        std::vector<std::uint32_t> selected_world(
            visible_world.begin(), visible_world.end());
        std::ranges::sort(selected_world);
        if (std::adjacent_find(selected_world.begin(), selected_world.end()) !=
            selected_world.end()) {
            return fail(WorldVisibleDrawListErrorCode::invalid_input,
                std::nullopt,
                "Visibility set contains a duplicate world surface");
        }

        for (const auto source_surface : visible_world) {
            const auto* surface = find_in_lookup(
                world_surface_lookup, source_surface);
            if (surface == nullptr) {
                return fail(WorldVisibleDrawListErrorCode::visible_surface_not_found,
                    source_surface,
                    "Visible world surface has no exact render range");
            }
            if (commands.size() >= limits.maximum_draw_commands) {
                return fail(WorldVisibleDrawListErrorCode::command_limit_exceeded,
                    source_surface,
                    "World draw command exceeds the configured limit");
            }
            commands.push_back(make_command(WorldVisibleObjectKind::world_surface,
                *surface, renderer::RenderMatrix4{}, std::nullopt, std::nullopt));
        }
        std::vector<std::uint32_t> selected_brush(
            visible_brush.begin(), visible_brush.end());
        std::ranges::sort(selected_brush);
        if (std::adjacent_find(selected_brush.begin(), selected_brush.end()) !=
            selected_brush.end()) {
            return fail(WorldVisibleDrawListErrorCode::invalid_input,
                std::nullopt,
                "Visibility set contains a duplicate brush instance");
        }

        for (const auto source_instance : visible_brush) {
            const auto* instance = find_in_lookup(
                brush_instance_lookup, source_instance);
            if (instance == nullptr) {
                return fail(
                    WorldVisibleDrawListErrorCode::visible_brush_instance_not_found,
                    source_instance,
                    "Visible brush instance has no exact draw input");
            }
            if (!renderer::is_finite(instance->model_transform)) {
                return fail(WorldVisibleDrawListErrorCode::non_finite_transform,
                    source_instance,
                    "Brush instance transform is non-finite");
            }
            const auto* model = find_in_lookup(
                brush_model_lookup, instance->source_model_index);
            if (model == nullptr) {
                return fail(WorldVisibleDrawListErrorCode::brush_model_not_found,
                    source_instance,
                    "Visible brush instance references an unavailable render model");
            }
            for (const auto& surface : model->surfaces) {
                if (commands.size() >= limits.maximum_draw_commands) {
                    return fail(WorldVisibleDrawListErrorCode::command_limit_exceeded,
                        source_instance,
                        "Brush draw command exceeds the configured limit");
                }
                commands.push_back(make_command(
                    WorldVisibleObjectKind::brush_instance_surface,
                    surface,
                    instance->model_transform,
                    instance->source_model_index,
                    instance->source_instance_index));
            }
        }

        std::ranges::sort(commands, [](const auto& left, const auto& right) {
            return ordering_key(left) < ordering_key(right);
        });

        WorldVisibleDrawListStatistics statistics;
        statistics.command_count = commands.size();
        for (const auto& command : commands) {
            const auto triangles = static_cast<std::size_t>(
                command.index_count / 3U);
            if (triangles > std::numeric_limits<std::size_t>::max() -
                    statistics.triangle_count) {
                return fail(WorldVisibleDrawListErrorCode::invalid_index_range,
                    command.source_surface_index,
                    "Visible triangle count overflows the host size type");
            }
            statistics.triangle_count += triangles;
            if (command.object_kind == WorldVisibleObjectKind::world_surface) {
                ++statistics.world_command_count;
            } else {
                ++statistics.brush_command_count;
            }
            if (command.alpha_mode ==
                assets::WorldTextureAlphaMode::masked_index_255) {
                ++statistics.masked_command_count;
            } else {
                ++statistics.opaque_command_count;
            }
        }
        return {
            WorldVisibleDrawList{
                std::move(commands),
                statistics,
                input.visibility->revision(),
                input.visibility->scene_identity(),
                input.visibility->result_signature()},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail(WorldVisibleDrawListErrorCode::unable_to_retain_draw_list,
            std::nullopt,
            "Unable to retain bounded visible draw commands");
    } catch (const std::length_error&) {
        return fail(WorldVisibleDrawListErrorCode::unable_to_retain_draw_list,
            std::nullopt,
            "Visible draw-list container length is invalid");
    }
}

} // namespace hlclient::world_visibility
