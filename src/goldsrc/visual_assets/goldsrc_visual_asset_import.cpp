#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>

#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>

#include <algorithm>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hlclient::goldsrc::visual_assets {
namespace {

inline constexpr std::size_t kVisualImportDiagnosticTextLimit = 192U;

[[nodiscard]] bool terminal_state(
    const GoldSrcVisualAssetImportState state) noexcept
{
    return state == GoldSrcVisualAssetImportState::asset_ready ||
           state == GoldSrcVisualAssetImportState::dependency_missing ||
           state == GoldSrcVisualAssetImportState::dependency_invalid ||
           state == GoldSrcVisualAssetImportState::cancelled ||
           state == GoldSrcVisualAssetImportState::timed_out ||
           state == GoldSrcVisualAssetImportState::failed;
}

[[nodiscard]] GoldSrcVisualAssetImportBeginResult begin_failure(
    const GoldSrcVisualAssetImportErrorCode code,
    const std::string_view context) noexcept
{
    GoldSrcVisualAssetImportBeginResult result;
    try {
        result.error.emplace();
        result.error->code = code;
        const auto bounded = context.substr(
            0U, (std::min)(context.size(), kVisualImportDiagnosticTextLimit));
        result.error->context.assign(bounded.data(), bounded.size());
    } catch (...) {
        result.error.reset();
        try {
            result.error.emplace();
            result.error->code =
                GoldSrcVisualAssetImportErrorCode::unable_to_retain_state;
        } catch (...) {
        }
    }
    return result;
}

[[nodiscard]] std::string qualified_studio_importer_id()
{
    std::string result{"model:"};
    result.append(studio::kGoldSrcStudioModelImporterId);
    return result;
}

[[nodiscard]] bool source_fits_studio_bundle_capability(
    const std::size_t source_bytes,
    const GoldSrcStudioModelBundleImportLimits& limits) noexcept
{
    const auto bytes = static_cast<std::uint64_t>(source_bytes);
    return source_bytes <= limits.studio.maximum_main_source_bytes &&
           bytes <= static_cast<std::uint64_t>(
                        limits.studio.maximum_total_bundle_bytes) &&
           bytes <= limits.bundle.maximum_total_source_bytes;
}

[[nodiscard]] assets::ModelAssetResult import_selected_studio_with_limits(
    const assets::IModelImporter& selected_importer,
    const assets::AssetSource& source,
    const studio::GoldSrcStudioModelImportLimits& limits)
{
    const auto* constrained = dynamic_cast<
        const studio::IGoldSrcStudioModelImporterWithLimits*>(
            &selected_importer);
    if (constrained == nullptr) {
        return assets::ModelAssetResult::failure(assets::AssetError{
            assets::AssetErrorCode::ImportFailed,
            source.virtual_path(),
            std::string{studio::kGoldSrcStudioModelImporterId},
            "Selected production-ID Studio importer does not support caller-owned limits",
            {},
        });
    }
    return constrained->import_with_limits(source, limits);
}

} // namespace

bool valid_goldsrc_visual_asset_import_limits(
    const GoldSrcVisualAssetImportLimits& limits) noexcept
{
    return valid_goldsrc_studio_model_bundle_import_limits(
        limits.studio_bundle);
}

GoldSrcVisualAssetImportResult::GoldSrcVisualAssetImportResult(
    const std::size_t wire_ordinal,
    const ResourceType resource_type,
    const std::uint16_t resource_index,
    assets::ImportedAsset asset,
    const assets::AssetImporterCategory selected_category,
    std::string selected_importer_id,
    std::vector<assets::AssetDispatchProbeCandidate> top_candidates,
    std::optional<GoldSrcStudioModelSourceBundle> studio_sources,
    const GoldSrcStudioModelDependencyStatistics dependency_statistics,
    std::vector<assets::AssetSourceFingerprint> source_fingerprints,
    const PrecacheManifestCompatibilityProfile compatibility_profile,
    const PrecacheManifestEvidenceProfile evidence_profile)
    : wire_ordinal_{wire_ordinal},
      resource_type_{resource_type},
      resource_index_{resource_index},
      asset_{std::move(asset)},
      selected_category_{selected_category},
      selected_importer_id_{std::move(selected_importer_id)},
      top_candidates_{std::move(top_candidates)},
      studio_sources_{std::move(studio_sources)},
      dependency_statistics_{dependency_statistics},
      source_fingerprints_{std::move(source_fingerprints)},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

