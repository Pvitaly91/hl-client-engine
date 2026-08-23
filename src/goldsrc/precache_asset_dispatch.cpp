#include <hlclient/goldsrc/precache_asset_dispatch.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] AssetDispatchPlanBuildResult plan_failure(
    const AssetDispatchPlanErrorCode code,
    const std::optional<std::size_t> wire_ordinal,
    const std::string_view context) noexcept
{
    AssetDispatchPlanBuildResult result;
    try {
        result.error.emplace();
        result.error->code = code;
        result.error->wire_ordinal = wire_ordinal;
        const auto bounded = context.substr(
            0U,
            (std::min)(context.size(),
                       kPrecacheAssetDispatchDiagnosticTextLimit));
        result.error->context.assign(bounded.data(), bounded.size());
    } catch (...) {
    }
    return result;
}

[[nodiscard]] ApprovedAssetSourceCreateResult source_failure(
    const ApprovedAssetSourceErrorCode code,
    const std::optional<std::size_t> wire_ordinal,
    const std::string_view context) noexcept
{
    ApprovedAssetSourceCreateResult result;
    try {
        result.error.emplace();
        result.error->code = code;
        result.error->wire_ordinal = wire_ordinal;
        const auto bounded = context.substr(
            0U,
            (std::min)(context.size(),
                       kPrecacheAssetDispatchDiagnosticTextLimit));
        result.error->context.assign(bounded.data(), bounded.size());
    } catch (...) {
    }
    return result;
}

[[nodiscard]] bool source_backed_role(
    const assets::AssetDispatchRole role) noexcept
{
    return role == assets::AssetDispatchRole::world ||
           role == assets::AssetDispatchRole::model_or_sprite ||
           role == assets::AssetDispatchRole::audio;
}

[[nodiscard]] bool valid_source_backed_plan_role(
    const AssetDispatchPlan& plan) noexcept
{
    const auto categories = plan.allowed_importer_categories();
    switch (plan.role()) {
    case assets::AssetDispatchRole::world:
        return plan.selected_world() &&
               plan.resource_type() == ResourceType::model &&
               categories.size() == 1U &&
               categories[0U] == assets::AssetImporterCategory::world;
    case assets::AssetDispatchRole::model_or_sprite:
        return !plan.selected_world() &&
               plan.resource_type() == ResourceType::model &&
               categories.size() == 2U &&
               categories[0U] == assets::AssetImporterCategory::model &&
               categories[1U] == assets::AssetImporterCategory::sprite;
    case assets::AssetDispatchRole::audio:
        return !plan.selected_world() &&
               plan.resource_type() == ResourceType::sound &&
               categories.size() == 1U &&
               categories[0U] == assets::AssetImporterCategory::audio;
    case assets::AssetDispatchRole::metadata_only:
    case assets::AssetDispatchRole::unsupported:
        return false;
    }
    return false;
}

[[nodiscard]] ApprovedAssetSourceOpenErrorCode map_open_error(
    const local_assets::LocalAssetSourceOpenErrorCode code) noexcept
{
    using LocalCode = local_assets::LocalAssetSourceOpenErrorCode;
    switch (code) {
    case LocalCode::invalid_configuration:
    case LocalCode::open_source_limit_reached:
        return ApprovedAssetSourceOpenErrorCode::invalid_configuration;
    case LocalCode::locator_invalid:
        return ApprovedAssetSourceOpenErrorCode::locator_invalid;
    case LocalCode::locator_environment_mismatch:
        return ApprovedAssetSourceOpenErrorCode::locator_environment_mismatch;
    case LocalCode::locator_target_missing:
        return ApprovedAssetSourceOpenErrorCode::locator_target_missing;
    case LocalCode::stale_locator:
        return ApprovedAssetSourceOpenErrorCode::stale_locator;
    case LocalCode::source_too_large:
        return ApprovedAssetSourceOpenErrorCode::source_too_large;
    case LocalCode::source_read_failed:
        return ApprovedAssetSourceOpenErrorCode::source_read_failed;
    case LocalCode::source_changed_during_read:
        return ApprovedAssetSourceOpenErrorCode::source_changed_during_read;
    case LocalCode::source_creation_failed:
        return ApprovedAssetSourceOpenErrorCode::source_creation_failed;
    case LocalCode::cancelled:
        return ApprovedAssetSourceOpenErrorCode::cancelled;
    case LocalCode::timed_out:
        return ApprovedAssetSourceOpenErrorCode::timed_out;
    }
    return ApprovedAssetSourceOpenErrorCode::invalid_configuration;
}

