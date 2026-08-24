#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>

#include <hlclient/goldsrc/bsp/goldsrc_bsp_texture_source_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_worldspawn_wad_references.hpp>
#include <hlclient/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_texture.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace hlclient::goldsrc {
namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace indexed = hlclient::goldsrc::indexed_texture;
namespace wad3 = hlclient::goldsrc::wad3;

inline constexpr std::size_t kHardMaximumWorldTextureMaterials = 8'192U;
inline constexpr std::size_t kHardMaximumWorldTextureAssets = 512U;
inline constexpr std::size_t kHardMaximumWorldTextureWadReferences = 128U;
inline constexpr std::size_t kHardMaximumWorldTextureWadLumps = 65'536U;
inline constexpr std::uint32_t kHardMaximumWorldTextureDimension = 16'384U;
inline constexpr std::uint64_t kHardMaximumWorldTextureTexels =
    268'435'456ULL;
inline constexpr std::size_t kHardMaximumWorldTextureMaterialsPerUpdate =
    8'192U;
inline constexpr std::size_t
    kHardMaximumWorldTexturePixelConversionBytesPerUpdate =
        64U * 1024U * 1024U;

[[nodiscard]] bool terminal_state(const WorldTextureImportState state) noexcept
{
    return state == WorldTextureImportState::textures_ready ||
        state == WorldTextureImportState::textures_incomplete ||
        state == WorldTextureImportState::cancelled ||
        state == WorldTextureImportState::timed_out ||
        state == WorldTextureImportState::failed;
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] indexed::GoldSrcIndexedTextureLimits indexed_limits(
    const GoldSrcWorldTextureImportLimits& limits) noexcept
{
    return indexed::GoldSrcIndexedTextureLimits{
        limits.maximum_texture_dimension,
        limits.maximum_texture_texels,
        limits.maximum_decoded_bytes_per_texture,
        3U,
    };
}

[[nodiscard]] bsp::GoldSrcBspTextureSourceLimits bsp_source_limits(
    const GoldSrcWorldTextureImportLimits& limits) noexcept
{
    return bsp::GoldSrcBspTextureSourceLimits{
        32U * 1024U * 1024U,
        limits.maximum_material_count,
        limits.maximum_texture_asset_count,
        indexed_limits(limits),
    };
}

[[nodiscard]] bsp::GoldSrcWorldspawnParseLimits worldspawn_limits(
    const GoldSrcWorldTextureImportLimits& limits) noexcept
{
    return bsp::GoldSrcWorldspawnParseLimits{
        128U * 1024U,
        limits.maximum_worldspawn_pairs,
        256U,
        limits.maximum_worldspawn_value_bytes,
        limits.maximum_wad_reference_count,
        128U,
    };
}

[[nodiscard]] wad3::GoldSrcWad3CatalogLimits wad_catalog_limits(
    const GoldSrcWorldTextureImportLimits& limits) noexcept
{
    return wad3::GoldSrcWad3CatalogLimits{
        static_cast<std::size_t>(limits.maximum_wad_source_bytes),
        limits.maximum_wad_lump_count,
    };
}

[[nodiscard]] assets::WorldTextureSetLimits texture_set_limits(
    const GoldSrcWorldTextureImportLimits& limits) noexcept
{
    return assets::WorldTextureSetLimits{
        limits.maximum_texture_asset_count,
        limits.maximum_material_count,
        limits.maximum_wad_reference_count,
        limits.maximum_total_decoded_rgba_bytes,
    };
}

struct EmbeddedTextureBinding {
    std::uint32_t canonical_source_texture_index{0U};
    std::size_t texture_asset_index{0U};
};

struct ExternalTextureBinding {
    std::uint32_t archive_ordinal{0U};
    std::size_t directory_ordinal{0U};
    std::size_t texture_asset_index{0U};
};

} // namespace

bool valid_goldsrc_world_texture_import_limits(
    const GoldSrcWorldTextureImportLimits& limits) noexcept
{
    if (limits.maximum_material_count == 0U ||
        limits.maximum_material_count > kHardMaximumWorldTextureMaterials ||
        limits.maximum_texture_asset_count == 0U ||
        limits.maximum_texture_asset_count > kHardMaximumWorldTextureAssets ||
        limits.maximum_wad_reference_count == 0U ||
        limits.maximum_wad_reference_count >
            kHardMaximumWorldTextureWadReferences ||
        limits.maximum_wad_source_bytes == 0U ||
        limits.maximum_wad_source_bytes >
            local_assets::kHardMaximumLocalAssetSourceBytes ||
        limits.maximum_wad_lump_count == 0U ||
        limits.maximum_wad_lump_count > kHardMaximumWorldTextureWadLumps ||
        limits.maximum_texture_dimension == 0U ||
        limits.maximum_texture_dimension > kHardMaximumWorldTextureDimension ||
        limits.maximum_texture_texels == 0U ||
        limits.maximum_texture_texels > kHardMaximumWorldTextureTexels ||
        limits.maximum_decoded_bytes_per_texture == 0U ||
        limits.maximum_decoded_bytes_per_texture >
            kDefaultMaximumDecodedBytesPerWorldTexture ||
        limits.maximum_total_decoded_rgba_bytes == 0U ||
        limits.maximum_total_decoded_rgba_bytes >
            kDefaultMaximumTotalWorldTextureRgbaBytes ||
        limits.maximum_worldspawn_pairs == 0U ||
        limits.maximum_worldspawn_pairs > kDefaultMaximumWorldspawnPairs ||
        limits.maximum_worldspawn_value_bytes == 0U ||
        limits.maximum_worldspawn_value_bytes >
            kDefaultMaximumWorldspawnValueBytes ||
        limits.maximum_materials_per_update == 0U ||
        limits.maximum_materials_per_update >
            kHardMaximumWorldTextureMaterialsPerUpdate ||
        limits.maximum_pixel_conversion_bytes_per_update < 4U ||
        limits.maximum_pixel_conversion_bytes_per_update >
            kHardMaximumWorldTexturePixelConversionBytesPerUpdate ||
        !local_assets::valid_local_asset_source_open_limits(
            limits.wad_source_open) ||
        limits.wad_source_open.maximum_open_sources != 1U ||
        limits.wad_source_open.maximum_source_bytes >
            limits.maximum_wad_source_bytes ||
        (limits.timeout &&
            (*limits.timeout <= std::chrono::milliseconds::zero() ||
             *limits.timeout > kHardMaximumWorldTextureTimeout))) {
        return false;
    }
    return indexed::valid_goldsrc_indexed_texture_limits(
               indexed_limits(limits)) &&
        bsp::valid_goldsrc_worldspawn_parse_limits(worldspawn_limits(limits)) &&
        wad3::valid_goldsrc_wad3_catalog_limits(wad_catalog_limits(limits));
}