const assets::ImportedAsset& GoldSrcVisualAssetImportResult::asset()
    const noexcept
{
    return asset_;
}

std::size_t GoldSrcVisualAssetImportResult::wire_ordinal() const noexcept
{
    return wire_ordinal_;
}

ResourceType GoldSrcVisualAssetImportResult::resource_type() const noexcept
{
    return resource_type_;
}

std::uint16_t GoldSrcVisualAssetImportResult::resource_index() const noexcept
{
    return resource_index_;
}

assets::AssetImporterCategory
GoldSrcVisualAssetImportResult::selected_category() const noexcept
{
    return selected_category_;
}

std::string_view GoldSrcVisualAssetImportResult::selected_importer_id()
    const noexcept
{
    return selected_importer_id_;
}

const std::vector<assets::AssetDispatchProbeCandidate>&
GoldSrcVisualAssetImportResult::top_candidates() const noexcept
{
    return top_candidates_;
}

const std::optional<GoldSrcStudioModelSourceBundle>&
GoldSrcVisualAssetImportResult::studio_sources() const noexcept
{
    return studio_sources_;
}

const GoldSrcStudioModelDependencyStatistics&
GoldSrcVisualAssetImportResult::dependency_statistics() const noexcept
{
    return dependency_statistics_;
}

std::span<const assets::AssetSourceFingerprint>
GoldSrcVisualAssetImportResult::source_fingerprints() const noexcept
{
    return source_fingerprints_;
}

PrecacheManifestCompatibilityProfile
GoldSrcVisualAssetImportResult::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

PrecacheManifestEvidenceProfile
GoldSrcVisualAssetImportResult::evidence_profile() const noexcept
{
    return evidence_profile_;
}

class GoldSrcVisualAssetImportOperation::Implementation final {
public:
    Implementation(
        assets::AssetSource source,
        const local_resources::LocalResourceRootId root_id,
        const local_resources::LocalVirtualResourceId virtual_resource_id,
        const local_resources::LocalStableFileIdentity expected_identity,
        const std::uint64_t byte_count,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const std::size_t wire_ordinal,
        const ResourceType resource_type,
        const std::uint16_t resource_index,
        const assets::AssetDispatchRole role,
        const PrecacheManifestCompatibilityProfile compatibility_profile,
        const PrecacheManifestEvidenceProfile evidence_profile,
        const assets::AssetImporterRegistries& registries,
        GoldSrcVisualAssetImportLimits limits) noexcept
        : source_{std::move(source)},
          root_id_{root_id},
          virtual_resource_id_{virtual_resource_id},
          expected_identity_{expected_identity},
          byte_count_{byte_count},
          environment_{std::move(environment)},
          wire_ordinal_{wire_ordinal},
          resource_type_{resource_type},
          resource_index_{resource_index},
          role_{role},
          compatibility_profile_{compatibility_profile},
          evidence_profile_{evidence_profile},
          registries_{&registries},
          limits_{std::move(limits)},
          state_{GoldSrcVisualAssetImportState::dispatching}
    {
    }