[[nodiscard]] ApprovedAssetSourceOpenState map_open_state(
    const local_assets::LocalAssetSourceOpenState state) noexcept
{
    using LocalState = local_assets::LocalAssetSourceOpenState;
    switch (state) {
    case LocalState::idle: return ApprovedAssetSourceOpenState::idle;
    case LocalState::opening: return ApprovedAssetSourceOpenState::opening;
    case LocalState::reading: return ApprovedAssetSourceOpenState::reading;
    case LocalState::validating: return ApprovedAssetSourceOpenState::validating;
    case LocalState::source_ready:
        return ApprovedAssetSourceOpenState::source_ready;
    case LocalState::cancelled: return ApprovedAssetSourceOpenState::cancelled;
    case LocalState::timed_out: return ApprovedAssetSourceOpenState::timed_out;
    case LocalState::failed: return ApprovedAssetSourceOpenState::failed;
    }
    return ApprovedAssetSourceOpenState::failed;
}

} // namespace

AssetDispatchPlan::AssetDispatchPlan(
    PrecacheManifestEntry entry,
    const std::size_t wire_ordinal,
    const ResourceType resource_type,
    const std::uint16_t resource_index,
    const assets::AssetDispatchRole role,
    std::array<assets::AssetImporterCategory,
               kMaximumAssetDispatchPlanCategories> allowed_categories,
    const std::size_t allowed_category_count,
    const bool selected_world,
    const PrecacheManifestCompatibilityProfile compatibility_profile,
    const PrecacheManifestEvidenceProfile evidence_profile) noexcept
    : entry_{std::move(entry)},
      wire_ordinal_{wire_ordinal},
      resource_type_{resource_type},
      resource_index_{resource_index},
      role_{role},
      allowed_categories_{std::move(allowed_categories)},
      allowed_category_count_{allowed_category_count},
      selected_world_{selected_world},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

std::size_t AssetDispatchPlan::wire_ordinal() const noexcept
{
    return wire_ordinal_;
}

ResourceType AssetDispatchPlan::resource_type() const noexcept
{
    return resource_type_;
}

std::uint16_t AssetDispatchPlan::resource_index() const noexcept
{
    return resource_index_;
}

assets::AssetDispatchRole AssetDispatchPlan::role() const noexcept
{
    return role_;
}

std::span<const assets::AssetImporterCategory>
AssetDispatchPlan::allowed_importer_categories() const noexcept
{
    return std::span<const assets::AssetImporterCategory>{allowed_categories_}
        .first(allowed_category_count_);
}

bool AssetDispatchPlan::selected_world() const noexcept
{
    return selected_world_;
}

PrecacheManifestCompatibilityProfile
AssetDispatchPlan::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

PrecacheManifestEvidenceProfile AssetDispatchPlan::evidence_profile()
    const noexcept
{
    return evidence_profile_;
}