class WorldTextureImportOperation::Implementation final {
public:
    Implementation(
        std::vector<assets::WorldMaterialReference> materials,
        const std::span<const std::byte> bsp_source,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        GoldSrcWorldTextureImportLimits limits) noexcept
        : materials_{std::move(materials)},
          bsp_source_{bsp_source},
          environment_{std::move(environment)},
          limits_{std::move(limits)},
          state_{WorldTextureImportState::parsing_bsp_texture_sources}
    {
    }

    void update(const WorldTextureImportTimePoint now) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (!started_at_) {
            started_at_ = now;
            last_update_ = now;
        } else if (last_update_ && now < *last_update_) {
            fail(WorldTextureImportErrorCode::time_moved_backwards,
                "Texture import time moved backwards");
            return;
        } else {
            last_update_ = now;
        }
        if (limits_.timeout && now - *started_at_ >= *limits_.timeout) {
            if (wad_open_operation_) {
                wad_open_operation_->cancel();
            }
            fail(WorldTextureImportErrorCode::timed_out,
                "Texture import exceeded its caller-provided deadline",
                WorldTextureImportState::timed_out);
            return;
        }

        try {
            switch (state_) {
            case WorldTextureImportState::parsing_bsp_texture_sources:
                parse_bsp_texture_sources();
                return;
            case WorldTextureImportState::decoding_embedded_textures:
                update_embedded_textures();
                return;
            case WorldTextureImportState::parsing_worldspawn:
                parse_worldspawn();
                return;
            case WorldTextureImportState::resolving_wad_references:
                resolve_next_wad(now);
                return;
            case WorldTextureImportState::opening_wad:
                update_wad_open(now);
                return;
            case WorldTextureImportState::parsing_wad_catalog:
                parse_wad_catalog();
                return;
            case WorldTextureImportState::resolving_external_textures:
                update_external_textures();
                return;
            case WorldTextureImportState::building_texture_set:
                build_texture_set();
                return;
            case WorldTextureImportState::idle:
            case WorldTextureImportState::textures_ready:
            case WorldTextureImportState::textures_incomplete:
            case WorldTextureImportState::cancelled:
            case WorldTextureImportState::timed_out:
            case WorldTextureImportState::failed:
                return;
            }
        } catch (const std::bad_alloc&) {
            fail(WorldTextureImportErrorCode::unable_to_retain_state,
                "Unable to retain bounded texture import state");
        } catch (const std::length_error&) {
            fail(WorldTextureImportErrorCode::unable_to_retain_state,
                "Texture import state exceeds an owning container limit");
        } catch (...) {
            fail(WorldTextureImportErrorCode::unable_to_retain_state,
                "Unexpected failure while advancing local texture import");
        }
    }

    void cancel() noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (wad_open_operation_) {
            wad_open_operation_->cancel();
        }
        clear_active_wad();
        decode_operation_.reset();
        result_.reset();
        fail(WorldTextureImportErrorCode::cancelled,
            "Texture import was cancelled",
            WorldTextureImportState::cancelled);
    }

