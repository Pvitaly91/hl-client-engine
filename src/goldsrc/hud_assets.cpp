#include <hlclient/goldsrc/hud_assets.hpp>
#include <hlclient/goldsrc/hud_manifest.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>
#include <hlclient/assets/asset_manager.hpp>
#include <hlclient/hash/sha256.hpp>
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace hlclient::goldsrc {
namespace {
using Status = game_api::HudResourceStatus;
Status read_status(const filesystem::FileReadResult& value) noexcept {
    if (!value.error) return Status::open_failed;
    switch (value.error->code) {
    case filesystem::FileReadErrorCode::NotFound: return Status::missing_file;
    case filesystem::FileReadErrorCode::InvalidVirtualPath:
    case filesystem::FileReadErrorCode::InvalidConfiguration: return Status::not_authorized;
    case filesystem::FileReadErrorCode::TooLarge: return Status::resource_limit;
    default: return Status::open_failed;
    }
}
filesystem::FileReadResult rejected_read(const std::filesystem::path& path,
    filesystem::FileReadErrorCode code) {
    return {std::nullopt, filesystem::FileReadError{code, path, "Bounded HUD resource read rejected"}};
}
std::uint64_t revision(const hash::Sha256Digest& digest) noexcept {
    std::uint64_t value{};
    for (std::size_t i = 0U; i < 8U; ++i) value = (value << 8U) | std::to_integer<std::uint8_t>(digest[i]);
    return value ? value : 1U;
}
game_api::HudSpriteBlend blend_for(assets::SpriteTextureFormat value) noexcept {
    switch (value) {
    case assets::SpriteTextureFormat::normal: return game_api::HudSpriteBlend::opaque;
    case assets::SpriteTextureFormat::alpha_test: return game_api::HudSpriteBlend::alpha_test;
    case assets::SpriteTextureFormat::additive: return game_api::HudSpriteBlend::additive;
    case assets::SpriteTextureFormat::index_alpha: return game_api::HudSpriteBlend::straight_alpha;
    }
    return game_api::HudSpriteBlend::opaque;
}
class SingleApprovedSource final : public filesystem::IFileSystem {
public:
    explicit SingleApprovedSource(filesystem::FileContents value) : file_{std::move(value)} {}
    filesystem::FileReadResult read_file(const std::filesystem::path& path) const override {
        if (path != file_.virtual_path) return rejected_read(path, filesystem::FileReadErrorCode::InvalidVirtualPath);
        return {file_, std::nullopt};
    }
private:
    filesystem::FileContents file_;
};

class ExactWorldHudSource final : public filesystem::IFileSystem {
public:
    ExactWorldHudSource(const PreparedSoundResources& value, std::stop_token stop)
        : resources_{value}, stop_{stop}, deadline_{std::chrono::steady_clock::now() + std::chrono::seconds{20}} {}
    filesystem::FileReadResult read_file(const std::filesystem::path& path) const override {
        const auto name = path.generic_string();
        const bool manifest = valid_hud_descriptor_name(name);
        const bool sprite = name.starts_with("sprites/") && name.ends_with(".spr") &&
            valid_hud_sprite_basename(std::string_view{name}.substr(8U, name.size() - 12U));
        if ((!manifest && !sprite) || stop_.stop_requested())
            return rejected_read(path, filesystem::FileReadErrorCode::InvalidVirtualPath);
        if (std::chrono::steady_clock::now() >= deadline_)
            return rejected_read(path, filesystem::FileReadErrorCode::IoError);
        if (!resources_.manifest || !resources_.environment ||
            !resources_.manifest->world_entry() || !resources_.manifest->world_entry()->locator())
            return rejected_read(path, filesystem::FileReadErrorCode::NotFound);
        auto virtual_name = local_resources::LocalVirtualResourceName::create(name);
        if (!virtual_name) return rejected_read(path, filesystem::FileReadErrorCode::InvalidVirtualPath);
        auto resolved = resources_.environment->resolve_exact_root(*virtual_name.name,
            resources_.manifest->world_entry()->locator()->root_id());
        if (!resolved) {
            using Code = local_resources::LocalResourceResolutionCode;
            const auto code = resolved.code == Code::not_found ? filesystem::FileReadErrorCode::NotFound :
                resolved.code == Code::too_large ? filesystem::FileReadErrorCode::TooLarge :
                resolved.code == Code::io_error ? filesystem::FileReadErrorCode::IoError :
                filesystem::FileReadErrorCode::InvalidVirtualPath;
            return rejected_read(path, code);
        }
        const auto maximum = manifest ? kMaximumHudManifestBytes : kMaximumHudSpriteSourceBytes;
        if (resolved.file->file_size() > maximum ||
            resolved.file->file_size() > kMaximumHudAggregateSourceBytes - bytes_read_) {
            resolved.file->close(); return rejected_read(path, filesystem::FileReadErrorCode::TooLarge);
        }
        auto locator = resources_.environment->make_locator(resolved.file->root_id(), *virtual_name.name,
            resolved.file->identity(), resolved.file->file_size());
        resolved.file->close();
        if (!locator) return rejected_read(path, filesystem::FileReadErrorCode::InvalidVirtualPath);
        local_assets::LocalAssetSourceOpener opener;
        local_assets::LocalAssetSourceOpenLimits limits;
        limits.maximum_source_bytes = maximum; limits.timeout = std::chrono::seconds{2};
        auto opened = opener.begin(*locator.locator, resources_.environment, limits);
        if (!opened) return rejected_read(path, filesystem::FileReadErrorCode::IoError);
        for (std::size_t i = 0U; i < 65'536U && !opened.operation->result() && !opened.operation->error(); ++i) {
            if (stop_.stop_requested() || std::chrono::steady_clock::now() >= deadline_) { opened.operation->cancel(); break; }
            opened.operation->update(std::chrono::steady_clock::now());
        }
        auto source = opened.operation->take_result();
        if (!source) return rejected_read(path, filesystem::FileReadErrorCode::IoError);
        filesystem::FileContents file;
        file.virtual_path = path;
        file.bytes.assign(source->source().bytes().begin(), source->source().bytes().end());
        bytes_read_ += file.bytes.size();
        file.metadata = filesystem::FileMetadata{file.bytes.size(), std::nullopt};
        return {std::move(file), std::nullopt};
    }
private:
    const PreparedSoundResources& resources_;
    std::stop_token stop_;
    std::chrono::steady_clock::time_point deadline_;
    mutable std::size_t bytes_read_{};
};
bool valid_request(const game_api::HudManifestRequest& request) noexcept {
    if (!valid_hud_descriptor_name(request.name)) return false;
    if (!request.count_compatibility) return true;
    const auto& compatibility = *request.count_compatibility;
    return compatibility.exact_source_sha256.size() == 64U &&
        std::all_of(compatibility.exact_source_sha256.begin(), compatibility.exact_source_sha256.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }) && compatibility.declared_count <= game_api::kMaximumHudResourceEntries &&
        compatibility.parsed_count <= game_api::kMaximumHudResourceEntries;
}
HudPreparedAssets failed_preparation(std::span<const game_api::HudManifestRequest> requests, Status status) {
    HudPreparedAssets result;
    result.resources.revision = 1U;
    for (const auto& request : requests.first(std::min(requests.size(), game_api::kMaximumHudResourceDescriptors))) {
        game_api::HudResourceDescriptor descriptor;
        if (valid_hud_descriptor_name(request.name)) descriptor.name = request.name;
        descriptor.status = status;
        result.resources.descriptors.push_back(std::move(descriptor));
    }
    result.rejected_entries = result.resources.descriptors.size();
    return result;
}
} // namespace