AssetDispatchPlanBuildResult AssetDispatchPlanBuilder::build(
    const PrecacheManifestState& manifest,
    const PrecacheManifestEntry& entry) const noexcept
{
    bool entry_belongs_to_manifest = false;
    for (const auto& candidate : manifest.entries()) {
        if (&candidate == &entry) {
            entry_belongs_to_manifest = true;
            break;
        }
    }
    if (!entry_belongs_to_manifest) {
        return plan_failure(
            AssetDispatchPlanErrorCode::entry_not_in_manifest,
            entry.wire_ordinal(),
            "Asset dispatch entry does not belong to the supplied manifest");
    }

    const bool selected_world = manifest.world_entry() == &entry;
    if (selected_world &&
        (entry.resource_type() != ResourceType::model ||
         manifest.world_selection().wire_ordinal() != entry.wire_ordinal() ||
         manifest.world_selection().resource_index() !=
             std::optional{entry.resource_index()})) {
        return plan_failure(
            AssetDispatchPlanErrorCode::world_selection_invariant_failed,
            entry.wire_ordinal(),
            "Exact selected-world metadata disagrees with the manifest entry");
    }

    auto role = assets::AssetDispatchRole::unsupported;
    std::array<assets::AssetImporterCategory,
               kMaximumAssetDispatchPlanCategories> categories{
        assets::AssetImporterCategory::none,
        assets::AssetImporterCategory::none,
    };
    std::size_t category_count = 0U;
    switch (entry.resource_type()) {
    case ResourceType::model:
        if (selected_world) {
            role = assets::AssetDispatchRole::world;
            categories[0U] = assets::AssetImporterCategory::world;
            category_count = 1U;
        } else {
            role = assets::AssetDispatchRole::model_or_sprite;
            categories[0U] = assets::AssetImporterCategory::model;
            categories[1U] = assets::AssetImporterCategory::sprite;
            category_count = 2U;
        }
        break;
    case ResourceType::sound:
        role = assets::AssetDispatchRole::audio;
        categories[0U] = assets::AssetImporterCategory::audio;
        category_count = 1U;
        break;
    case ResourceType::decal:
        role = assets::AssetDispatchRole::metadata_only;
        break;
    case ResourceType::generic:
    case ResourceType::event_script:
        role = assets::AssetDispatchRole::unsupported;
        break;
    }

    AssetDispatchPlanBuildResult result;
    try {
        PrecacheManifestEntry retained_entry{entry};
        AssetDispatchPlan plan{
            std::move(retained_entry),
            entry.wire_ordinal(),
            entry.resource_type(),
            entry.resource_index(),
            role,
            categories,
            category_count,
            selected_world,
            entry.compatibility_profile(),
            entry.evidence_profile()};
        result.plan.emplace(std::move(plan));
    } catch (...) {
        return plan_failure(
            AssetDispatchPlanErrorCode::unable_to_retain_plan,
            entry.wire_ordinal(),
            "Unable to retain the immutable asset dispatch plan");
    }
    return result;
}

ApprovedAssetSource::ApprovedAssetSource(
    local_assets::LocalAssetSource source,
    const std::size_t wire_ordinal,
    const ResourceType resource_type,
    const std::uint16_t resource_index,
    const assets::AssetDispatchRole role,
    const PrecacheManifestCompatibilityProfile compatibility_profile,
    const PrecacheManifestEvidenceProfile evidence_profile) noexcept
    : source_{std::move(source)},
      wire_ordinal_{wire_ordinal},
      resource_type_{resource_type},
      resource_index_{resource_index},
      role_{role},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

ApprovedAssetSourceCreateResult ApprovedAssetSource::create(
    const AssetDispatchPlan& plan,
    local_assets::LocalAssetSource source) noexcept
{
    const auto& entry = plan.entry_;
    if (entry.readiness_status() !=
        LocalResourceReadinessStatus::ready_local_file) {
        return source_failure(
            ApprovedAssetSourceErrorCode::entry_not_ready,
            entry.wire_ordinal(),
            "Only a ready local manifest entry may approve an asset source");
    }
    if (!entry.locator()) {
        return source_failure(
            ApprovedAssetSourceErrorCode::locator_missing,
            entry.wire_ordinal(),
            "Ready manifest entry has no approved local locator");
    }
    if (!source_backed_role(plan.role())) {
        return source_failure(
            ApprovedAssetSourceErrorCode::unsupported_dispatch_role,
            entry.wire_ordinal(),
            "Metadata-only or unsupported roles may not open asset sources");
    }
    if (plan.wire_ordinal() != entry.wire_ordinal() ||
        plan.resource_type() != entry.resource_type() ||
        plan.resource_index() != entry.resource_index() ||
        plan.compatibility_profile() != entry.compatibility_profile() ||
        plan.evidence_profile() != entry.evidence_profile()) {
        return source_failure(
            ApprovedAssetSourceErrorCode::dispatch_plan_mismatch,
            entry.wire_ordinal(),
            "Asset dispatch plan does not describe the selected manifest entry");
    }

    const auto& locator = *entry.locator();
    const auto source_bytes = source.source().bytes().size();
    if (source.root_id() != locator.root_id() ||
        source.virtual_resource_id() != locator.virtual_name().id() ||
        source.expected_identity() != locator.expected_identity() ||
        source.byte_count() != locator.expected_file_size() ||
        source.locator_compatibility_profile() !=
            locator.compatibility_profile() ||
        source.byte_count() >
            static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) ||
        source_bytes != static_cast<std::size_t>(source.byte_count())) {
        return source_failure(
            ApprovedAssetSourceErrorCode::approved_source_mismatch,
            entry.wire_ordinal(),
            "Opened local source does not match the approved locator metadata");
    }

    ApprovedAssetSourceCreateResult result;
    try {
        ApprovedAssetSource source_result{
            std::move(source),
            entry.wire_ordinal(),
            entry.resource_type(),
            entry.resource_index(),
            plan.role(),
            entry.compatibility_profile(),
            entry.evidence_profile()};
        result.source.emplace(std::move(source_result));
    } catch (...) {
        return source_failure(
            ApprovedAssetSourceErrorCode::unable_to_retain_source,
            entry.wire_ordinal(),
            "Unable to retain the approved owning asset source");
    }
    return result;
}