private:
    void parse_bsp_texture_sources()
    {
        auto parsed = bsp::GoldSrcBspTextureSourceParser::parse(
            bsp_source_, materials_, bsp_source_limits(limits_));
        if (!parsed) {
            fail(WorldTextureImportErrorCode::bsp_texture_source_parse_failed,
                "Approved BSP texture-source metadata is malformed");
            return;
        }
        source_document_.emplace(std::move(*parsed.document));
        bindings_.resize(materials_.size());
        state_ = WorldTextureImportState::decoding_embedded_textures;
    }

    void update_embedded_textures()
    {
        if (!source_document_) {
            fail(WorldTextureImportErrorCode::bsp_texture_source_parse_failed,
                "BSP texture-source document is unavailable");
            return;
        }
        if (binding_initialization_index_ < materials_.size()) {
            const auto stop = (std::min)(materials_.size(),
                binding_initialization_index_ +
                    limits_.maximum_materials_per_update);
            for (; binding_initialization_index_ < stop;
                 ++binding_initialization_index_) {
                initialize_binding(binding_initialization_index_);
                ++progress_.materials_considered;
            }
            return;
        }

        const auto sources = source_document_->sources();
        while (embedded_source_index_ < sources.size() &&
            sources[embedded_source_index_].storage !=
                bsp::GoldSrcBspTextureSourceStorage::embedded) {
            ++embedded_source_index_;
        }
        if (embedded_source_index_ < sources.size()) {
            const auto& source = sources[embedded_source_index_];
            if (!decode_operation_) {
                if (!source.source_record_offset ||
                    !source.source_record_byte_count || !source.miptex ||
                    *source.source_record_offset > bsp_source_.size() ||
                    *source.source_record_byte_count >
                        bsp_source_.size() - *source.source_record_offset) {
                    fail(WorldTextureImportErrorCode::embedded_texture_decode_failed,
                        "Embedded BSP miptex record range is unavailable",
                        WorldTextureImportState::failed,
                        std::nullopt,
                        source.canonical_source_texture_index);
                    return;
                }
                if (textures_.size() >= limits_.maximum_texture_asset_count) {
                    fail(WorldTextureImportErrorCode::embedded_texture_decode_failed,
                        "Embedded texture asset count exceeds the configured limit",
                        WorldTextureImportState::failed,
                        std::nullopt,
                        source.canonical_source_texture_index);
                    return;
                }
                const auto record = bsp_source_.subspan(
                    *source.source_record_offset,
                    *source.source_record_byte_count);
                auto started = indexed::GoldSrcIndexedTextureDecodeOperation::begin(
                    record,
                    indexed::GoldSrcMiptexSourceProfile::bsp_embedded,
                    assets::WorldTextureSourceKind::embedded_bsp,
                    source.canonical_source_texture_index,
                    std::nullopt,
                    indexed_limits(limits_));
                if (!started) {
                    fail(WorldTextureImportErrorCode::embedded_texture_decode_failed,
                        "Shared decoder rejected an embedded BSP miptex record",
                        WorldTextureImportState::failed,
                        std::nullopt,
                        source.canonical_source_texture_index);
                    return;
                }
                decode_operation_.emplace(std::move(*started.operation));
            }
            const auto before = decode_operation_->converted_rgba_byte_count();
            const auto decode_state = decode_operation_->update(
                limits_.maximum_pixel_conversion_bytes_per_update);
            const auto after = decode_operation_->converted_rgba_byte_count();
            progress_.pixel_conversion_bytes += after - before;
            if (decode_state ==
                indexed::GoldSrcIndexedTextureDecodeState::failed) {
                fail(WorldTextureImportErrorCode::embedded_texture_decode_failed,
                    "Shared decoder failed for an embedded BSP miptex record",
                    WorldTextureImportState::failed,
                    std::nullopt,
                    source.canonical_source_texture_index);
                return;
            }
            if (decode_state ==
                indexed::GoldSrcIndexedTextureDecodeState::complete) {
                auto texture = decode_operation_->take_texture();
                if (!texture || !retain_decoded_bytes(*texture)) {
                    fail(WorldTextureImportErrorCode::embedded_texture_decode_failed,
                        "Embedded texture exceeds the aggregate decoded-memory limit",
                        WorldTextureImportState::failed,
                        std::nullopt,
                        source.canonical_source_texture_index);
                    return;
                }
                const auto texture_index = textures_.size();
                textures_.push_back(std::move(*texture));
                embedded_bindings_.push_back(EmbeddedTextureBinding{
                    source.canonical_source_texture_index,
                    texture_index});
                ++progress_.embedded_textures_decoded;
                decode_operation_.reset();
                ++embedded_source_index_;
            }
            return;
        }

        if (embedded_binding_index_ < materials_.size()) {
            const auto stop = (std::min)(materials_.size(),
                embedded_binding_index_ + limits_.maximum_materials_per_update);
            for (; embedded_binding_index_ < stop; ++embedded_binding_index_) {
                bind_embedded_material(embedded_binding_index_);
                if (terminal_state(state_)) {
                    return;
                }
            }
            return;
        }
        state_ = WorldTextureImportState::parsing_worldspawn;
    }

    void initialize_binding(const std::size_t material_index)
    {
        const auto& material = materials_[material_index];
        auto& binding = bindings_[material_index];
        binding.material_index = material_index;
        binding.source_bsp_texture_index = material.source_texture_index;
        binding.status = assets::WorldMaterialTextureBindingStatus::
            missing_bsp_texture_reference;
        if (!material.source_texture_index) {
            return;
        }
        const auto* source = source_document_->source_for_texture_index(
            *material.source_texture_index);
        if (source == nullptr ||
            source->storage == bsp::GoldSrcBspTextureSourceStorage::missing) {
            return;
        }
        if (source->storage ==
            bsp::GoldSrcBspTextureSourceStorage::external_reference) {
            binding.status = assets::WorldMaterialTextureBindingStatus::
                external_texture_not_found;
            has_external_requirements_ = true;
            return;
        }
        // A fatal embedded decode never publishes this provisional status.
        binding.status = assets::WorldMaterialTextureBindingStatus::
            malformed_embedded_texture;
    }

    void bind_embedded_material(const std::size_t material_index)
    {
        const auto& material = materials_[material_index];
        auto& binding = bindings_[material_index];
        if (!material.source_texture_index ||
            material.texture_storage != assets::WorldTextureStorage::embedded) {
            return;
        }
        const auto* source = source_document_->source_for_texture_index(
            *material.source_texture_index);
        if (source == nullptr ||
            source->storage != bsp::GoldSrcBspTextureSourceStorage::embedded) {
            return;
        }
        const auto found = std::ranges::find_if(embedded_bindings_,
            [source](const EmbeddedTextureBinding& candidate) {
                return candidate.canonical_source_texture_index ==
                    source->canonical_source_texture_index;
            });
        if (found == embedded_bindings_.end()) {
            fail(WorldTextureImportErrorCode::embedded_texture_decode_failed,
                "An embedded material has no decoded texture asset",
                WorldTextureImportState::failed,
                material_index,
                *material.source_texture_index);
            return;
        }
        binding.status =
            assets::WorldMaterialTextureBindingStatus::resolved_embedded;
        binding.texture_asset_index = found->texture_asset_index;
    }

    void parse_worldspawn()
    {
        if (!source_document_ ||
            source_document_->entity_lump_offset() > bsp_source_.size() ||
            source_document_->entity_lump_byte_count() >
                bsp_source_.size() - source_document_->entity_lump_offset()) {
            fail(WorldTextureImportErrorCode::worldspawn_parse_failed,
                "Validated BSP entity-lump range is unavailable");
            return;
        }
        const auto entity_lump = bsp_source_.subspan(
            source_document_->entity_lump_offset(),
            source_document_->entity_lump_byte_count());
        auto parsed = bsp::GoldSrcEntityLumpParser::
            parse_worldspawn_wad_references(
                entity_lump, worldspawn_limits(limits_));
        if (!parsed) {
            fail(WorldTextureImportErrorCode::worldspawn_parse_failed,
                "Inert worldspawn metadata is malformed");
            return;
        }
        wad_references_.emplace(std::move(*parsed.references));
        archive_metadata_.reserve(wad_references_->size());
        for (const auto& reference : wad_references_->references()) {
            assets::WorldTextureArchiveMetadata metadata;
            metadata.declaration_ordinal = reference.declaration_ordinal;
            metadata.basename_byte_count = reference.basename.size();
            // Unopened declarations remain metadata-only. If external lookup
            // requires the archive, resolution below replaces this status.
            metadata.status = assets::WorldTextureArchiveStatus::not_required;
            archive_metadata_.push_back(metadata);
        }
        if (!has_external_requirements_) {
            state_ = WorldTextureImportState::building_texture_set;
            return;
        }
        if (wad_references_->empty()) {
            wad_list_missing_ = true;
            state_ = WorldTextureImportState::resolving_wad_references;
            return;
        }
        state_ = WorldTextureImportState::resolving_wad_references;
    }

    void resolve_next_wad(const WorldTextureImportTimePoint now)
    {
        if (!wad_references_) {
            fail(WorldTextureImportErrorCode::wad_reference_invalid,
                "Approved WAD reference list is unavailable");
            return;
        }
        if (!has_unresolved_external_materials() ||
            current_wad_index_ >= wad_references_->size()) {
            finish_external_lookup();
            return;
        }
        const auto& reference =
            wad_references_->references()[current_wad_index_];
        auto virtual_name =
            local_resources::LocalVirtualResourceName::create(reference.basename);
        if (!virtual_name) {
            fail(WorldTextureImportErrorCode::wad_reference_invalid,
                "Sanitized WAD basename could not become a local virtual name",
                WorldTextureImportState::failed,
                std::nullopt,
                std::nullopt,
                reference.declaration_ordinal);
            return;
        }

        auto resolved = environment_->resolver().resolve(*virtual_name.name);
        ++progress_.wad_declarations_considered;
        if (!resolved) {
            if (resolved.code ==
                local_resources::LocalResourceResolutionCode::not_found) {
                archive_metadata_[current_wad_index_].status =
                    assets::WorldTextureArchiveStatus::missing;
                ++current_wad_index_;
                return;
            }
            fail(WorldTextureImportErrorCode::wad_source_resolution_failed,
                "Declared WAD could not be resolved safely in approved roots",
                WorldTextureImportState::failed,
                std::nullopt,
                std::nullopt,
                reference.declaration_ordinal,
                resolved.code);
            return;
        }
        if (!resolved.file) {
            fail(WorldTextureImportErrorCode::wad_source_resolution_failed,
                "WAD resolver reported success without a read-only file",
                WorldTextureImportState::failed,
                std::nullopt,
                std::nullopt,
                reference.declaration_ordinal,
                local_resources::LocalResourceResolutionCode::io_error);
            return;
        }

        const auto root_id = resolved.file->root_id();
        const auto identity = resolved.file->identity();
        const auto file_size = resolved.file->file_size();
        resolved.file->close();
        auto locator = environment_->make_locator(root_id,
            std::move(*virtual_name.name), identity, file_size);
        if (!locator) {
            fail(WorldTextureImportErrorCode::wad_source_resolution_failed,
                "Resolved WAD metadata could not form a verified locator",
                WorldTextureImportState::failed,
                std::nullopt,
                std::nullopt,
                reference.declaration_ordinal);
            return;
        }
        auto opened = wad_source_opener_.begin(
            *locator.locator, environment_, limits_.wad_source_open);
        ++progress_.wad_source_open_attempts;
        if (!opened) {
            fail(WorldTextureImportErrorCode::wad_source_open_failed,
                "Verified WAD source opening could not begin",
                WorldTextureImportState::failed,
                std::nullopt,
                std::nullopt,
                reference.declaration_ordinal,
                std::nullopt,
                opened.error
                    ? std::optional{opened.error->code}
                    : std::nullopt);
            return;
        }
        archive_metadata_[current_wad_index_].source_root_ordinal =
            root_id.value();
        wad_open_operation_.emplace(std::move(*opened.operation));
        progress_.wad_sources_open = 1U;
        state_ = WorldTextureImportState::opening_wad;
        wad_open_operation_->update(now);
        synchronize_wad_open();
    }

    void update_wad_open(const WorldTextureImportTimePoint now)
    {
        if (!wad_open_operation_) {
            fail(WorldTextureImportErrorCode::wad_source_open_failed,
                "WAD source open operation is unavailable");
            return;
        }
        wad_open_operation_->update(now);
        synchronize_wad_open();
    }

    void synchronize_wad_open()
    {
        if (!wad_open_operation_) {
            return;
        }
        switch (wad_open_operation_->state()) {
        case local_assets::LocalAssetSourceOpenState::opening:
        case local_assets::LocalAssetSourceOpenState::reading:
        case local_assets::LocalAssetSourceOpenState::validating:
            return;
        case local_assets::LocalAssetSourceOpenState::source_ready: {
            auto source = wad_open_operation_->take_result();
            if (!source) {
                fail(WorldTextureImportErrorCode::wad_source_open_failed,
                    "WAD source operation completed without owning bytes");
                return;
            }
            current_wad_source_.emplace(std::move(*source));
            archive_metadata_[current_wad_index_].source_byte_count =
                static_cast<std::size_t>(current_wad_source_->byte_count());
            wad_open_operation_.reset();
            state_ = WorldTextureImportState::parsing_wad_catalog;
            return;
        }
        case local_assets::LocalAssetSourceOpenState::cancelled:
            fail(WorldTextureImportErrorCode::cancelled,
                "WAD source opening was cancelled",
                WorldTextureImportState::cancelled);
            return;
        case local_assets::LocalAssetSourceOpenState::timed_out:
            fail(WorldTextureImportErrorCode::timed_out,
                "WAD source opening timed out",
                WorldTextureImportState::timed_out,
                std::nullopt,
                std::nullopt,
                current_archive_ordinal(),
                std::nullopt,
                local_assets::LocalAssetSourceOpenErrorCode::timed_out);
            return;
        case local_assets::LocalAssetSourceOpenState::failed: {
            const auto* nested = wad_open_operation_->error();
            fail(WorldTextureImportErrorCode::wad_source_open_failed,
                "Verified WAD source opening failed",
                WorldTextureImportState::failed,
                std::nullopt,
                std::nullopt,
                current_archive_ordinal(),
                std::nullopt,
                nested ? std::optional{nested->code} : std::nullopt);
            return;
        }
        case local_assets::LocalAssetSourceOpenState::idle:
            fail(WorldTextureImportErrorCode::wad_source_open_failed,
                "WAD source operation returned to idle unexpectedly");
            return;
        }
    }

    void parse_wad_catalog()
    {
        if (!current_wad_source_) {
            fail(WorldTextureImportErrorCode::wad_catalog_failed,
                "Owning WAD source bytes are unavailable");
            return;
        }
        auto parsed = wad3::GoldSrcWad3CatalogParser::parse(
            current_wad_source_->source().bytes(),
            wad_catalog_limits(limits_));
        if (!parsed) {
            if (current_wad_index_ < archive_metadata_.size()) {
                archive_metadata_[current_wad_index_].status =
                    assets::WorldTextureArchiveStatus::malformed;
            }
            fail(WorldTextureImportErrorCode::wad_catalog_failed,
                "Declared WAD3 catalog is malformed",
                WorldTextureImportState::failed,
                std::nullopt,
                std::nullopt,
                current_archive_ordinal());
            return;
        }
        current_wad_catalog_.emplace(std::move(*parsed.catalog));
        auto& metadata = archive_metadata_[current_wad_index_];
        metadata.status = assets::WorldTextureArchiveStatus::resolved;
        metadata.catalog_entry_count = current_wad_catalog_->entry_count();
        current_external_material_index_ = 0U;
        state_ = WorldTextureImportState::resolving_external_textures;
    }

    void update_external_textures()
    {
        if (!current_wad_source_ || !current_wad_catalog_ || !wad_references_) {
            fail(WorldTextureImportErrorCode::wad_catalog_failed,
                "Current WAD catalog lost its owning source prerequisite");
            return;
        }
        if (decode_operation_) {
            update_external_decode();
            return;
        }

        std::size_t considered = 0U;
        while (current_external_material_index_ < materials_.size() &&
            considered < limits_.maximum_materials_per_update) {
            const auto index = current_external_material_index_++;
            ++considered;
            auto& binding = bindings_[index];
            if (binding.status !=
                assets::WorldMaterialTextureBindingStatus::
                    external_texture_not_found) {
                continue;
            }
            const auto& material = materials_[index];
            if (!material.texture_name || !material.width || !material.height ||
                !material.source_texture_index) {
                fail(WorldTextureImportErrorCode::wad_texture_decode_failed,
                    "External BSP material metadata is incomplete",
                    WorldTextureImportState::failed,
                    index,
                    material.source_texture_index,
                    current_archive_ordinal());
                return;
            }
            const auto* entry =
                current_wad_catalog_->find_miptex(*material.texture_name);
            if (entry == nullptr) {
                continue;
            }

            const auto previously_decoded = std::ranges::find_if(
                external_bindings_,
                [this, entry](const ExternalTextureBinding& candidate) {
                    return candidate.archive_ordinal ==
                               current_archive_ordinal() &&
                        candidate.directory_ordinal == entry->directory_ordinal;
                });
            if (previously_decoded != external_bindings_.end()) {
                const auto& texture =
                    textures_[previously_decoded->texture_asset_index];
                if (texture.width != *material.width ||
                    texture.height != *material.height) {
                    binding.status = assets::WorldMaterialTextureBindingStatus::
                        external_texture_dimension_mismatch;
                    binding.source_archive_ordinal = current_archive_ordinal();
                } else {
                    binding.status =
                        assets::WorldMaterialTextureBindingStatus::resolved_wad3;
                    binding.texture_asset_index =
                        previously_decoded->texture_asset_index;
                    binding.source_archive_ordinal = current_archive_ordinal();
                }
                continue;
            }

            wad3::GoldSrcWad3TextureRequest request;
            request.expected_texture_name = *material.texture_name;
            request.expected_width = *material.width;
            request.expected_height = *material.height;
            request.source_bsp_texture_index = material.source_texture_index;
            request.source_archive_ordinal = current_archive_ordinal();
            auto prepared = wad3::GoldSrcWad3TextureParser::parse(
                current_wad_source_->source().bytes(),
                *entry,
                request,
                indexed_limits(limits_));
            if (!prepared) {
                if (prepared.error && prepared.error->code ==
                        wad3::GoldSrcWad3TextureErrorCode::dimension_mismatch) {
                    binding.status = assets::WorldMaterialTextureBindingStatus::
                        external_texture_dimension_mismatch;
                    binding.source_archive_ordinal = current_archive_ordinal();
                    continue;
                }
                fail(WorldTextureImportErrorCode::wad_texture_decode_failed,
                    "Declared WAD3 miptex record is malformed",
                    WorldTextureImportState::failed,
                    index,
                    material.source_texture_index,
                    current_archive_ordinal());
                return;
            }
            if (textures_.size() >= limits_.maximum_texture_asset_count) {
                fail(WorldTextureImportErrorCode::wad_texture_decode_failed,
                    "External texture asset count exceeds the configured limit",
                    WorldTextureImportState::failed,
                    index,
                    material.source_texture_index,
                    current_archive_ordinal());
                return;
            }
            const auto& metadata = *prepared.texture;
            const auto record = current_wad_source_->source().bytes().subspan(
                metadata.record_byte_offset, metadata.record_byte_count);
            auto started = indexed::GoldSrcIndexedTextureDecodeOperation::begin(
                record,
                indexed::GoldSrcMiptexSourceProfile::wad3_lump,
                assets::WorldTextureSourceKind::external_wad3,
                material.source_texture_index,
                current_archive_ordinal(),
                indexed_limits(limits_));
            if (!started) {
                fail(WorldTextureImportErrorCode::wad_texture_decode_failed,
                    "Shared decoder rejected a validated WAD3 miptex record",
                    WorldTextureImportState::failed,
                    index,
                    material.source_texture_index,
                    current_archive_ordinal());
                return;
            }
            pending_external_material_index_ = index;
            pending_external_directory_ordinal_ = entry->directory_ordinal;
            decode_operation_.emplace(std::move(*started.operation));
            update_external_decode();
            return;
        }

        if (current_external_material_index_ >= materials_.size()) {
            clear_active_wad();
            ++current_wad_index_;
            state_ = WorldTextureImportState::resolving_wad_references;
        }
    }

    void update_external_decode()
    {
        if (!decode_operation_ || !pending_external_material_index_ ||
            !pending_external_directory_ordinal_) {
            fail(WorldTextureImportErrorCode::wad_texture_decode_failed,
                "External decoder lost its bounded material metadata");
            return;
        }
        const auto before = decode_operation_->converted_rgba_byte_count();
        const auto decode_state = decode_operation_->update(
            limits_.maximum_pixel_conversion_bytes_per_update);
        const auto after = decode_operation_->converted_rgba_byte_count();
        progress_.pixel_conversion_bytes += after - before;
        if (decode_state == indexed::GoldSrcIndexedTextureDecodeState::failed) {
            fail(WorldTextureImportErrorCode::wad_texture_decode_failed,
                "Shared decoder failed for a WAD3 miptex record",
                WorldTextureImportState::failed,
                pending_external_material_index_,
                materials_[*pending_external_material_index_].source_texture_index,
                current_archive_ordinal());
            return;
        }
        if (decode_state != indexed::GoldSrcIndexedTextureDecodeState::complete) {
            return;
        }
        auto texture = decode_operation_->take_texture();
        if (!texture || !retain_decoded_bytes(*texture)) {
            fail(WorldTextureImportErrorCode::wad_texture_decode_failed,
                "External texture exceeds the aggregate decoded-memory limit",
                WorldTextureImportState::failed,
                pending_external_material_index_,
                materials_[*pending_external_material_index_].source_texture_index,
                current_archive_ordinal());
            return;
        }
        const auto texture_index = textures_.size();
        textures_.push_back(std::move(*texture));
        external_bindings_.push_back(ExternalTextureBinding{
            current_archive_ordinal(),
            *pending_external_directory_ordinal_,
            texture_index});
        auto& binding = bindings_[*pending_external_material_index_];
        binding.status = assets::WorldMaterialTextureBindingStatus::resolved_wad3;
        binding.texture_asset_index = texture_index;
        binding.source_archive_ordinal = current_archive_ordinal();
        ++archive_metadata_[current_wad_index_].textures_supplied_count;
        ++progress_.external_textures_decoded;
        decode_operation_.reset();
        pending_external_material_index_.reset();
        pending_external_directory_ordinal_.reset();
    }

    void finish_external_lookup()
    {
        if (final_external_binding_index_ < materials_.size()) {
            const auto stop = (std::min)(materials_.size(),
                final_external_binding_index_ +
                    limits_.maximum_materials_per_update);
            const auto any_missing_archive = std::ranges::any_of(
                archive_metadata_, [](const auto& archive) {
                    return archive.status ==
                        assets::WorldTextureArchiveStatus::missing;
                });
            for (; final_external_binding_index_ < stop;
                 ++final_external_binding_index_) {
                auto& binding = bindings_[final_external_binding_index_];
                if (binding.status ==
                    assets::WorldMaterialTextureBindingStatus::
                        external_texture_not_found) {
                    if (wad_list_missing_) {
                        binding.status = assets::
                            WorldMaterialTextureBindingStatus::
                                external_wad_list_missing;
                    } else if (any_missing_archive) {
                        binding.status = assets::
                            WorldMaterialTextureBindingStatus::
                                external_wad_archive_missing;
                    }
                }
            }
            return;
        }
        state_ = WorldTextureImportState::building_texture_set;
    }

    void build_texture_set()
    {
        auto built = assets::WorldTextureSet::create(
            std::move(textures_),
            std::move(bindings_),
            std::move(archive_metadata_),
            materials_.size(),
            texture_set_limits(limits_));
        if (!built) {
            fail(WorldTextureImportErrorCode::texture_set_build_failed,
                "Transactional world texture-set validation failed",
                WorldTextureImportState::failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                built.error
                    ? std::optional{built.error->code}
                    : std::nullopt);
            return;
        }
        const bool complete = built.texture_set->complete_for_world_materials();
        result_.emplace(std::move(*built.texture_set));
        source_document_.reset();
        wad_references_.reset();
        clear_active_wad();
        state_ = complete ? WorldTextureImportState::textures_ready
                          : WorldTextureImportState::textures_incomplete;
    }

    [[nodiscard]] bool retain_decoded_bytes(
        const assets::WorldTextureAsset& texture) noexcept
    {
        std::size_t texture_bytes = 0U;
        for (const auto& mip : texture.mip_levels) {
            if (!checked_add(texture_bytes, mip.rgba_pixels.size(),
                    texture_bytes)) {
                return false;
            }
        }
        std::size_t aggregate = 0U;
        if (!checked_add(decoded_rgba_bytes_, texture_bytes, aggregate) ||
            texture_bytes > limits_.maximum_decoded_bytes_per_texture ||
            aggregate > limits_.maximum_total_decoded_rgba_bytes) {
            return false;
        }
        decoded_rgba_bytes_ = aggregate;
        return true;
    }

    [[nodiscard]] bool has_unresolved_external_materials() const noexcept
    {
        return std::ranges::any_of(bindings_, [](const auto& binding) {
            return binding.status ==
                assets::WorldMaterialTextureBindingStatus::
                    external_texture_not_found;
        });
    }

    [[nodiscard]] std::uint32_t current_archive_ordinal() const noexcept
    {
        if (!wad_references_ || current_wad_index_ >= wad_references_->size()) {
            return 0U;
        }
        return wad_references_->references()[current_wad_index_]
            .declaration_ordinal;
    }

    void clear_active_wad() noexcept
    {
        if (wad_open_operation_) {
            wad_open_operation_->cancel();
        }
        wad_open_operation_.reset();
        current_wad_source_.reset();
        current_wad_catalog_.reset();
        decode_operation_.reset();
        pending_external_material_index_.reset();
        pending_external_directory_ordinal_.reset();
        progress_.wad_sources_open = 0U;
    }

    void fail(
        const WorldTextureImportErrorCode code,
        const std::string_view context,
        const WorldTextureImportState terminal =
            WorldTextureImportState::failed,
        const std::optional<std::size_t> material_index = std::nullopt,
        const std::optional<std::uint32_t> source_texture_index = std::nullopt,
        const std::optional<std::uint32_t> archive_ordinal = std::nullopt,
        const std::optional<local_resources::LocalResourceResolutionCode>
            resolution_code = std::nullopt,
        const std::optional<local_assets::LocalAssetSourceOpenErrorCode>
            source_open_code = std::nullopt,
        const std::optional<assets::WorldTextureSetErrorCode> texture_set_code =
            std::nullopt) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (wad_open_operation_) {
            wad_open_operation_->cancel();
        }
        state_ = terminal;
        result_.reset();
        source_document_.reset();
        wad_references_.reset();
        clear_active_wad();
        error_.reset();
        try {
            error_.emplace();
            error_->code = code;
            error_->material_index = material_index;
            error_->source_texture_index = source_texture_index;
            error_->archive_ordinal = archive_ordinal;
            error_->resolution_code = resolution_code;
            error_->source_open_code = source_open_code;
            error_->texture_set_code = texture_set_code;
            const auto bounded = context.substr(0U,
                (std::min)(context.size(),
                    kWorldTextureImportDiagnosticTextLimit));
            error_->context.assign(bounded.data(), bounded.size());
        } catch (...) {
        }
    }

