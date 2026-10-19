#include <hlclient/goldsrc/hud_assets.hpp>
#include <hlclient/goldsrc/hud_manifest.hpp>
#include <hlclient/goldsrc/text_messages.hpp>
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
bool valid_sprite_name(std::string_view name) noexcept {
    return name.starts_with("sprites/") && name.ends_with(".spr") &&
        valid_hud_sprite_basename(name.substr(8U, name.size() - 12U));
}
bool valid_sprite_request(const game_api::HudSpriteRequest& request) noexcept {
    return !request.role.empty() && request.role.size() <= kMaximumHudManifestTokenBytes &&
        std::all_of(request.role.begin(), request.role.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-';
        }) && request.resolution && request.resolution <= 65'535U &&
        request.frame < game_api::kMaximumHudSpriteFrames && valid_sprite_name(request.sprite_name);
}
bool supplemental_count_bounded(std::span<const game_api::HudManifestRequest> requests) noexcept {
    std::size_t count{};
    for (const auto& request : requests) {
        if (request.supplemental_sprites.size() > game_api::kMaximumHudResourceEntries - count) return false;
        count += request.supplemental_sprites.size();
    }
    return true;
}
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
        const bool sprite = valid_sprite_name(name);
        const bool titles=name=="titles.txt";
        if ((!manifest && !sprite && !titles) || stop_.stop_requested())
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
        const auto maximum = titles ? game_api::kMaximumTextMessageBytes :
            manifest ? kMaximumHudManifestBytes : kMaximumHudSpriteSourceBytes;
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
    if (!valid_hud_descriptor_name(request.name) ||
        request.supplemental_sprites.size() > game_api::kMaximumHudResourceEntries) return false;
    for (std::size_t i = 0U; i < request.supplemental_sprites.size(); ++i) {
        const auto& sprite = request.supplemental_sprites[i];
        if (!valid_sprite_request(sprite)) return false;
        for (std::size_t previous = 0U; previous < i; ++previous) {
            const auto& other = request.supplemental_sprites[previous];
            if (sprite.role == other.role && sprite.resolution == other.resolution) return false;
        }
    }
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
    if (!supplemental_count_bounded(requests)) return failed_preparation(requests, Status::resource_limit);
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
                else if (parsed.records.size() > game_api::kMaximumHudResourceEntries - total_entries ||
                    request.supplemental_sprites.size() >
                        game_api::kMaximumHudResourceEntries - total_entries - parsed.records.size())
                    descriptor.status = Status::resource_limit;
                else if (std::any_of(request.supplemental_sprites.begin(), request.supplemental_sprites.end(),
                    [&](const auto& sprite) {
                        return std::any_of(parsed.records.begin(), parsed.records.end(), [&](const auto& record) {
                            return sprite.role == record.role && sprite.resolution == record.resolution;
                        });
                    })) descriptor.status = Status::not_authorized;
                else {
                    out.source_bytes += opened.file->bytes.size();
                    total_entries += parsed.records.size() + request.supplemental_sprites.size();
                    descriptor.status = Status::ready;
                    for (const auto& record : parsed.records) {
                        game_api::HudResourceEntry entry;
                        entry.role = record.role; entry.resolution = record.resolution;
                        entry.sprite_name = "sprites/" + record.sprite_basename + ".spr";
                        entry.source = record.source;
                        descriptor.entries.push_back(std::move(entry));
                    }
                    for (const auto& sprite : request.supplemental_sprites) {
                        game_api::HudResourceEntry entry;
                        entry.role = sprite.role; entry.resolution = sprite.resolution;
                        entry.sprite_name = sprite.sprite_name; entry.frame = sprite.frame;
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
    for (auto& descriptor : out.resources.descriptors) for (std::size_t entry_index = 0U;
        entry_index < descriptor.entries.size(); ++entry_index) {
        auto& entry = descriptor.entries[entry_index];
        // Only explicit trusted supplemental requests are appended beyond the
        // successfully parsed record count. Never infer whole-frame authority
        // from an empty/invalid rectangle supplied by the manifest itself.
        const bool whole_frame = entry_index >= descriptor.parsed_count;
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
                    limits.maximum_top_level_entries = limits.maximum_flattened_frames = game_api::kMaximumHudSpriteFrames;
                    limits.maximum_group_frames = game_api::kMaximumHudSpriteFrames;
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
        if (value.status == Status::ready && value.asset) {
            if (entry.frame >= value.asset->frames().size()) {
                entry.status = Status::invalid_rectangle;
                ++out.rejected_entries; continue;
            }
            const auto& frame = value.asset->frames()[entry.frame];
            entry.frame_width = frame.geometry.width; entry.frame_height = frame.geometry.height;
            entry.blend = blend_for(value.asset->texture_format());
            if (whole_frame) entry.source = {0U, 0U, entry.frame_width, entry.frame_height};
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
        for (std::size_t index = 0U; index < descriptor.entries.size(); ++index) {
            const auto& entry = descriptor.entries[index];
            identity += entry.sprite_name; identity += entry.asset_sha256;
            if (index >= descriptor.parsed_count) {
                // A different explicit frame/role/tier must publish a new set
                // revision even when it shares the same immutable SPR bytes.
                identity += "|supplement:" + entry.role + ":" + std::to_string(entry.resolution) +
                    ":" + std::to_string(entry.frame) + ";";
            }
        }
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
    std::span<const game_api::HudManifestRequest> requests,std::string_view text_resource) : impl_{std::make_unique<Impl>()} {
    if (requests.size() > game_api::kMaximumHudResourceDescriptors ||
        !supplemental_count_bounded(requests) ||
        (!text_resource.empty() && text_resource!="titles.txt") ||
        std::any_of(requests.begin(), requests.end(), [](const auto& request) { return !valid_request(request); })) {
        impl_->result = std::make_shared<const HudPreparedAssets>(failed_preparation(requests, Status::not_authorized)); return;
    }
    // Preallocate failure metadata before starting the worker: even an allocation
    // failure in parsing/import never masquerades as a successful empty resource set.
    auto failure = std::make_shared<const HudPreparedAssets>(failed_preparation(requests, Status::decode_failed));
    std::vector<game_api::HudManifestRequest> copied{requests.begin(), requests.end()};
    try { impl_->worker = std::jthread{[impl = impl_.get(), resources = std::move(resources),
        requests = std::move(copied), text_resource=std::string{text_resource}, failure](std::stop_token stop) {
        std::shared_ptr<const HudPreparedAssets> completed;
        try {
            ExactWorldHudSource source{resources,stop};
            auto prepared=prepare_hud_assets(source,requests,stop);
            if(!text_resource.empty() && !stop.stop_requested()) {
                const auto opened=source.read_file(text_resource);
                prepared.text_messages_status=opened ? Status::decode_failed : read_status(opened);
                if(opened)if(auto catalogue=parse_text_message_catalogue(opened.file->bytes)) {
                    prepared.text_messages=std::move(*catalogue);prepared.text_messages_status=Status::ready;
                }
            }
            completed=std::make_shared<const HudPreparedAssets>(std::move(prepared));
        }
        catch (...) { completed = failure; }
        if (!stop.stop_requested()) { std::lock_guard lock{impl->mutex}; impl->result = std::move(completed); }
    }}; } catch (...) { impl_->result = std::move(failure); }
}
ApprovedHudAssets::~ApprovedHudAssets() = default;
std::shared_ptr<const HudPreparedAssets> ApprovedHudAssets::snapshot() const noexcept {
    std::lock_guard lock{impl_->mutex}; return impl_->result;
}
} // namespace hlclient::goldsrc