const assets::AssetSource& ApprovedAssetSource::source() const noexcept
{
    return source_.source();
}

std::size_t ApprovedAssetSource::wire_ordinal() const noexcept
{
    return wire_ordinal_;
}

ResourceType ApprovedAssetSource::resource_type() const noexcept
{
    return resource_type_;
}

std::uint16_t ApprovedAssetSource::resource_index() const noexcept
{
    return resource_index_;
}

local_resources::LocalResourceRootId ApprovedAssetSource::root_id()
    const noexcept
{
    return source_.root_id();
}

local_resources::LocalVirtualResourceId
ApprovedAssetSource::virtual_resource_id() const noexcept
{
    return source_.virtual_resource_id();
}

local_resources::LocalStableFileIdentity
ApprovedAssetSource::expected_identity() const noexcept
{
    return source_.expected_identity();
}

std::uint64_t ApprovedAssetSource::byte_count() const noexcept
{
    return source_.byte_count();
}

assets::AssetDispatchRole ApprovedAssetSource::role() const noexcept
{
    return role_;
}

PrecacheManifestCompatibilityProfile
ApprovedAssetSource::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

PrecacheManifestEvidenceProfile ApprovedAssetSource::evidence_profile()
    const noexcept
{
    return evidence_profile_;
}

ApprovedAssetImporterDispatcher::ApprovedAssetImporterDispatcher(
    const assets::AssetImporterRegistries& registries) noexcept
    : dispatcher_{registries}
{
}

assets::AssetDispatchResult ApprovedAssetImporterDispatcher::dispatch(
    const ApprovedAssetSource& source,
    const AssetDispatchPlan& plan) const
{
    const auto& entry = plan.entry_;
    const auto& locator = entry.locator();
    const auto local_file_size = entry.local_file_size();
    const bool retained_capability_matches =
        entry.readiness_status() ==
            LocalResourceReadinessStatus::ready_local_file &&
        entry.wire_ordinal() == plan.wire_ordinal() &&
        entry.resource_type() == plan.resource_type() &&
        entry.resource_index() == plan.resource_index() &&
        entry.compatibility_profile() == plan.compatibility_profile() &&
        entry.evidence_profile() == plan.evidence_profile() &&
        locator.has_value() && local_file_size.has_value() &&
        *local_file_size == locator->expected_file_size() &&
        source.root_id() == locator->root_id() &&
        source.virtual_resource_id() == locator->virtual_name().id() &&
        source.expected_identity() == locator->expected_identity() &&
        source.byte_count() == locator->expected_file_size() &&
        source.source_.locator_compatibility_profile() ==
            locator->compatibility_profile() &&
        static_cast<std::uint64_t>(source.source().bytes().size()) ==
            source.byte_count();
    if (!valid_source_backed_plan_role(plan) ||
        !retained_capability_matches ||
        source.wire_ordinal() != plan.wire_ordinal() ||
        source.resource_type() != plan.resource_type() ||
        source.resource_index() != plan.resource_index() ||
        source.role() != plan.role() ||
        source.compatibility_profile() != plan.compatibility_profile() ||
        source.evidence_profile() != plan.evidence_profile()) {
        return assets::AssetDispatchResult{
            assets::AssetDispatchState::source_invalid,
            std::nullopt,
            assets::AssetImporterCategory::none,
            {},
            {},
            assets::AssetError{
                assets::AssetErrorCode::ImportFailed,
                source.source().virtual_path(),
                {},
                "Approved asset source and manifest dispatch plan disagree",
                {},
            },
        };
    }
    return dispatcher_.dispatch(source.source(), plan.role());
}