public:
    std::vector<assets::WorldMaterialReference> materials_;
    std::span<const std::byte> bsp_source_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    GoldSrcWorldTextureImportLimits limits_;
    WorldTextureImportState state_{WorldTextureImportState::idle};
    WorldTextureImportProgress progress_{};
    std::optional<WorldTextureImportTimePoint> started_at_;
    std::optional<WorldTextureImportTimePoint> last_update_;
    std::optional<bsp::GoldSrcBspTextureSourceDocument> source_document_;
    std::optional<bsp::GoldSrcWadReferenceList> wad_references_;
    std::vector<assets::WorldTextureAsset> textures_;
    std::vector<assets::WorldMaterialTextureBinding> bindings_;
    std::vector<assets::WorldTextureArchiveMetadata> archive_metadata_;
    std::vector<EmbeddedTextureBinding> embedded_bindings_;
    std::vector<ExternalTextureBinding> external_bindings_;
    std::optional<indexed::GoldSrcIndexedTextureDecodeOperation>
        decode_operation_;
    local_assets::LocalAssetSourceOpener wad_source_opener_;
    std::optional<local_assets::LocalAssetSourceOpenOperation>
        wad_open_operation_;
    std::optional<local_assets::LocalAssetSource> current_wad_source_;
    std::optional<wad3::GoldSrcWad3Catalog> current_wad_catalog_;
    std::optional<std::size_t> pending_external_material_index_;
    std::optional<std::size_t> pending_external_directory_ordinal_;
    std::optional<assets::WorldTextureSet> result_;
    std::optional<WorldTextureImportError> error_;
    std::size_t binding_initialization_index_{0U};
    std::size_t embedded_source_index_{0U};
    std::size_t embedded_binding_index_{0U};
    std::size_t current_wad_index_{0U};
    std::size_t current_external_material_index_{0U};
    std::size_t final_external_binding_index_{0U};
    std::size_t decoded_rgba_bytes_{0U};
    bool has_external_requirements_{false};
    bool wad_list_missing_{false};
};