    void update(const GoldSrcVisualAssetImportTimePoint now) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (!started_at_) {
            started_at_ = now;
            last_update_at_ = now;
        } else if (last_update_at_ && now < *last_update_at_) {
            fail(
                GoldSrcVisualAssetImportErrorCode::time_moved_backwards,
                GoldSrcVisualAssetImportState::failed,
                "Visual asset import time moved backwards");
            return;
        } else {
            last_update_at_ = now;
        }
        if (limits_.studio_bundle.timeout &&
            now - *started_at_ >= *limits_.studio_bundle.timeout) {
            if (studio_operation_) {
                studio_operation_->cancel();
            }
            fail(
                GoldSrcVisualAssetImportErrorCode::timed_out,
                GoldSrcVisualAssetImportState::timed_out,
                "Visual asset import exceeded its caller-provided deadline");
            return;
        }

        try {
            if (state_ == GoldSrcVisualAssetImportState::dispatching) {
                dispatch();
                return;
            }
            if (state_ ==
                GoldSrcVisualAssetImportState::importing_studio_bundle) {
                update_studio_bundle(now);
            }
        } catch (const std::bad_alloc&) {
            fail(
                GoldSrcVisualAssetImportErrorCode::unable_to_retain_state,
                GoldSrcVisualAssetImportState::failed,
                "Unable to retain bounded visual asset import state");
        } catch (const std::length_error&) {
            fail(
                GoldSrcVisualAssetImportErrorCode::unable_to_retain_state,
                GoldSrcVisualAssetImportState::failed,
                "Visual asset import state exceeds an owning container limit");
        } catch (...) {
            fail(
                GoldSrcVisualAssetImportErrorCode::unable_to_retain_state,
                GoldSrcVisualAssetImportState::failed,
                "Unexpected failure while advancing visual asset import");
        }
    }

    void cancel() noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (studio_operation_) {
            studio_operation_->cancel();
        }
        fail(
            GoldSrcVisualAssetImportErrorCode::cancelled,
            GoldSrcVisualAssetImportState::cancelled,
            "Visual asset import was cancelled");
    }

    void dispatch()
    {
        if (!source_ || registries_ == nullptr) {
            fail(
                GoldSrcVisualAssetImportErrorCode::approved_source_invalid,
                GoldSrcVisualAssetImportState::failed,
                "Approved visual asset source is unavailable");
            return;
        }
        const assets::AssetImporterDispatcher dispatcher{*registries_};
        auto selection = dispatcher.select(*source_, role_);
        const bool selected_studio =
            selection.selected() &&
            selection.selected_category ==
                assets::AssetImporterCategory::model &&
            selection.selected_importer_id == qualified_studio_importer_id();
        auto dispatched = selected_studio
                              ? dispatcher.import_selected_model_with(
                                    *source_,
                                    std::move(selection),
                                    [this](
                                        const assets::IModelImporter& importer,
                                        const assets::AssetSource& source) {
                                        return import_selected_studio_with_limits(
                                            importer,
                                            source,
                                            limits_.studio_bundle.studio);
                                    })
                              : dispatcher.import_selected(
                                    *source_, std::move(selection));
        selected_category_ = dispatched.selected_category;
        selected_importer_id_ = std::move(dispatched.selected_importer_id);
        top_candidates_ = std::move(dispatched.top_candidates);
        if (dispatched.imported() && dispatched.asset) {
            const GoldSrcStudioModelDependencyStatistics statistics{
                1U,
                0U,
                static_cast<std::uint64_t>(source_->bytes().size()),
                false};
            std::vector<assets::AssetSourceFingerprint> fingerprints;
            fingerprints.push_back(
                goldsrc_studio_source_fingerprint(source_->bytes()));
            result_.emplace(
                wire_ordinal_,
                resource_type_,
                resource_index_,
                std::move(*dispatched.asset),
                selected_category_,
                std::move(selected_importer_id_),
                std::move(top_candidates_),
                std::nullopt,
                statistics,
                std::move(fingerprints),
                compatibility_profile_,
                evidence_profile_);
            source_.reset();
            studio_operation_.reset();
            state_ = GoldSrcVisualAssetImportState::asset_ready;
            return;
        }

        const auto asset_code = dispatched.error
                                    ? std::optional{dispatched.error->code}
                                    : std::nullopt;
        if (asset_code == assets::AssetErrorCode::
                              ExternalDependencyRequired) {
            if (selected_studio) {
                begin_studio_bundle(dispatched.state, *asset_code);
                return;
            }
            fail(
                GoldSrcVisualAssetImportErrorCode::
                    unsupported_external_dependency,
                GoldSrcVisualAssetImportState::dependency_missing,
                "Selected visual importer requires an unsupported external dependency",
                dispatched.state,
                asset_code);
            return;
        }

        fail(
            GoldSrcVisualAssetImportErrorCode::dispatch_failed,
            GoldSrcVisualAssetImportState::failed,
            "Approved model-or-sprite dispatch failed",
            dispatched.state,
            asset_code);
    }

    void begin_studio_bundle(
        const assets::AssetDispatchState dispatch_state,
        const assets::AssetErrorCode asset_code)
    {
        if (!source_ || !environment_ ||
            !source_fits_studio_bundle_capability(
                source_->bytes().size(), limits_.studio_bundle)) {
            fail(
                GoldSrcVisualAssetImportErrorCode::
                    studio_bundle_begin_failed,
                GoldSrcVisualAssetImportState::failed,
                "Selected Studio dependency source exceeds caller-configured composition limits",
                dispatch_state,
                asset_code);
            return;
        }

        auto begun = GoldSrcStudioModelBundleImportOperation::
            begin_with_retained_main_evidence(
                *source_,
                root_id_,
                virtual_resource_id_,
                expected_identity_,
                byte_count_,
                environment_,
                limits_.studio_bundle);
        if (!begun || !begun.operation) {
            const auto bundle_code = begun.error
                                         ? std::optional{begun.error->code}
                                         : std::nullopt;
            const bool stale_or_missing_main =
                bundle_code == GoldSrcStudioModelBundleImportErrorCode::
                                   approved_source_invalid ||
                bundle_code == GoldSrcStudioModelBundleImportErrorCode::
                                   invalid_main_virtual_name;
            fail(
                stale_or_missing_main
                    ? GoldSrcVisualAssetImportErrorCode::dependency_invalid
                    : GoldSrcVisualAssetImportErrorCode::
                          studio_bundle_begin_failed,
                stale_or_missing_main
                    ? GoldSrcVisualAssetImportState::dependency_invalid
                    : GoldSrcVisualAssetImportState::failed,
                stale_or_missing_main
                    ? "Selected Studio dependency source no longer matches its exact-root approval evidence"
                    : "Unable to retain the lazy exact-root Studio composition operation",
                dispatch_state,
                asset_code,
                bundle_code);
            return;
        }
        studio_operation_.emplace(std::move(*begun.operation));
        state_ = GoldSrcVisualAssetImportState::importing_studio_bundle;
    }

    void update_studio_bundle(const GoldSrcVisualAssetImportTimePoint now)
    {
        if (!studio_operation_) {
            fail(
                GoldSrcVisualAssetImportErrorCode::studio_bundle_begin_failed,
                GoldSrcVisualAssetImportState::failed,
                "Studio bundle import operation is unavailable");
            return;
        }
        studio_operation_->update(now);
        switch (studio_operation_->state()) {
        case GoldSrcStudioModelBundleImportState::model_ready: {
            auto imported = studio_operation_->take_result();
            if (!imported) {
                fail(
                    GoldSrcVisualAssetImportErrorCode::
                        unable_to_retain_state,
                    GoldSrcVisualAssetImportState::failed,
                    "Studio bundle operation published no owning result");
                return;
            }
            std::optional<GoldSrcStudioModelSourceBundle> sources;
            const auto statistics = imported->sources().statistics();
            std::vector<assets::AssetSourceFingerprint> fingerprints;
            fingerprints.reserve(statistics.source_count);
            fingerprints.push_back(imported->sources().main_fingerprint());
            if (imported->sources().texture_fingerprint()) {
                fingerprints.push_back(
                    *imported->sources().texture_fingerprint());
            }
            for (const auto& group :
                 imported->sources().sequence_group_sources()) {
                fingerprints.push_back(group.fingerprint);
            }
            sources.emplace(std::move(*imported).sources());
            assets::ImportedAsset asset{
                std::in_place_type<assets::ModelAsset>,
                std::move(*imported).model()};
            result_.emplace(
                wire_ordinal_,
                resource_type_,
                resource_index_,
                std::move(asset),
                selected_category_,
                std::move(selected_importer_id_),
                std::move(top_candidates_),
                std::move(sources),
                statistics,
                std::move(fingerprints),
                compatibility_profile_,
                evidence_profile_);
            source_.reset();
            studio_operation_.reset();
            state_ = GoldSrcVisualAssetImportState::asset_ready;
            return;
        }
        case GoldSrcStudioModelBundleImportState::dependency_missing:
            fail_from_studio(
                GoldSrcVisualAssetImportErrorCode::dependency_missing,
                GoldSrcVisualAssetImportState::dependency_missing);
            return;
        case GoldSrcStudioModelBundleImportState::dependency_invalid:
            fail_from_studio(
                GoldSrcVisualAssetImportErrorCode::dependency_invalid,
                GoldSrcVisualAssetImportState::dependency_invalid);
            return;
        case GoldSrcStudioModelBundleImportState::cancelled:
            fail_from_studio(
                GoldSrcVisualAssetImportErrorCode::cancelled,
                GoldSrcVisualAssetImportState::cancelled);
            return;
        case GoldSrcStudioModelBundleImportState::timed_out:
            fail_from_studio(
                GoldSrcVisualAssetImportErrorCode::timed_out,
                GoldSrcVisualAssetImportState::timed_out);
            return;
        case GoldSrcStudioModelBundleImportState::failed:
            fail_from_studio(
                GoldSrcVisualAssetImportErrorCode::dispatch_failed,
                GoldSrcVisualAssetImportState::failed);
            return;
        case GoldSrcStudioModelBundleImportState::idle:
        case GoldSrcStudioModelBundleImportState::inspecting_main:
        case GoldSrcStudioModelBundleImportState::planning_dependencies:
        case GoldSrcStudioModelBundleImportState::
            opening_texture_companion:
        case GoldSrcStudioModelBundleImportState::opening_sequence_group:
        case GoldSrcStudioModelBundleImportState::validating_bundle:
        case GoldSrcStudioModelBundleImportState::importing_model:
            return;
        }
    }

    void fail_from_studio(
        const GoldSrcVisualAssetImportErrorCode code,
        const GoldSrcVisualAssetImportState state) noexcept
    {
        const auto* nested =
            studio_operation_ ? studio_operation_->error() : nullptr;
        fail(
            code,
            state,
            state == GoldSrcVisualAssetImportState::dependency_missing
                ? "A required exact-root Studio dependency is missing"
                : state == GoldSrcVisualAssetImportState::dependency_invalid
                    ? "A required exact-root Studio dependency is invalid"
                    : "Studio bundle import failed",
            assets::AssetDispatchState::import_failed,
            nested ? nested->asset_code : std::nullopt,
            nested ? std::optional{nested->code} : std::nullopt);
    }

    void fail(
        const GoldSrcVisualAssetImportErrorCode code,
        const GoldSrcVisualAssetImportState terminal,
        const std::string_view context,
        const std::optional<assets::AssetDispatchState> dispatch_state =
            std::nullopt,
        const std::optional<assets::AssetErrorCode> asset_code = std::nullopt,
        const std::optional<GoldSrcStudioModelBundleImportErrorCode>
            studio_bundle_code = std::nullopt) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (studio_operation_ && !studio_operation_->terminal()) {
            studio_operation_->cancel();
        }
        state_ = terminal;
        source_.reset();
        studio_operation_.reset();
        result_.reset();
        error_.reset();
        try {
            error_.emplace();
            error_->code = code;
            error_->dispatch_state = dispatch_state;
            error_->asset_code = asset_code;
            error_->studio_bundle_code = studio_bundle_code;
            const auto bounded = context.substr(
                0U,
                (std::min)(context.size(),
                    kVisualImportDiagnosticTextLimit));
            error_->context.assign(bounded.data(), bounded.size());
        } catch (...) {
        }
    }