ApprovedAssetSourceOpenOperation::ApprovedAssetSourceOpenOperation(
    AssetDispatchPlan plan,
    local_assets::LocalAssetSourceOpenOperation operation) noexcept
    : plan_{std::move(plan)},
      operation_{std::move(operation)}
{
}

void ApprovedAssetSourceOpenOperation::update(
    const ApprovedAssetSourceOpenTimePoint now) noexcept
{
    if (state_ == ApprovedAssetSourceOpenState::source_ready ||
        state_ == ApprovedAssetSourceOpenState::cancelled ||
        state_ == ApprovedAssetSourceOpenState::timed_out ||
        state_ == ApprovedAssetSourceOpenState::failed) {
        return;
    }
    operation_.update(now);
    synchronize_from_local_operation();
}

void ApprovedAssetSourceOpenOperation::cancel() noexcept
{
    if (state_ == ApprovedAssetSourceOpenState::source_ready ||
        state_ == ApprovedAssetSourceOpenState::cancelled ||
        state_ == ApprovedAssetSourceOpenState::timed_out ||
        state_ == ApprovedAssetSourceOpenState::failed) {
        return;
    }
    operation_.cancel();
    synchronize_from_local_operation();
}

ApprovedAssetSourceOpenState ApprovedAssetSourceOpenOperation::state()
    const noexcept
{
    return state_;
}

std::uint64_t ApprovedAssetSourceOpenOperation::progress_bytes()
    const noexcept
{
    return operation_.progress_bytes();
}

const ApprovedAssetSource* ApprovedAssetSourceOpenOperation::result()
    const noexcept
{
    return result_ ? &*result_ : nullptr;
}

const ApprovedAssetSourceOpenError* ApprovedAssetSourceOpenOperation::error()
    const noexcept
{
    return error_ ? &*error_ : nullptr;
}

std::optional<ApprovedAssetSource>
ApprovedAssetSourceOpenOperation::take_result() noexcept
{
    auto result = std::move(result_);
    result_.reset();
    return result;
}

void ApprovedAssetSourceOpenOperation::synchronize_from_local_operation()
    noexcept
{
    const auto local_state = operation_.state();
    state_ = map_open_state(local_state);
    if (local_state == local_assets::LocalAssetSourceOpenState::source_ready) {
        auto local_source = operation_.take_result();
        if (!local_source) {
            ApprovedAssetSourceError error;
            error.code = ApprovedAssetSourceErrorCode::unable_to_retain_source;
            error.wire_ordinal = plan_.wire_ordinal();
            fail_from_approved_source(error);
            return;
        }
        auto approved = ApprovedAssetSource::create(
            plan_, std::move(*local_source));
        if (!approved || !approved.source) {
            if (approved.error) {
                fail_from_approved_source(*approved.error);
            } else {
                ApprovedAssetSourceError error;
                error.code =
                    ApprovedAssetSourceErrorCode::unable_to_retain_source;
                error.wire_ordinal = plan_.wire_ordinal();
                fail_from_approved_source(error);
            }
            return;
        }
        result_.emplace(std::move(*approved.source));
        error_.reset();
        state_ = ApprovedAssetSourceOpenState::source_ready;
        return;
    }
    if (local_state == local_assets::LocalAssetSourceOpenState::cancelled ||
        local_state == local_assets::LocalAssetSourceOpenState::timed_out ||
        local_state == local_assets::LocalAssetSourceOpenState::failed) {
        if (const auto* error = operation_.error()) {
            fail_from_local_operation(*error);
        } else {
            local_assets::LocalAssetSourceOpenError fallback_error;
            fallback_error.code =
                local_state == local_assets::LocalAssetSourceOpenState::cancelled
                    ? local_assets::LocalAssetSourceOpenErrorCode::cancelled
                    : local_state ==
                              local_assets::LocalAssetSourceOpenState::timed_out
                          ? local_assets::LocalAssetSourceOpenErrorCode::timed_out
                          : local_assets::LocalAssetSourceOpenErrorCode::
                                source_read_failed;
            fail_from_local_operation(fallback_error);
        }
    }
}