WorldTextureImportBeginResult WorldTextureImportOperation::begin(
    const assets::WorldAsset& world,
    const std::span<const std::byte> retained_bsp_source,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    GoldSrcWorldTextureImportLimits limits)
{
    if (!valid_goldsrc_world_texture_import_limits(limits) ||
        environment == nullptr || environment->root_count() == 0U) {
        return WorldTextureImportBeginResult{
            std::nullopt,
            WorldTextureImportError{
                WorldTextureImportErrorCode::invalid_configuration,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Texture import limits or local environment are invalid"},
        };
    }
    if (world.source_profile !=
            assets::WorldGeometrySourceProfile::goldsrc_bsp_v30 ||
        world.materials.size() > limits.maximum_material_count) {
        return WorldTextureImportBeginResult{
            std::nullopt,
            WorldTextureImportError{
                WorldTextureImportErrorCode::invalid_world_asset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "World asset is not a supported bounded GoldSrc BSP v30 world"},
        };
    }
    if (retained_bsp_source.empty()) {
        return WorldTextureImportBeginResult{
            std::nullopt,
            WorldTextureImportError{
                WorldTextureImportErrorCode::bsp_source_missing,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Retained approved BSP source bytes are missing"},
        };
    }
    try {
        auto implementation = std::make_unique<Implementation>(
            world.materials,
            retained_bsp_source,
            std::move(environment),
            std::move(limits));
        return WorldTextureImportBeginResult{
            WorldTextureImportOperation{std::move(implementation)},
            std::nullopt,
        };
    } catch (...) {
        return WorldTextureImportBeginResult{
            std::nullopt,
            WorldTextureImportError{
                WorldTextureImportErrorCode::unable_to_retain_state,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Unable to retain bounded texture import prerequisites"},
        };
    }
}

WorldTextureImportOperation::WorldTextureImportOperation(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_{std::move(implementation)}
{
}

WorldTextureImportOperation::~WorldTextureImportOperation() = default;
WorldTextureImportOperation::WorldTextureImportOperation(
    WorldTextureImportOperation&&) noexcept = default;
WorldTextureImportOperation& WorldTextureImportOperation::operator=(
    WorldTextureImportOperation&&) noexcept = default;

void WorldTextureImportOperation::update(const WorldTextureImportTimePoint now)
    noexcept
{
    implementation_->update(now);
}

void WorldTextureImportOperation::cancel() noexcept
{
    implementation_->cancel();
}

WorldTextureImportState WorldTextureImportOperation::state() const noexcept
{
    return implementation_->state_;
}

bool WorldTextureImportOperation::terminal() const noexcept
{
    return terminal_state(implementation_->state_);
}

const WorldTextureImportProgress& WorldTextureImportOperation::progress()
    const noexcept
{
    return implementation_->progress_;
}

const assets::WorldTextureSet* WorldTextureImportOperation::result()
    const noexcept
{
    return implementation_->result_ ? &*implementation_->result_ : nullptr;
}

const WorldTextureImportError* WorldTextureImportOperation::error()
    const noexcept
{
    return implementation_->error_ ? &*implementation_->error_ : nullptr;
}

std::optional<assets::WorldTextureSet>
WorldTextureImportOperation::take_result() noexcept
{
    if ((implementation_->state_ != WorldTextureImportState::textures_ready &&
            implementation_->state_ !=
                WorldTextureImportState::textures_incomplete) ||
        !implementation_->result_) {
        return std::nullopt;
    }
    return std::exchange(implementation_->result_, std::nullopt);
}

} // namespace hlclient::goldsrc