public:
    std::optional<assets::AssetSource> source_;
    local_resources::LocalResourceRootId root_id_;
    local_resources::LocalVirtualResourceId virtual_resource_id_;
    local_resources::LocalStableFileIdentity expected_identity_;
    std::uint64_t byte_count_{0U};
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    std::size_t wire_ordinal_{0U};
    ResourceType resource_type_{ResourceType::sound};
    std::uint16_t resource_index_{0U};
    assets::AssetDispatchRole role_{assets::AssetDispatchRole::unsupported};
    PrecacheManifestCompatibilityProfile compatibility_profile_{
        PrecacheManifestCompatibilityProfile::
            stock_protocol_48_standard_metadata_only};
    PrecacheManifestEvidenceProfile evidence_profile_{
        PrecacheManifestEvidenceProfile::
            exact_correlated_local_resource_metadata};
    const assets::AssetImporterRegistries* registries_{nullptr};
    GoldSrcVisualAssetImportLimits limits_;
    std::optional<GoldSrcStudioModelBundleImportOperation> studio_operation_;
    GoldSrcVisualAssetImportState state_{GoldSrcVisualAssetImportState::idle};
    std::optional<GoldSrcVisualAssetImportTimePoint> started_at_;
    std::optional<GoldSrcVisualAssetImportTimePoint> last_update_at_;
    assets::AssetImporterCategory selected_category_{
        assets::AssetImporterCategory::none};
    std::string selected_importer_id_;
    std::vector<assets::AssetDispatchProbeCandidate> top_candidates_;
    std::optional<GoldSrcVisualAssetImportResult> result_;
    std::optional<GoldSrcVisualAssetImportError> error_;
};