HudPreparedAssets prepare_hud_assets(const filesystem::IFileSystem& source,
    std::span<const game_api::HudManifestRequest> requests, std::stop_token stop) {
    HudPreparedAssets out;
    if (requests.empty() || requests.size() > game_api::kMaximumHudResourceDescriptors) return out;
    std::set<std::string> requested_names;
    std::size_t total_entries{};
    for (const auto& request : requests) {
        game_api::HudResourceDescriptor descriptor;
        if (valid_hud_descriptor_name(request.name)) descriptor.name = request.name;
        if (!valid_request(request) || !requested_names.insert(request.name).second) {
            descriptor.status = Status::not_authorized;
        } else if (stop.stop_requested()) {
            descriptor.status = Status::not_yet_resolved;
        } else {
            const auto opened = source.read_file(request.name);
            if (!opened) descriptor.status = read_status(opened);
            else if (opened.file->virtual_path != std::filesystem::path{request.name}) descriptor.status = Status::not_authorized;
            else if (opened.file->bytes.size() > kMaximumHudAggregateSourceBytes - out.source_bytes)
                descriptor.status = Status::resource_limit;
            else {
                const auto parsed = parse_hud_manifest(opened.file->bytes, request.count_compatibility);
                descriptor.source_sha256 = parsed.source_sha256;
                descriptor.declared_count = parsed.declared_count;
                descriptor.parsed_count = static_cast<std::uint32_t>(parsed.records.size());
                descriptor.exact_count_compatibility_applied = parsed.exact_count_compatibility_applied;
                if (!parsed) descriptor.status = Status::invalid_manifest;
                else if (parsed.records.size() > game_api::kMaximumHudResourceEntries - total_entries)
                    descriptor.status = Status::resource_limit;
                else {
                    out.source_bytes += opened.file->bytes.size(); total_entries += parsed.records.size();
                    descriptor.status = Status::ready;
                    for (const auto& record : parsed.records) {
                        game_api::HudResourceEntry entry;
                        entry.role = record.role; entry.resolution = record.resolution;
                        entry.sprite_name = "sprites/" + record.sprite_basename + ".spr";
                        entry.source = record.source;
                        descriptor.entries.push_back(std::move(entry));
                    }
                }
            }
        }
        out.resources.descriptors.push_back(std::move(descriptor));
    }
    struct Cached final { Status status{}; std::uint64_t id{}, revision{}; std::string sha;
        std::shared_ptr<const entity_render::SpriteRenderAsset> asset; };
    std::map<std::string, Cached> cache;
    for (auto& descriptor : out.resources.descriptors) for (auto& entry : descriptor.entries) {
        auto cached = cache.find(entry.sprite_name);
        if (cached == cache.end()) {
            Cached value; value.status = Status::resource_limit;
            if (!stop.stop_requested() && cache.size() < game_api::kMaximumHudResourceDependencies) {
                auto opened = source.read_file(entry.sprite_name);
                if (!opened) value.status = read_status(opened);
                else if (opened.file->virtual_path != std::filesystem::path{entry.sprite_name}) value.status = Status::not_authorized;
                else if (opened.file->bytes.size() > kMaximumHudSpriteSourceBytes ||
                    opened.file->bytes.size() > kMaximumHudAggregateSourceBytes - out.source_bytes) value.status = Status::resource_limit;
                else {
                    const auto digest = hash::sha256(opened.file->bytes);
                    out.source_bytes += opened.file->bytes.size();
                    if (digest) { value.revision = revision(*digest); value.sha = hash::sha256_hex(*digest); }
                    SingleApprovedSource exact{std::move(*opened.file)};
                    const auto remaining = kMaximumHudDecodedBytes - out.retained_decoded_bytes;
                    constexpr std::size_t metadata_reserve = 4U * 1024U * 1024U;
                    if (remaining <= metadata_reserve + 13U) { value.status = Status::resource_limit; }
                    else {
                    // Canonical parser rejects BEFORE pixel allocation. One indexed
                    // pixel + two owning importer RGBA copies + renderer RGBA =13.
                    // Source buffers are bounded independently (8MiB each,64MiB IO).
                    sprite::GoldSrcSpriteImportLimits limits;
                    limits.maximum_source_bytes = kMaximumHudSpriteSourceBytes;
                    limits.maximum_width = limits.maximum_height = 4'096U;
                    limits.maximum_top_level_entries = limits.maximum_flattened_frames = 64U;
                    limits.maximum_group_frames = 64U;
                    limits.maximum_metadata_bytes = 64U * 1024U;
                    limits.maximum_total_indexed_bytes = std::min<std::size_t>(16U * 1024U * 1024U, (remaining - metadata_reserve) / 13U);
                    limits.maximum_total_rgba_bytes = std::min<std::size_t>(64U * 1024U * 1024U, ((remaining - metadata_reserve) / 13U) * 8U);
                    limits.maximum_pixels_per_frame = limits.maximum_total_indexed_bytes;
                    assets::AssetImporterRegistries importers;
                    const bool registered = static_cast<bool>(importers.sprites.register_importer(
                        std::make_unique<sprite::GoldSrcSpriteImporter>(limits), 100));
                    auto imported = assets::AssetManager{exact, importers}.load_sprite(entry.sprite_name);
                    if (!registered || !imported || !digest) value.status = Status::decode_failed;
                    else {
                        const auto& asset = imported.value();
                        std::size_t retained{};
                        for (const auto& frame : asset.frames) retained += frame.image.pixels.size();
                        if (asset.source_data) for (const auto& frame : asset.source_data->indexed_frames)
                            retained += frame.indexed_pixels.size() + frame.derived_rgba8.size();
                        // Include importer owning allocations AND renderer RGBA copies at
                        // peak publication; the importer dies before the next dependency.
                        const auto render_bytes = asset.source_data ? asset.source_data->statistics.derived_rgba_byte_count : 0U;
                        if (retained + render_bytes > kMaximumHudDecodedBytes - out.retained_decoded_bytes)
                            value.status = Status::resource_limit;
                        else {
                            value.id = static_cast<std::uint64_t>(cache.size() + 1U);
                            auto rendered = entity_render::SpriteRenderAssetBuilder{}.build(asset, {value.id, value.revision});
                            if (!rendered) value.status = Status::decode_failed;
                            else {
                                value.asset = std::make_shared<const entity_render::SpriteRenderAsset>(std::move(*rendered.asset));
                                value.status = Status::ready;
                                out.retained_decoded_bytes += value.asset->statistics().texture_rgba_bytes;
                                out.sprites.push_back({value.id, value.revision, value.asset});
                            }
                        }
                    }
                    }
                }
            }
            // A limit failure must not grow an unbounded rejected-name cache.
            if (cache.size() == game_api::kMaximumHudResourceDependencies) {
                entry.status = Status::resource_limit; ++out.rejected_entries; continue;
            }
            cached = cache.emplace(entry.sprite_name, std::move(value)).first;
        }
        const auto& value = cached->second;
        entry.status = value.status; entry.asset_sha256 = value.sha;
        if (value.status == Status::ready && value.asset && !value.asset->frames().empty()) {
            const auto& frame = value.asset->frames().front();
            entry.frame_width = frame.geometry.width; entry.frame_height = frame.geometry.height;
            entry.blend = blend_for(value.asset->texture_format());
            if (!valid_hud_source_rectangle(entry.source, entry.frame_width, entry.frame_height))
                entry.status = Status::invalid_rectangle;
            else if (frame.profile == entity_render::SpriteRenderTextureProfile::unsupported)
                entry.status = Status::unsupported_material;
            else { entry.resource_id = value.id; entry.resource_revision = value.revision; }
        }
        if (entry.status == Status::ready) ++out.ready_entries; else ++out.rejected_entries;
    }
    std::string identity;
    for (const auto& descriptor : out.resources.descriptors) {
        identity += descriptor.name; identity += descriptor.source_sha256;
        for (const auto& entry : descriptor.entries) { identity += entry.sprite_name; identity += entry.asset_sha256; }
    }
    const auto digest = hash::sha256(std::as_bytes(std::span{identity.data(), identity.size()}));
    if (digest) out.resources.revision = revision(*digest);
    return out;
}
struct ApprovedHudAssets::Impl final {
    mutable std::mutex mutex;
    std::shared_ptr<const HudPreparedAssets> result;
    std::jthread worker;
};
ApprovedHudAssets::ApprovedHudAssets(PreparedSoundResources resources,
    std::span<const game_api::HudManifestRequest> requests) : impl_{std::make_unique<Impl>()} {
    if (requests.size() > game_api::kMaximumHudResourceDescriptors ||
        std::any_of(requests.begin(), requests.end(), [](const auto& request) { return !valid_request(request); })) {
        impl_->result = std::make_shared<const HudPreparedAssets>(failed_preparation(requests, Status::not_authorized)); return;
    }
    // Preallocate failure metadata before starting the worker: even an allocation
    // failure in parsing/import never masquerades as a successful empty resource set.
    auto failure = std::make_shared<const HudPreparedAssets>(failed_preparation(requests, Status::decode_failed));
    std::vector<game_api::HudManifestRequest> copied{requests.begin(), requests.end()};
    try { impl_->worker = std::jthread{[impl = impl_.get(), resources = std::move(resources),
        requests = std::move(copied), failure](std::stop_token stop) {
        std::shared_ptr<const HudPreparedAssets> completed;
        try { completed = std::make_shared<const HudPreparedAssets>(
            prepare_hud_assets(ExactWorldHudSource{resources, stop}, requests, stop)); }
        catch (...) { completed = failure; }
        if (!stop.stop_requested()) { std::lock_guard lock{impl->mutex}; impl->result = std::move(completed); }
    }}; } catch (...) { impl_->result = std::move(failure); }
}
ApprovedHudAssets::~ApprovedHudAssets() = default;
std::shared_ptr<const HudPreparedAssets> ApprovedHudAssets::snapshot() const noexcept {
    std::lock_guard lock{impl_->mutex}; return impl_->result;
}
} // namespace hlclient::goldsrc
