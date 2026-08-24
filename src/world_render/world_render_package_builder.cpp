#include <hlclient/world_render/world_render_package_builder.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace hlclient::world_render {
namespace {

constexpr float kLightmapCoordinateTolerance = 1.0e-4F;
constexpr std::uint64_t kFnvOffsetBasis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checked_accumulate_bytes(
    const std::size_t count,
    const std::size_t element_size,
    std::size_t& total) noexcept
{
    std::size_t bytes = 0U;
    return checked_multiply(count, element_size, bytes) &&
        checked_add(total, bytes, total);
}

[[nodiscard]] bool finite(const assets::AssetVector2& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite(bounds.minimum) && finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool is_safe_virtual_identity(const std::string_view identity) noexcept
{
    if (identity.empty()) {
        return true;
    }
    if (identity.front() == '/' || identity.find('\\') != std::string_view::npos ||
        identity.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t position = 0U;
    while (position <= identity.size()) {
        const auto separator = identity.find('/', position);
        const auto length = separator == std::string_view::npos
            ? identity.size() - position
            : separator - position;
        const auto component = identity.substr(position, length);
        if (component == "." || component == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        position = separator + 1U;
    }
    return true;
}

[[nodiscard]] bool rectangle_in_page(
    const assets::WorldLightmapRectangle& rectangle,
    const assets::WorldLightmapAtlasPage& page) noexcept
{
    return rectangle.width != 0U && rectangle.height != 0U &&
        rectangle.x <= page.width && rectangle.y <= page.height &&
        rectangle.width <= page.width - rectangle.x &&
        rectangle.height <= page.height - rectangle.y;
}

[[nodiscard]] WorldRenderPackageBuildResult fail(
    const WorldRenderPackageErrorCode code,
    const std::optional<std::size_t> index,
    std::string context)
{
    return {
        std::nullopt,
        WorldRenderPackageError{code, index, std::move(context)},
    };
}

struct BatchKey {
    std::size_t base_texture_asset_index{0U};
    assets::WorldTextureAlphaMode alpha_mode{assets::WorldTextureAlphaMode::opaque};
    WorldRenderLightmapMode lightmap_mode{WorldRenderLightmapMode::unlit_white};
    std::optional<std::size_t> atlas_page_index;
    bool special_surface{false};

    [[nodiscard]] friend bool operator<(const BatchKey& left, const BatchKey& right)
    {
        return std::tie(left.base_texture_asset_index,
                   left.alpha_mode,
                   left.lightmap_mode,
                   left.atlas_page_index,
                   left.special_surface) <
            std::tie(right.base_texture_asset_index,
                right.alpha_mode,
                right.lightmap_mode,
                right.atlas_page_index,
                right.special_surface);
    }
};

struct PendingBatch {
    BatchKey key{};
    std::vector<std::uint32_t> indices;
    struct SurfaceRange {
        std::size_t source_surface_index{0U};
        std::size_t first_pending_index{0U};
        std::size_t index_count{0U};
        assets::WorldBounds bounds{};
    };
    std::vector<SurfaceRange> surface_ranges;
    std::size_t source_surface_count{0U};
};

class StableHasher final {
public:
    void add_byte(const std::uint8_t value) noexcept
    {
        value_ ^= value;
        value_ *= kFnvPrime;
    }

    void add_integer(const bool value) noexcept
    {
        add_byte(value ? 1U : 0U);
    }

    template <typename Integer>
    void add_integer(const Integer value) noexcept
    {
        using Unsigned = std::make_unsigned_t<Integer>;
        auto remaining = static_cast<Unsigned>(value);
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            add_byte(static_cast<std::uint8_t>(remaining & static_cast<Unsigned>(0xFFU)));
            remaining >>= 8U;
        }
    }

    void add_float(const float value) noexcept
    {
        add_integer(std::bit_cast<std::uint32_t>(value));
    }

    void add_string(const std::string_view value) noexcept
    {
        add_integer(value.size());
        for (const auto character : value) {
            add_byte(static_cast<std::uint8_t>(character));
        }
    }

    void add_bytes(const std::span<const std::byte> values) noexcept
    {
        add_integer(values.size());
        for (const auto value : values) {
            add_byte(std::to_integer<std::uint8_t>(value));
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? kFnvOffsetBasis : value_;
    }

private:
    std::uint64_t value_{kFnvOffsetBasis};
};

void hash_vector(StableHasher& hash, const assets::AssetVector2& value) noexcept
{
    hash.add_float(value.x);
    hash.add_float(value.y);
}

void hash_vector(StableHasher& hash, const assets::AssetVector3& value) noexcept
{
    hash.add_float(value.x);
    hash.add_float(value.y);
    hash.add_float(value.z);
}

[[nodiscard]] WorldRendererResourceIdentity make_resource_identity(
    const assets::TexturedWorldAsset& textured_world,
    const assets::WorldLightmapSet& lightmaps,
    const std::span<const WorldRenderVertex> vertices,
    const std::span<const std::uint32_t> indices,
    const std::span<const WorldRenderMaterial> materials,
    const std::span<const WorldDrawBatch> batches) noexcept
{
    StableHasher identity_hash;
    identity_hash.add_integer(textured_world.world.coordinate_space);
    identity_hash.add_integer(textured_world.world.texture_coordinate_space);
    hash_vector(identity_hash, textured_world.world.bounds.minimum);
    hash_vector(identity_hash, textured_world.world.bounds.maximum);
    identity_hash.add_integer(vertices.size());
    for (const auto& vertex : vertices) {
        hash_vector(identity_hash, vertex.position);
        hash_vector(identity_hash, vertex.normal);
    }
    identity_hash.add_integer(indices.size());
    for (const auto index : indices) {
        identity_hash.add_integer(index);
    }

    StableHasher revision_hash;
    revision_hash.add_integer(identity_hash.value());
    for (const auto& vertex : vertices) {
        hash_vector(revision_hash, vertex.base_texture_coordinate);
        hash_vector(revision_hash, vertex.lightmap_atlas_coordinate);
    }
    for (const auto& material : materials) {
        revision_hash.add_integer(material.material_index);
        revision_hash.add_integer(material.base_texture_asset_index);
        revision_hash.add_integer(material.base_texture_alpha_mode);
        revision_hash.add_integer(material.lightmap_mode);
        revision_hash.add_integer(material.lightmap_atlas_page_index.has_value());
        revision_hash.add_integer(material.lightmap_atlas_page_index.value_or(0U));
        revision_hash.add_integer(material.special_surface);
    }
    for (const auto& batch : batches) {
        revision_hash.add_integer(batch.first_index);
        revision_hash.add_integer(batch.index_count);
        revision_hash.add_integer(batch.render_material_index);
        revision_hash.add_integer(batch.source_surface_count);
    }
    for (const auto& texture : textured_world.textures.textures()) {
        revision_hash.add_string(texture.name);
        revision_hash.add_integer(texture.width);
        revision_hash.add_integer(texture.height);
        revision_hash.add_integer(texture.alpha_mode);
        for (const auto& mip : texture.mip_levels) {
            revision_hash.add_integer(mip.width);
            revision_hash.add_integer(mip.height);
            revision_hash.add_bytes(mip.rgba_pixels);
        }
    }
    for (const auto& page : lightmaps.pages()) {
        revision_hash.add_integer(page.width);
        revision_hash.add_integer(page.height);
        for (const auto& image : page.style_slot_images) {
            revision_hash.add_bytes(image.rgba_pixels);
        }
    }
    return {identity_hash.value(), revision_hash.value()};
}

} // namespace

std::string_view to_string(const WorldRenderPackageErrorCode code) noexcept
{
    switch (code) {
    case WorldRenderPackageErrorCode::invalid_prerequisite:
        return "invalid_prerequisite";
    case WorldRenderPackageErrorCode::texture_set_incomplete:
        return "texture_set_incomplete";
    case WorldRenderPackageErrorCode::lightmap_binding_mismatch:
        return "lightmap_binding_mismatch";
    case WorldRenderPackageErrorCode::invalid_surface_range:
        return "invalid_surface_range";
    case WorldRenderPackageErrorCode::invalid_material_binding:
        return "invalid_material_binding";
    case WorldRenderPackageErrorCode::invalid_texture_dimensions:
        return "invalid_texture_dimensions";
    case WorldRenderPackageErrorCode::invalid_render_coordinate:
        return "invalid_render_coordinate";
    case WorldRenderPackageErrorCode::invalid_atlas_binding:
        return "invalid_atlas_binding";
    case WorldRenderPackageErrorCode::batch_limit_exceeded:
        return "batch_limit_exceeded";
    case WorldRenderPackageErrorCode::output_limit_exceeded:
        return "output_limit_exceeded";
    case WorldRenderPackageErrorCode::unable_to_retain_package:
        return "unable_to_retain_package";
    }
    return "unknown";
}

WorldRenderPackageBuildResult WorldRenderPackageBuilder::build(
    assets::TexturedWorldAsset textured_world,
    assets::WorldLightmapSet lightmaps,
    const WorldRenderPackageLimits& limits) const
{
    try {
        const auto& world = textured_world.world;
        const auto& textures = textured_world.textures;
        if (limits.maximum_vertices == 0U || limits.maximum_indices == 0U ||
            limits.maximum_materials == 0U || limits.maximum_batches == 0U ||
            limits.maximum_base_texture_bytes == 0U ||
            limits.maximum_lightmap_bytes == 0U ||
            limits.maximum_total_cpu_render_bytes == 0U) {
            return fail(WorldRenderPackageErrorCode::invalid_prerequisite,
                std::nullopt,
                "World render-package limits must all be positive");
        }
        if (world.vertices.empty() || world.indices.empty() || world.surfaces.empty() ||
            world.materials.empty() || world.indices.size() % 3U != 0U ||
            !valid_bounds(world.bounds) ||
            !is_safe_virtual_identity(world.identity.source_name)) {
            return fail(WorldRenderPackageErrorCode::invalid_prerequisite,
                std::nullopt,
                "World geometry metadata is empty, malformed, or not renderer-neutral");
        }
        if (world.coordinate_space !=
                assets::WorldCoordinateSpace::source_native_goldsrc_z_up ||
            world.texture_coordinate_space !=
                assets::WorldTextureCoordinateSpace::texel_units) {
            return fail(WorldRenderPackageErrorCode::invalid_prerequisite,
                std::nullopt,
                "World coordinate metadata is unsupported");
        }
        if (world.vertices.size() > limits.maximum_vertices ||
            world.indices.size() > limits.maximum_indices ||
            world.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
            world.materials.size() > limits.maximum_materials) {
            return fail(WorldRenderPackageErrorCode::output_limit_exceeded,
                std::nullopt,
                "World geometry exceeds configured render-package limits");
        }
        if (!textures.complete_for_world_materials()) {
            return fail(WorldRenderPackageErrorCode::texture_set_incomplete,
                std::nullopt,
                "World texture set is incomplete");
        }
        if (textures.binding_count() != world.materials.size()) {
            return fail(WorldRenderPackageErrorCode::invalid_material_binding,
                textures.binding_count(),
                "Texture binding count does not equal world material count");
        }
        if (lightmaps.binding_count() != world.surfaces.size() ||
            !lightmaps.complete_for_world_surfaces()) {
            return fail(WorldRenderPackageErrorCode::lightmap_binding_mismatch,
                lightmaps.binding_count(),
                "Lightmap bindings do not form a complete world-surface mapping");
        }

        std::size_t base_texture_bytes = 0U;
        for (std::size_t texture_index = 0U;
             texture_index < textures.textures().size(); ++texture_index) {
            const auto& texture = textures.textures()[texture_index];
            if (texture.width == 0U || texture.height == 0U) {
                return fail(WorldRenderPackageErrorCode::invalid_texture_dimensions,
                    texture_index,
                    "Base texture dimensions must be positive");
            }
            for (const auto& mip : texture.mip_levels) {
                if (!checked_add(
                        base_texture_bytes, mip.rgba_pixels.size(), base_texture_bytes) ||
                    base_texture_bytes > limits.maximum_base_texture_bytes) {
                    return fail(WorldRenderPackageErrorCode::output_limit_exceeded,
                        texture_index,
                        "Base texture RGBA storage exceeds the configured limit");
                }
            }
        }
        std::size_t lightmap_bytes = 0U;
        for (std::size_t page_index = 0U; page_index < lightmaps.pages().size();
             ++page_index) {
            for (const auto& image : lightmaps.pages()[page_index].style_slot_images) {
                if (!checked_add(lightmap_bytes,
                        image.rgba_pixels.size(),
                        lightmap_bytes) ||
                    lightmap_bytes > limits.maximum_lightmap_bytes) {
                    return fail(WorldRenderPackageErrorCode::output_limit_exceeded,
                        page_index,
                        "Lightmap RGBA storage exceeds the configured limit");
                }
            }
        }

        std::vector<WorldRenderVertex> render_vertices(world.vertices.size());
        std::vector<bool> vertex_covered(world.vertices.size(), false);
        std::vector<bool> source_index_covered(world.indices.size(), false);
        std::vector<bool> material_covered(world.materials.size(), false);
        std::vector<PendingBatch> pending_batches;
        std::map<BatchKey, std::size_t> batch_lookup;

        for (std::size_t surface_index = 0U; surface_index < world.surfaces.size();
             ++surface_index) {
            const auto& surface = world.surfaces[surface_index];
            const auto first_vertex = static_cast<std::size_t>(surface.first_vertex);
            const auto vertex_count = static_cast<std::size_t>(surface.vertex_count);
            const auto first_index = static_cast<std::size_t>(surface.first_index);
            const auto index_count = static_cast<std::size_t>(surface.index_count);
            std::size_t vertex_end = 0U;
            std::size_t index_end = 0U;
            if (vertex_count < 3U || index_count == 0U || index_count % 3U != 0U ||
                !checked_add(first_vertex, vertex_count, vertex_end) ||
                vertex_end > world.vertices.size() ||
                !checked_add(first_index, index_count, index_end) ||
                index_end > world.indices.size() || !valid_bounds(surface.bounds)) {
                return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                    surface_index,
                    "Surface vertex, index, or bounds metadata is invalid");
            }
            if (surface.material_index >= world.materials.size()) {
                return fail(WorldRenderPackageErrorCode::invalid_material_binding,
                    surface_index,
                    "Surface material index is out of range");
            }
            material_covered[surface.material_index] = true;
            const auto* texture_binding =
                textures.binding_for_material(surface.material_index);
            if (texture_binding == nullptr || !assets::is_resolved(texture_binding->status) ||
                !texture_binding->texture_asset_index ||
                *texture_binding->texture_asset_index >= textures.textures().size()) {
                return fail(WorldRenderPackageErrorCode::invalid_material_binding,
                    surface_index,
                    "Surface does not have an exact resolved base texture binding");
            }
            const auto& source_material =
                world.materials[surface.material_index];
            const auto& texture =
                textures.textures()[*texture_binding->texture_asset_index];
            const bool embedded = source_material.texture_storage ==
                    assets::WorldTextureStorage::embedded &&
                texture_binding->status == assets::
                    WorldMaterialTextureBindingStatus::resolved_embedded &&
                texture.source_kind == assets::WorldTextureSourceKind::embedded_bsp;
            const bool external = source_material.texture_storage ==
                    assets::WorldTextureStorage::external_reference &&
                texture_binding->status == assets::
                    WorldMaterialTextureBindingStatus::resolved_wad3 &&
                texture.source_kind == assets::WorldTextureSourceKind::external_wad3;
            const bool archive_identity_valid =
                (embedded && !texture_binding->source_archive_ordinal &&
                    !texture.source_archive_ordinal) ||
                (external && texture_binding->source_archive_ordinal &&
                    texture.source_archive_ordinal ==
                        texture_binding->source_archive_ordinal);
            if ((!embedded && !external) || !archive_identity_valid ||
                !source_material.texture_name ||
                source_material.texture_name->empty() ||
                source_material.width != std::optional{texture.width} ||
                source_material.height != std::optional{texture.height} ||
                !source_material.source_texture_index ||
                texture_binding->source_bsp_texture_index !=
                    source_material.source_texture_index ||
                source_material.compatibility_profile != assets::
                    WorldMaterialCompatibilityProfile::source_texture_reference_v1 ||
                source_material.evidence_profile != assets::
                    WorldMaterialEvidenceProfile::validated_source_metadata ||
                texture_binding->compatibility_profile != assets::
                    WorldTextureCompatibilityProfile::goldsrc_indexed_miptex_v1 ||
                texture_binding->evidence_profile != assets::
                    WorldTextureEvidenceProfile::
                        valve_public_tools_and_synthetic_fixtures) {
                return fail(WorldRenderPackageErrorCode::invalid_material_binding,
                    surface_index,
                    "World material and resolved texture metadata do not match exactly");
            }
            if (texture.width == 0U || texture.height == 0U) {
                return fail(WorldRenderPackageErrorCode::invalid_texture_dimensions,
                    surface_index,
                    "Surface base texture dimensions are zero");
            }

            const auto* lightmap_binding = lightmaps.binding_for_surface(surface_index);
            if (lightmap_binding == nullptr ||
                !assets::is_renderable(lightmap_binding->status)) {
                return fail(WorldRenderPackageErrorCode::lightmap_binding_mismatch,
                    surface_index,
                    "Surface does not have an exact renderable lightmap binding");
            }
            WorldRenderLightmapMode lightmap_mode =
                WorldRenderLightmapMode::unlit_white;
            std::optional<std::size_t> atlas_page_index;
            const assets::WorldLightmapAtlasPage* atlas_page = nullptr;
            if (lightmap_binding->status ==
                assets::WorldSurfaceLightmapBindingStatus::resolved) {
                lightmap_mode = WorldRenderLightmapMode::atlas;
                atlas_page_index = lightmap_binding->atlas_page_index;
                if (!atlas_page_index || *atlas_page_index >= lightmaps.pages().size() ||
                    lightmap_binding->sample_width == 0U ||
                    lightmap_binding->sample_height == 0U) {
                    return fail(WorldRenderPackageErrorCode::invalid_atlas_binding,
                        surface_index,
                        "Resolved surface lightmap atlas reference is invalid");
                }
                atlas_page = &lightmaps.pages()[*atlas_page_index];
                if (!rectangle_in_page(lightmap_binding->inner_rectangle, *atlas_page) ||
                    lightmap_binding->inner_rectangle.width !=
                        lightmap_binding->sample_width ||
                    lightmap_binding->inner_rectangle.height !=
                        lightmap_binding->sample_height) {
                    return fail(WorldRenderPackageErrorCode::invalid_atlas_binding,
                        surface_index,
                        "Resolved surface lightmap rectangle is invalid");
                }
            } else if (lightmap_binding->atlas_page_index) {
                return fail(WorldRenderPackageErrorCode::invalid_atlas_binding,
                    surface_index,
                    "Unlit surface unexpectedly references an atlas page");
            }

            for (std::size_t vertex_index = first_vertex; vertex_index < vertex_end;
                 ++vertex_index) {
                if (vertex_covered[vertex_index]) {
                    return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                        surface_index,
                        "Face-local surface vertex ranges overlap");
                }
                const auto& source_vertex = world.vertices[vertex_index];
                if (!finite(source_vertex.position) || !finite(source_vertex.normal) ||
                    !finite(source_vertex.texture_coordinate)) {
                    return fail(WorldRenderPackageErrorCode::invalid_render_coordinate,
                        vertex_index,
                        "Source world vertex contains a non-finite coordinate");
                }
                WorldRenderVertex render_vertex{
                    source_vertex.position,
                    source_vertex.normal,
                    {source_vertex.texture_coordinate.x /
                            static_cast<float>(texture.width),
                        source_vertex.texture_coordinate.y /
                            static_cast<float>(texture.height)},
                    {0.5F, 0.5F},
                };
                if (lightmap_mode == WorldRenderLightmapMode::atlas) {
                    const auto local_s =
                        (source_vertex.texture_coordinate.x -
                            static_cast<float>(lightmap_binding->texture_min_s)) /
                        16.0F;
                    const auto local_t =
                        (source_vertex.texture_coordinate.y -
                            static_cast<float>(lightmap_binding->texture_min_t)) /
                        16.0F;
                    const auto maximum_s =
                        static_cast<float>(lightmap_binding->sample_width - 1U);
                    const auto maximum_t =
                        static_cast<float>(lightmap_binding->sample_height - 1U);
                    if (!std::isfinite(local_s) || !std::isfinite(local_t) ||
                        local_s < -kLightmapCoordinateTolerance ||
                        local_t < -kLightmapCoordinateTolerance ||
                        local_s > maximum_s + kLightmapCoordinateTolerance ||
                        local_t > maximum_t + kLightmapCoordinateTolerance) {
                        return fail(WorldRenderPackageErrorCode::invalid_render_coordinate,
                            vertex_index,
                            "Source vertex lies outside its calculated lightmap extent");
                    }
                    render_vertex.lightmap_atlas_coordinate = {
                        (static_cast<float>(lightmap_binding->inner_rectangle.x) +
                            local_s + 0.5F) /
                            static_cast<float>(atlas_page->width),
                        (static_cast<float>(lightmap_binding->inner_rectangle.y) +
                            local_t + 0.5F) /
                            static_cast<float>(atlas_page->height),
                    };
                }
                if (!finite(render_vertex.base_texture_coordinate) ||
                    !finite(render_vertex.lightmap_atlas_coordinate) ||
                    render_vertex.lightmap_atlas_coordinate.x < 0.0F ||
                    render_vertex.lightmap_atlas_coordinate.x > 1.0F ||
                    render_vertex.lightmap_atlas_coordinate.y < 0.0F ||
                    render_vertex.lightmap_atlas_coordinate.y > 1.0F) {
                    return fail(WorldRenderPackageErrorCode::invalid_render_coordinate,
                        vertex_index,
                        "Normalized render texture coordinates are invalid");
                }
                render_vertices[vertex_index] = render_vertex;
                vertex_covered[vertex_index] = true;
            }

            const BatchKey key{
                *texture_binding->texture_asset_index,
                texture.alpha_mode,
                lightmap_mode,
                atlas_page_index,
                surface.special_surface,
            };
            auto found = batch_lookup.find(key);
            if (found == batch_lookup.end()) {
                if (pending_batches.size() >= limits.maximum_batches ||
                    pending_batches.size() >= limits.maximum_materials) {
                    return fail(WorldRenderPackageErrorCode::batch_limit_exceeded,
                        surface_index,
                        "Deterministic material batch count exceeds configured limits");
                }
                const auto batch_index = pending_batches.size();
                pending_batches.push_back(PendingBatch{key, {}, {}, 0U});
                found = batch_lookup.emplace(key, batch_index).first;
            }
            auto& pending = pending_batches[found->second];
            ++pending.source_surface_count;
            pending.surface_ranges.push_back(PendingBatch::SurfaceRange{
                surface_index,
                pending.indices.size(),
                index_count,
                surface.bounds,
            });
            std::size_t pending_index_count = 0U;
            if (!checked_add(
                    pending.indices.size(), index_count, pending_index_count) ||
                pending_index_count > world.indices.size()) {
                return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                    surface_index,
                    "Surface batch index accumulation exceeds source geometry");
            }
            pending.indices.reserve(pending_index_count);
            for (std::size_t source_index = first_index; source_index < index_end;
                 ++source_index) {
                if (source_index_covered[source_index]) {
                    return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                        surface_index,
                        "Surface source index ranges overlap");
                }
                const auto vertex_index = world.indices[source_index];
                if (vertex_index < first_vertex || vertex_index >= vertex_end) {
                    return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                        surface_index,
                        "Surface index references a vertex outside its face-local range");
                }
                pending.indices.push_back(vertex_index);
                source_index_covered[source_index] = true;
            }
        }

        const auto unused_material =
            std::ranges::find(material_covered, false);
        if (unused_material != material_covered.end()) {
            return fail(WorldRenderPackageErrorCode::invalid_material_binding,
                static_cast<std::size_t>(
                    std::distance(material_covered.begin(), unused_material)),
                "Every retained world material must be referenced by a surface");
        }

        if (!std::ranges::all_of(vertex_covered, [](const bool covered) {
                return covered;
            }) ||
            !std::ranges::all_of(source_index_covered, [](const bool covered) {
                return covered;
            })) {
            return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                std::nullopt,
                "World surface ranges do not cover source geometry exactly once");
        }

        std::vector<std::uint32_t> render_indices;
        render_indices.reserve(world.indices.size());
        std::vector<WorldRenderMaterial> render_materials;
        render_materials.reserve(pending_batches.size());
        std::vector<WorldDrawBatch> draw_batches;
        draw_batches.reserve(pending_batches.size());
        std::vector<WorldRenderSurfaceRange> surface_ranges(
            world.surfaces.size());
        std::vector<bool> surface_range_covered(world.surfaces.size(), false);
        WorldRenderStatistics statistics;
        statistics.vertex_count = render_vertices.size();
        statistics.index_count = world.indices.size();
        statistics.triangle_count = world.indices.size() / 3U;
        statistics.source_surface_count = world.surfaces.size();
        statistics.base_texture_rgba_byte_count = base_texture_bytes;
        statistics.lightmap_rgba_byte_count = lightmap_bytes;

        for (auto& pending : pending_batches) {
            if (render_indices.size() > std::numeric_limits<std::uint32_t>::max() ||
                pending.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
                pending.indices.size() % 3U != 0U) {
                return fail(WorldRenderPackageErrorCode::output_limit_exceeded,
                    draw_batches.size(),
                    "Render batch index range cannot be represented exactly");
            }
            const auto material_index = render_materials.size();
            const auto batch_index = draw_batches.size();
            const auto batch_first_index = render_indices.size();
            render_materials.push_back(WorldRenderMaterial{
                material_index,
                pending.key.base_texture_asset_index,
                pending.key.alpha_mode,
                pending.key.lightmap_mode,
                pending.key.atlas_page_index,
                pending.key.special_surface,
                WorldRenderCompatibilityProfile::goldsrc_static_world_v1,
                WorldRenderEvidenceProfile::
                    validated_geometry_texture_and_lightmap_bindings,
            });
            draw_batches.push_back(WorldDrawBatch{
                static_cast<std::uint32_t>(render_indices.size()),
                static_cast<std::uint32_t>(pending.indices.size()),
                material_index,
                pending.key.alpha_mode,
                pending.key.lightmap_mode,
                pending.key.atlas_page_index,
                pending.source_surface_count,
            });
            for (const auto& pending_range : pending.surface_ranges) {
                if (pending_range.source_surface_index >= surface_ranges.size() ||
                    surface_range_covered[pending_range.source_surface_index] ||
                    pending_range.first_pending_index >
                        std::numeric_limits<std::uint32_t>::max() ||
                    batch_first_index >
                        std::numeric_limits<std::uint32_t>::max() -
                            pending_range.first_pending_index ||
                    pending_range.index_count >
                        std::numeric_limits<std::uint32_t>::max()) {
                    return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                        pending_range.source_surface_index,
                        "Per-surface render range cannot be represented exactly");
                }
                surface_ranges[pending_range.source_surface_index] =
                    WorldRenderSurfaceRange{
                        pending_range.source_surface_index,
                        static_cast<std::uint32_t>(
                            batch_first_index + pending_range.first_pending_index),
                        static_cast<std::uint32_t>(pending_range.index_count),
                        material_index,
                        pending_range.bounds,
                        batch_index,
                        pending.key.alpha_mode,
                        pending.key.lightmap_mode,
                        pending.key.atlas_page_index,
                    };
                surface_range_covered[pending_range.source_surface_index] = true;
            }
            render_indices.insert(render_indices.end(),
                pending.indices.begin(),
                pending.indices.end());
            if (pending.key.alpha_mode ==
                assets::WorldTextureAlphaMode::masked_index_255) {
                ++statistics.masked_batch_count;
            } else {
                ++statistics.opaque_batch_count;
            }
            if (pending.key.lightmap_mode == WorldRenderLightmapMode::atlas) {
                ++statistics.atlas_batch_count;
            } else {
                ++statistics.unlit_batch_count;
            }
        }
        statistics.material_count = render_materials.size();
        statistics.batch_count = draw_batches.size();
        if (render_indices.size() != world.indices.size()) {
            return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                std::nullopt,
                "Render batches do not retain every source triangle exactly once");
        }
        if (!std::ranges::all_of(surface_range_covered, [](const bool covered) {
                return covered;
            })) {
            return fail(WorldRenderPackageErrorCode::invalid_surface_range,
                std::nullopt,
                "Per-surface render ranges do not cover every source surface");
        }

        std::size_t output_geometry_bytes = 0U;
        if (!checked_accumulate_bytes(render_vertices.size(),
                sizeof(WorldRenderVertex), output_geometry_bytes) ||
            !checked_accumulate_bytes(render_indices.size(),
                sizeof(std::uint32_t), output_geometry_bytes) ||
            !checked_accumulate_bytes(render_materials.size(),
                sizeof(WorldRenderMaterial), output_geometry_bytes) ||
            !checked_accumulate_bytes(draw_batches.size(),
                sizeof(WorldDrawBatch), output_geometry_bytes) ||
            !checked_accumulate_bytes(surface_ranges.size(),
                sizeof(WorldRenderSurfaceRange), output_geometry_bytes)) {
            return fail(WorldRenderPackageErrorCode::output_limit_exceeded,
                std::nullopt,
                "Render output byte count overflows the host size type");
        }
        statistics.output_geometry_byte_count = output_geometry_bytes;

        std::size_t total_cpu_bytes = output_geometry_bytes;
        if (!checked_add(total_cpu_bytes, base_texture_bytes, total_cpu_bytes) ||
            !checked_add(total_cpu_bytes, lightmap_bytes, total_cpu_bytes) ||
            !checked_accumulate_bytes(world.vertices.size(),
                sizeof(assets::WorldVertex), total_cpu_bytes) ||
            !checked_accumulate_bytes(world.indices.size(),
                sizeof(std::uint32_t), total_cpu_bytes) ||
            !checked_accumulate_bytes(world.surfaces.size(),
                sizeof(assets::WorldSurface), total_cpu_bytes) ||
            !checked_accumulate_bytes(world.materials.size(),
                sizeof(assets::WorldMaterialReference), total_cpu_bytes) ||
            total_cpu_bytes > limits.maximum_total_cpu_render_bytes) {
            return fail(WorldRenderPackageErrorCode::output_limit_exceeded,
                std::nullopt,
                "Aggregate retained CPU render storage exceeds the configured limit");
        }
        statistics.total_cpu_render_byte_count = total_cpu_bytes;
        const auto retained_bounds = world.bounds;
        const auto resource_identity = make_resource_identity(textured_world,
            lightmaps,
            render_vertices,
            render_indices,
            render_materials,
            draw_batches);

        return {
            WorldRenderPackage{
                std::move(textured_world),
                std::move(lightmaps),
                std::move(render_vertices),
                std::move(render_indices),
                std::move(render_materials),
                std::move(draw_batches),
                std::move(surface_ranges),
                retained_bounds,
                WorldRenderCoordinateMetadata{},
                statistics,
                resource_identity,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail(WorldRenderPackageErrorCode::unable_to_retain_package,
            std::nullopt,
            "Unable to retain the immutable world render package");
    } catch (const std::length_error&) {
        return fail(WorldRenderPackageErrorCode::unable_to_retain_package,
            std::nullopt,
            "World render package exceeds an owning container limit");
    }
}

} // namespace hlclient::world_render