GoldSrcVisualAssetImportBeginResult GoldSrcVisualAssetImportOperation::begin(
    const local_assets::LocalAssetSource& source,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    const assets::AssetImporterRegistries& registries,
    GoldSrcVisualAssetImportLimits limits)
{
    if (!valid_goldsrc_visual_asset_import_limits(limits) ||
        environment == nullptr || environment->root_count() == 0U) {
        return begin_failure(
            GoldSrcVisualAssetImportErrorCode::invalid_configuration,
            "Visual asset import limits or local environment are invalid");
    }
    if (source.byte_count() != source.source().bytes().size() ||
        !source.expected_identity().valid() ||
        !environment->root_metadata(source.root_id())) {
        return begin_failure(
            GoldSrcVisualAssetImportErrorCode::approved_source_invalid,
            "Verified local source is not a valid bounded model-or-sprite source");
    }
    try {
        auto implementation = std::make_unique<Implementation>(
            source.source(),
            source.root_id(),
            source.virtual_resource_id(),
            source.expected_identity(),
            source.byte_count(),
            environment,
            0U,
            ResourceType::model,
            std::uint16_t{0U},
            assets::AssetDispatchRole::model_or_sprite,
            PrecacheManifestCompatibilityProfile::
                stock_protocol_48_standard_metadata_only,
            PrecacheManifestEvidenceProfile::
                exact_correlated_local_resource_metadata,
            registries,
            std::move(limits));
        GoldSrcVisualAssetImportBeginResult result;
        result.operation.emplace(
            GoldSrcVisualAssetImportOperation{std::move(implementation)});
        return result;
    } catch (...) {
        return begin_failure(
            GoldSrcVisualAssetImportErrorCode::unable_to_retain_state,
            "Unable to retain verified visual asset import prerequisites");
    }
}