void ApprovedAssetSourceOpenOperation::fail_from_local_operation(
    const local_assets::LocalAssetSourceOpenError& error) noexcept
{
    result_.reset();
    error_.reset();
    try {
        error_.emplace();
        error_->code = map_open_error(error.code);
        error_->local_source_code = error.code;
        error_->locator_reopen_code = error.locator_reopen_code;
        error_->read_code = error.read_code;
        const auto bounded = std::string_view{error.context}.substr(
            0U,
            (std::min)(error.context.size(),
                       kPrecacheAssetDispatchDiagnosticTextLimit));
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
    }
    state_ = error.code ==
                     local_assets::LocalAssetSourceOpenErrorCode::cancelled
                 ? ApprovedAssetSourceOpenState::cancelled
                 : error.code ==
                           local_assets::LocalAssetSourceOpenErrorCode::timed_out
                       ? ApprovedAssetSourceOpenState::timed_out
                       : ApprovedAssetSourceOpenState::failed;
}

void ApprovedAssetSourceOpenOperation::fail_from_approved_source(
    const ApprovedAssetSourceError& error) noexcept
{
    result_.reset();
    error_.reset();
    try {
        error_.emplace();
        error_->code =
            ApprovedAssetSourceOpenErrorCode::source_creation_failed;
        error_->approved_source_code = error.code;
        const auto bounded = std::string_view{error.context}.substr(
            0U,
            (std::min)(error.context.size(),
                       kPrecacheAssetDispatchDiagnosticTextLimit));
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
    }
    state_ = ApprovedAssetSourceOpenState::failed;
}

ApprovedAssetSourceOpenBeginResult ApprovedAssetSourceOpener::begin(
    const AssetDispatchPlan& plan,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    ApprovedAssetSourceOpenLimits limits)
{
    ApprovedAssetSourceOpenBeginResult result;
    const auto& entry = plan.entry_;
    if (entry.readiness_status() !=
            LocalResourceReadinessStatus::ready_local_file ||
        !entry.locator() || !source_backed_role(plan.role()) ||
        !valid_source_backed_plan_role(plan) ||
        plan.wire_ordinal() != entry.wire_ordinal() ||
        plan.resource_type() != entry.resource_type() ||
        plan.resource_index() != entry.resource_index() ||
        plan.compatibility_profile() != entry.compatibility_profile() ||
        plan.evidence_profile() != entry.evidence_profile()) {
        result.error.emplace();
        result.error->code =
            ApprovedAssetSourceOpenErrorCode::invalid_configuration;
        result.error->context =
            "Approved source opening requires one ready matching manifest entry and plan";
        return result;
    }

    auto local_begin = opener_.begin(
        *entry.locator(), std::move(environment), std::move(limits));
    if (!local_begin || !local_begin.operation) {
        if (local_begin.error) {
            result.error.emplace();
            result.error->code = map_open_error(local_begin.error->code);
            result.error->local_source_code = local_begin.error->code;
            result.error->locator_reopen_code =
                local_begin.error->locator_reopen_code;
            result.error->read_code = local_begin.error->read_code;
            const auto bounded = std::string_view{local_begin.error->context}
                                     .substr(
                                         0U,
                                         (std::min)(
                                             local_begin.error->context.size(),
                                             kPrecacheAssetDispatchDiagnosticTextLimit));
            result.error->context.assign(bounded.data(), bounded.size());
        } else {
            result.error.emplace();
            result.error->code =
                ApprovedAssetSourceOpenErrorCode::invalid_configuration;
            result.error->context =
                "Local asset-source opener returned no operation";
        }
        return result;
    }

    try {
        ApprovedAssetSourceOpenOperation operation{
            plan, std::move(*local_begin.operation)};
        result.operation.emplace(std::move(operation));
    } catch (...) {
        result.error.emplace();
        result.error->code =
            ApprovedAssetSourceOpenErrorCode::source_creation_failed;
        result.error->context =
            "Unable to retain the approved source-open operation";
    }
    return result;
}

} // namespace hlclient::goldsrc