GoldSrcVisualAssetImportBeginResult GoldSrcVisualAssetImportOperation::begin(
    const ApprovedAssetSource& approved_source,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    const assets::AssetImporterRegistries& registries,
    GoldSrcVisualAssetImportLimits limits)
{
    if (!valid_goldsrc_visual_asset_import_limits(limits) ||
        environment == nullptr || environment->root_count() == 0U) {
        return begin_failure(
            GoldSrcVisualAssetImportErrorCode::invalid_configuration,
            "Visual asset import limits or local environment are invalid");
    }
    if (approved_source.role() !=
            assets::AssetDispatchRole::model_or_sprite ||
        approved_source.byte_count() != approved_source.source().bytes().size() ||
        !approved_source.expected_identity().valid() ||
        !environment->root_metadata(approved_source.root_id())) {
        return begin_failure(
            GoldSrcVisualAssetImportErrorCode::approved_source_invalid,
            "Approved source is not a valid bounded model-or-sprite source");
    }
    try {
        auto implementation = std::make_unique<Implementation>(
            approved_source.source(),
            approved_source.root_id(),
            approved_source.virtual_resource_id(),
            approved_source.expected_identity(),
            approved_source.byte_count(),
            environment,
            approved_source.wire_ordinal(),
            approved_source.resource_type(),
            approved_source.resource_index(),
            approved_source.role(),
            approved_source.compatibility_profile(),
            approved_source.evidence_profile(),
            registries,
            std::move(limits));
        GoldSrcVisualAssetImportBeginResult result;
        result.operation.emplace(
            GoldSrcVisualAssetImportOperation{std::move(implementation)});
        return result;
    } catch (...) {
        return begin_failure(
            GoldSrcVisualAssetImportErrorCode::unable_to_retain_state,
            "Unable to retain approved visual asset import prerequisites");
    }
}

GoldSrcVisualAssetImportOperation::GoldSrcVisualAssetImportOperation(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_{std::move(implementation)}
{
}

GoldSrcVisualAssetImportOperation::~GoldSrcVisualAssetImportOperation() =
    default;
GoldSrcVisualAssetImportOperation::GoldSrcVisualAssetImportOperation(
    GoldSrcVisualAssetImportOperation&&) noexcept = default;
GoldSrcVisualAssetImportOperation&
GoldSrcVisualAssetImportOperation::operator=(
    GoldSrcVisualAssetImportOperation&&) noexcept = default;

void GoldSrcVisualAssetImportOperation::update(
    const GoldSrcVisualAssetImportTimePoint now) noexcept
{
    implementation_->update(now);
}

void GoldSrcVisualAssetImportOperation::cancel() noexcept
{
    implementation_->cancel();
}

GoldSrcVisualAssetImportState GoldSrcVisualAssetImportOperation::state()
    const noexcept
{
    return implementation_->state_;
}

bool GoldSrcVisualAssetImportOperation::terminal() const noexcept
{
    return terminal_state(implementation_->state_);
}

const GoldSrcVisualAssetImportResult* GoldSrcVisualAssetImportOperation::result()
    const noexcept
{
    return implementation_->result_ ? &*implementation_->result_ : nullptr;
}

const GoldSrcVisualAssetImportError* GoldSrcVisualAssetImportOperation::error()
    const noexcept
{
    return implementation_->error_ ? &*implementation_->error_ : nullptr;
}

std::optional<GoldSrcVisualAssetImportResult>
GoldSrcVisualAssetImportOperation::take_result() noexcept
{
    if (implementation_->state_ != GoldSrcVisualAssetImportState::asset_ready ||
        !implementation_->result_) {
        return std::nullopt;
    }
    return std::exchange(implementation_->result_, std::nullopt);
}

} // namespace hlclient::goldsrc::visual_assets
