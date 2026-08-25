#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_bundle_import.hpp>

#include <hlclient/assets/model_asset_types.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::visual_assets {
namespace {

inline constexpr std::size_t kBundleImportDiagnosticTextLimit = 192U;

[[nodiscard]] bool terminal_state(
    const GoldSrcStudioModelBundleImportState state) noexcept
{
    return state == GoldSrcStudioModelBundleImportState::model_ready ||
           state == GoldSrcStudioModelBundleImportState::dependency_missing ||
           state == GoldSrcStudioModelBundleImportState::dependency_invalid ||
           state == GoldSrcStudioModelBundleImportState::cancelled ||
           state == GoldSrcStudioModelBundleImportState::timed_out ||
           state == GoldSrcStudioModelBundleImportState::failed;
}

[[nodiscard]] std::string virtual_path_bytes(
    const assets::AssetSource& source)
{
    const auto encoded = source.virtual_path().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] std::uint64_t effective_total_source_bytes_limit(
    const GoldSrcStudioModelBundleImportLimits& limits) noexcept
{
    return (std::min)(
        limits.bundle.maximum_total_source_bytes,
        static_cast<std::uint64_t>(
            limits.studio.maximum_total_bundle_bytes));
}

[[nodiscard]] GoldSrcStudioModelBundleImportBeginResult begin_failure(
    const GoldSrcStudioModelBundleImportErrorCode code,
    const std::string_view context) noexcept
{
    GoldSrcStudioModelBundleImportBeginResult result;
    try {
        result.error.emplace();
        result.error->code = code;
        const auto bounded = context.substr(
            0U, (std::min)(context.size(), kBundleImportDiagnosticTextLimit));
        result.error->context.assign(bounded.data(), bounded.size());
    } catch (...) {
        result.error.reset();
        try {
            result.error.emplace();
            result.error->code =
                GoldSrcStudioModelBundleImportErrorCode::
                    unable_to_retain_state;
        } catch (...) {
        }
    }
    return result;
}

} // namespace

bool valid_goldsrc_studio_model_bundle_import_limits(
    const GoldSrcStudioModelBundleImportLimits& limits) noexcept
{
    return studio::valid_goldsrc_studio_model_import_limits(limits.studio) &&
           valid_goldsrc_studio_model_source_bundle_limits(limits.bundle) &&
           local_assets::valid_local_asset_source_open_limits(
               limits.companion_source_open) &&
           limits.companion_source_open.maximum_open_sources == 1U &&
           limits.companion_source_open.maximum_source_bytes <=
               limits.studio.maximum_companion_source_bytes &&
           (!limits.timeout ||
               (*limits.timeout > std::chrono::milliseconds::zero() &&
                *limits.timeout <=
                    kHardMaximumGoldSrcStudioModelBundleImportTimeout));
}

GoldSrcStudioModelBundleImportResult::
    GoldSrcStudioModelBundleImportResult(
        assets::ModelAsset model,
        GoldSrcStudioModelSourceBundle sources) noexcept
    : model_{std::move(model)}, sources_{std::move(sources)}
{
}

const assets::ModelAsset& GoldSrcStudioModelBundleImportResult::model()
    const & noexcept
{
    return model_;
}

const GoldSrcStudioModelSourceBundle&
GoldSrcStudioModelBundleImportResult::sources() const & noexcept
{
    return sources_;
}

assets::ModelAsset&& GoldSrcStudioModelBundleImportResult::model() && noexcept
{
    return std::move(model_);
}

GoldSrcStudioModelSourceBundle&&
GoldSrcStudioModelBundleImportResult::sources() && noexcept
{
    return std::move(sources_);
}

class GoldSrcStudioModelBundleImportOperation::Implementation final {
public:
    Implementation(
        assets::AssetSource main_source,
        local_resources::LocalResourceRootId root_id,
        local_resources::LocalVirtualResourceName main_virtual_name,
        const local_resources::LocalStableFileIdentity main_identity,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        GoldSrcStudioModelBundleImportLimits limits,
        const std::uint64_t main_source_bytes) noexcept
        : main_source_{std::move(main_source)},
          root_id_{std::move(root_id)},
          main_virtual_name_{std::move(main_virtual_name)},
          main_identity_{main_identity},
          environment_{std::move(environment)},
          limits_{std::move(limits)},
          state_{GoldSrcStudioModelBundleImportState::inspecting_main}
    {
        progress_.source_bytes_ready = main_source_bytes;
    }

    void update(const GoldSrcStudioModelBundleImportTimePoint now) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (!started_at_) {
            started_at_ = now;
            last_update_at_ = now;
        } else if (last_update_at_ && now < *last_update_at_) {
            fail(
                GoldSrcStudioModelBundleImportErrorCode::time_moved_backwards,
                GoldSrcStudioModelBundleImportState::failed,
                "Studio bundle-import time moved backwards");
            return;
        } else {
            last_update_at_ = now;
        }
        if (limits_.timeout && now - *started_at_ >= *limits_.timeout) {
            if (source_open_operation_) {
                source_open_operation_->cancel();
            }
            fail(
                GoldSrcStudioModelBundleImportErrorCode::timed_out,
                GoldSrcStudioModelBundleImportState::timed_out,
                "Studio bundle import exceeded its caller-provided deadline");
            return;
        }

        try {
            switch (state_) {
            case GoldSrcStudioModelBundleImportState::inspecting_main:
                inspect_main();
                return;
            case GoldSrcStudioModelBundleImportState::planning_dependencies:
                plan_dependencies();
                return;
            case GoldSrcStudioModelBundleImportState::
                opening_texture_companion:
            case GoldSrcStudioModelBundleImportState::opening_sequence_group:
                update_companion_open(now);
                return;
            case GoldSrcStudioModelBundleImportState::validating_bundle:
                validate_bundle();
                return;
            case GoldSrcStudioModelBundleImportState::importing_model:
                import_model();
                return;
            case GoldSrcStudioModelBundleImportState::idle:
            case GoldSrcStudioModelBundleImportState::model_ready:
            case GoldSrcStudioModelBundleImportState::dependency_missing:
            case GoldSrcStudioModelBundleImportState::dependency_invalid:
            case GoldSrcStudioModelBundleImportState::cancelled:
            case GoldSrcStudioModelBundleImportState::timed_out:
            case GoldSrcStudioModelBundleImportState::failed:
                return;
            }
        } catch (const std::bad_alloc&) {
            fail(
                GoldSrcStudioModelBundleImportErrorCode::
                    unable_to_retain_state,
                GoldSrcStudioModelBundleImportState::failed,
                "Unable to retain bounded Studio bundle-import state");
        } catch (const std::length_error&) {
            fail(
                GoldSrcStudioModelBundleImportErrorCode::
                    unable_to_retain_state,
                GoldSrcStudioModelBundleImportState::failed,
                "Studio bundle-import state exceeds an owning container limit");
        } catch (...) {
            fail(
                GoldSrcStudioModelBundleImportErrorCode::
                    unable_to_retain_state,
                GoldSrcStudioModelBundleImportState::failed,
                "Unexpected failure while advancing Studio bundle import");
        }
    }

    void cancel() noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (source_open_operation_) {
            source_open_operation_->cancel();
        }
        fail(
            GoldSrcStudioModelBundleImportErrorCode::cancelled,
            GoldSrcStudioModelBundleImportState::cancelled,
            "Studio bundle import was cancelled");
    }

    void inspect_main()
    {
        if (!main_source_) {
            fail(
                GoldSrcStudioModelBundleImportErrorCode::
                    approved_source_invalid,
                GoldSrcStudioModelBundleImportState::failed,
                "Approved Studio main source is unavailable");
            return;
        }
        auto inspected = studio::GoldSrcStudioParser::inspect_dependencies(
            main_source_->bytes(), limits_.studio);
        if (!inspected || !inspected.plan) {
            const auto studio_code =
                inspected.error
                    ? std::optional{inspected.error->code}
                    : std::nullopt;
            fail(
                GoldSrcStudioModelBundleImportErrorCode::
                    main_inspection_failed,
                GoldSrcStudioModelBundleImportState::failed,
                "Approved Studio main source failed dependency preflight",
                assets::AssetErrorCode::MalformedData,
                studio_code);
            return;
        }
        auto resolved = resolve_goldsrc_studio_dependency_plan(
            *inspected.plan,
            main_virtual_name_,
            root_id_,
            main_identity_,
            goldsrc_studio_source_fingerprint(main_source_->bytes()));
        if (!resolved || !resolved.plan) {
            fail_dependency_invalid(
                "Studio dependency preflight could not retain safe exact-root sibling names",
                resolved.error
                    ? resolved.error->sequence_group_ordinal
                    : std::nullopt);
            return;
        }
        dependency_plan_.emplace(std::move(*resolved.plan));
        progress_.expected_source_count =
            dependency_plan_->source_plan().expected_source_count;
        state_ =
            GoldSrcStudioModelBundleImportState::planning_dependencies;
    }

    void plan_dependencies()
    {
        if (!dependency_plan_) {
            fail_dependency_invalid("Studio dependency plan is unavailable");
            return;
        }
        const auto& source_plan = dependency_plan_->source_plan();
        if (source_plan.expected_source_count == 0U ||
            source_plan.expected_source_count >
                limits_.bundle.maximum_source_count ||
            source_plan.expected_source_count >
                kGoldSrcStudioBundleMaximumSources) {
            fail_dependency_invalid(
                "Studio dependency preflight produced an invalid source count");
            return;
        }
        const auto& ordinals = source_plan.required_sequence_group_ordinals;
        for (std::size_t index = 0U; index < ordinals.size(); ++index) {
            if (ordinals[index] <
                    kGoldSrcStudioMinimumExternalSequenceGroup ||
                ordinals[index] >
                    kGoldSrcStudioMaximumExternalSequenceGroup ||
                (index > 0U && ordinals[index - 1U] == ordinals[index])) {
                fail_dependency_invalid(
                    "Studio dependency preflight produced invalid sequence-group ordinals",
                    ordinals[index]);
                return;
            }
        }
        const auto expected =
            1U +
            (source_plan.texture_companion_required ? 1U : 0U) +
            ordinals.size();
        if (expected != source_plan.expected_source_count) {
            fail_dependency_invalid(
                "Studio dependency preflight source counts disagree");
            return;
        }

        if (source_plan.texture_companion_required) {
            state_ = GoldSrcStudioModelBundleImportState::
                opening_texture_companion;
            return;
        }
        if (!ordinals.empty()) {
            progress_.current_sequence_group_ordinal = ordinals.front();
            state_ = GoldSrcStudioModelBundleImportState::
                opening_sequence_group;
            return;
        }
        state_ = GoldSrcStudioModelBundleImportState::validating_bundle;
    }

    void update_companion_open(
        const GoldSrcStudioModelBundleImportTimePoint now)
    {
        if (!source_open_operation_) {
            begin_companion_open();
            return;
        }

        source_open_operation_->update(now);
        progress_.current_source_progress_bytes =
            source_open_operation_->progress_bytes();
        switch (source_open_operation_->state()) {
        case local_assets::LocalAssetSourceOpenState::opening:
        case local_assets::LocalAssetSourceOpenState::reading:
        case local_assets::LocalAssetSourceOpenState::validating:
            return;
        case local_assets::LocalAssetSourceOpenState::source_ready:
            finish_companion_open();
            return;
        case local_assets::LocalAssetSourceOpenState::timed_out:
            fail(
                GoldSrcStudioModelBundleImportErrorCode::timed_out,
                GoldSrcStudioModelBundleImportState::timed_out,
                "Studio companion source open timed out",
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                local_assets::LocalAssetSourceOpenErrorCode::timed_out,
                pending_sequence_group_ordinal_);
            return;
        case local_assets::LocalAssetSourceOpenState::cancelled:
            fail(
                GoldSrcStudioModelBundleImportErrorCode::cancelled,
                GoldSrcStudioModelBundleImportState::cancelled,
                "Studio companion source open was cancelled",
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                local_assets::LocalAssetSourceOpenErrorCode::cancelled,
                pending_sequence_group_ordinal_);
            return;
        case local_assets::LocalAssetSourceOpenState::failed:
            fail_from_source_open();
            return;
        case local_assets::LocalAssetSourceOpenState::idle:
            fail_dependency_invalid(
                "Studio companion source open entered an invalid state",
                pending_sequence_group_ordinal_);
            return;
        }
    }

    void begin_companion_open()
    {
        if (!dependency_plan_) {
            fail_dependency_invalid(
                "Studio dependency plan is unavailable");
            return;
        }
        std::optional<local_resources::LocalVirtualResourceName> derived_name;
        pending_sequence_group_ordinal_.reset();
        if (state_ == GoldSrcStudioModelBundleImportState::
                opening_texture_companion) {
            if (dependency_plan_->texture_companion_name()) {
                derived_name.emplace(
                    *dependency_plan_->texture_companion_name());
            }
        } else {
            if (next_sequence_group_index_ >=
                dependency_plan_->sequence_group_dependencies().size()) {
                fail_dependency_invalid(
                    "Studio sequence-group open index is invalid");
                return;
            }
            const auto& dependency = dependency_plan_
                                         ->sequence_group_dependencies()
                                             [next_sequence_group_index_];
            const auto ordinal = dependency.ordinal;
            pending_sequence_group_ordinal_ = ordinal;
            progress_.current_sequence_group_ordinal = ordinal;
            derived_name.emplace(dependency.virtual_name);
        }
        if (!derived_name) {
            fail(
                GoldSrcStudioModelBundleImportErrorCode::
                    companion_name_invalid,
                GoldSrcStudioModelBundleImportState::dependency_invalid,
                "Unable to derive a safe Studio companion virtual name",
                assets::AssetErrorCode::MalformedData,
                std::nullopt,
                GoldSrcStudioCompanionNameErrorCode::unable_to_retain_name,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                pending_sequence_group_ordinal_);
            return;
        }

        auto resolution = environment_->resolve_exact_root(
            *derived_name, root_id_);
        if (!resolution || !resolution.file) {
            const auto missing =
                resolution.code ==
                local_resources::LocalResourceResolutionCode::not_found;
            fail(
                missing
                    ? GoldSrcStudioModelBundleImportErrorCode::
                          dependency_missing
                    : GoldSrcStudioModelBundleImportErrorCode::
                          companion_resolution_failed,
                missing
                    ? GoldSrcStudioModelBundleImportState::dependency_missing
                    : GoldSrcStudioModelBundleImportState::dependency_invalid,
                missing
                    ? "A required exact-root Studio companion is missing"
                    : "A required Studio companion failed exact-root resolution",
                missing
                    ? std::optional{assets::AssetErrorCode::
                          ExternalDependencyRequired}
                    : std::optional{assets::AssetErrorCode::MalformedData},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                resolution.code,
                std::nullopt,
                pending_sequence_group_ordinal_);
            return;
        }

        auto& file = *resolution.file;
        const auto expected_virtual_id = derived_name->id();
        const auto expected_identity = file.identity();
        const auto expected_file_size = file.file_size();
        const bool evidence_matches =
            file.root_id() == root_id_ &&
            file.virtual_resource_id() == expected_virtual_id &&
            file.is_regular_file() && expected_identity.valid();
        file.close();
        if (!evidence_matches ||
            expected_file_size >
                limits_.companion_source_open.maximum_source_bytes ||
            expected_file_size >
                limits_.studio.maximum_companion_source_bytes) {
            fail_dependency_invalid(
                "Resolved Studio companion evidence is invalid or exceeds limits",
                pending_sequence_group_ordinal_);
            return;
        }
        const auto total_source_bytes_limit =
            effective_total_source_bytes_limit(limits_);
        if (progress_.source_bytes_ready > total_source_bytes_limit ||
            expected_file_size >
                total_source_bytes_limit - progress_.source_bytes_ready) {
            fail_total_source_bytes_limit(
                pending_sequence_group_ordinal_);
            return;
        }

        auto locator = environment_->make_locator(
            root_id_,
            std::move(*derived_name),
            expected_identity,
            expected_file_size);
        if (!locator || !locator.locator) {
            fail_dependency_invalid(
                "Unable to retain an exact-root Studio companion locator",
                pending_sequence_group_ordinal_);
            return;
        }
        auto begun = source_opener_.begin(
            *locator.locator,
            environment_,
            limits_.companion_source_open);
        if (!begun || !begun.operation) {
            fail(
                GoldSrcStudioModelBundleImportErrorCode::
                    companion_source_open_failed,
                GoldSrcStudioModelBundleImportState::dependency_invalid,
                "Unable to begin verified Studio companion source open",
                assets::AssetErrorCode::MalformedData,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                begun.error
                    ? std::optional{begun.error->code}
                    : std::nullopt,
                pending_sequence_group_ordinal_);
            return;
        }

        pending_virtual_id_ = expected_virtual_id;
        pending_identity_ = expected_identity;
        pending_file_size_ = expected_file_size;
        source_open_operation_.emplace(std::move(*begun.operation));
        ++progress_.source_open_attempt_count;
        progress_.current_source_progress_bytes = 0U;
    }

    void finish_companion_open()
    {
        if (!source_open_operation_ || !pending_virtual_id_ ||
            !pending_identity_ || !pending_file_size_) {
            fail_dependency_invalid(
                "Studio companion source evidence is incomplete",
                pending_sequence_group_ordinal_);
            return;
        }
        auto local_source = source_open_operation_->take_result();
        if (!local_source || local_source->root_id() != root_id_ ||
            local_source->virtual_resource_id() != *pending_virtual_id_ ||
            local_source->expected_identity() != *pending_identity_ ||
            local_source->byte_count() != *pending_file_size_ ||
            local_source->source().bytes().size() != *pending_file_size_) {
            fail_dependency_invalid(
                "Verified Studio companion source evidence changed",
                pending_sequence_group_ordinal_);
            return;
        }

        const auto retained_bytes = local_source->byte_count();
        const auto total_source_bytes_limit =
            effective_total_source_bytes_limit(limits_);
        if (progress_.source_bytes_ready > total_source_bytes_limit ||
            retained_bytes >
                total_source_bytes_limit - progress_.source_bytes_ready) {
            fail_total_source_bytes_limit(
                pending_sequence_group_ordinal_);
            return;
        }
        GoldSrcStudioVerifiedSourceIdentity retained_identity{
            local_source->root_id(),
            local_source->virtual_resource_id(),
            local_source->expected_identity()};
        assets::AssetSource retained_source = local_source->source();
        source_open_operation_.reset();
        pending_virtual_id_.reset();
        pending_identity_.reset();
        pending_file_size_.reset();
        progress_.current_source_progress_bytes = 0U;
        ++progress_.source_count_ready;
        progress_.source_bytes_ready += retained_bytes;

        if (state_ == GoldSrcStudioModelBundleImportState::
                opening_texture_companion) {
            texture_source_.emplace(std::move(retained_source));
            texture_source_identity_.emplace(
                std::move(retained_identity));
            pending_sequence_group_ordinal_.reset();
            if (dependency_plan_ &&
                !dependency_plan_->sequence_group_dependencies().empty()) {
                progress_.current_sequence_group_ordinal = dependency_plan_
                    ->sequence_group_dependencies().front().ordinal;
                state_ = GoldSrcStudioModelBundleImportState::
                    opening_sequence_group;
            } else {
                progress_.current_sequence_group_ordinal.reset();
                state_ =
                    GoldSrcStudioModelBundleImportState::validating_bundle;
            }
            return;
        }

        const auto ordinal = *pending_sequence_group_ordinal_;
        sequence_group_sources_.emplace_back(
            ordinal,
            std::move(retained_source),
            std::move(retained_identity));
        pending_sequence_group_ordinal_.reset();
        ++next_sequence_group_index_;
        if (dependency_plan_ &&
            next_sequence_group_index_ <
                dependency_plan_->sequence_group_dependencies().size()) {
            progress_.current_sequence_group_ordinal = dependency_plan_
                ->sequence_group_dependencies()[next_sequence_group_index_]
                    .ordinal;
            return;
        }
        progress_.current_sequence_group_ordinal.reset();
        state_ = GoldSrcStudioModelBundleImportState::validating_bundle;
    }

    void fail_from_source_open() noexcept
    {
        const auto* source_error =
            source_open_operation_ ? source_open_operation_->error() : nullptr;
        const auto source_code =
            source_error ? std::optional{source_error->code} : std::nullopt;
        const bool missing =
            source_code ==
            local_assets::LocalAssetSourceOpenErrorCode::
                locator_target_missing;
        fail(
            missing
                ? GoldSrcStudioModelBundleImportErrorCode::dependency_missing
                : GoldSrcStudioModelBundleImportErrorCode::
                      companion_source_open_failed,
            missing
                ? GoldSrcStudioModelBundleImportState::dependency_missing
                : GoldSrcStudioModelBundleImportState::dependency_invalid,
            missing
                ? "A required Studio companion disappeared before verified open"
                : "Verified Studio companion source open failed",
            missing
                ? std::optional{assets::AssetErrorCode::
                      ExternalDependencyRequired}
                : std::optional{assets::AssetErrorCode::MalformedData},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            source_code,
            pending_sequence_group_ordinal_);
    }

    void validate_bundle()
    {
        if (!main_source_ || !dependency_plan_) {
            fail_dependency_invalid(
                "Studio source-bundle prerequisites are unavailable");
            return;
        }
        auto created = GoldSrcStudioModelSourceBundle::create(
            std::move(*main_source_),
            std::move(texture_source_),
            std::move(texture_source_identity_),
            std::move(sequence_group_sources_),
            *dependency_plan_,
            limits_.bundle);
        main_source_.reset();
        texture_source_.reset();
        texture_source_identity_.reset();
        sequence_group_sources_.clear();
        if (!created || !created.bundle) {
            fail(
                GoldSrcStudioModelBundleImportErrorCode::
                    bundle_validation_failed,
                GoldSrcStudioModelBundleImportState::dependency_invalid,
                "Owning Studio source bundle failed dependency validation",
                assets::AssetErrorCode::MalformedData,
                std::nullopt,
                std::nullopt,
                created.error
                    ? std::optional{created.error->code}
                    : std::nullopt,
                std::nullopt,
                std::nullopt,
                created.error
                    ? created.error->sequence_group_ordinal
                    : std::nullopt);
            return;
        }
        source_bundle_.emplace(std::move(*created.bundle));
        state_ = GoldSrcStudioModelBundleImportState::importing_model;
    }

    void import_model()
    {
        if (!source_bundle_) {
            fail_dependency_invalid("Owning Studio source bundle is missing");
            return;
        }
        std::vector<studio::GoldSrcStudioSequenceGroupSourceView> group_views;
        group_views.reserve(source_bundle_->sequence_group_sources().size());
        for (const auto& group : source_bundle_->sequence_group_sources()) {
            group_views.push_back(
                studio::GoldSrcStudioSequenceGroupSourceView{
                    group.ordinal, group.source.bytes()});
        }
        studio::GoldSrcStudioSourceBundleView view{
            source_bundle_->main_source().bytes(),
            std::nullopt,
            group_views};
        if (source_bundle_->texture_source()) {
            view.texture_source = source_bundle_->texture_source()->bytes();
        }

        auto parsed = studio::GoldSrcStudioParser::parse(view, limits_.studio);
        if (!parsed || !parsed.document) {
            const auto studio_code = parsed.error
                                         ? std::optional{parsed.error->code}
                                         : std::nullopt;
            const bool dependency_missing =
                studio_code == studio::GoldSrcStudioErrorCode::
                                   external_dependency_required ||
                studio_code == studio::GoldSrcStudioErrorCode::
                                   missing_texture_companion ||
                studio_code == studio::GoldSrcStudioErrorCode::
                                   missing_sequence_group;
            if (dependency_missing) {
                fail(
                    GoldSrcStudioModelBundleImportErrorCode::
                        dependency_missing,
                    GoldSrcStudioModelBundleImportState::dependency_missing,
                    "Studio importer reported a required external dependency",
                    assets::AssetErrorCode::ExternalDependencyRequired,
                    studio_code,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    parsed.error
                        ? parsed.error->source_group_ordinal
                        : std::nullopt);
            } else {
                fail(
                    GoldSrcStudioModelBundleImportErrorCode::
                        dependency_invalid,
                    GoldSrcStudioModelBundleImportState::dependency_invalid,
                    "Studio source bundle contains an invalid dependency",
                    assets::AssetErrorCode::MalformedData,
                    studio_code,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    parsed.error
                        ? parsed.error->source_group_ordinal
                        : std::nullopt);
            }
            return;
        }
        assets::ModelAsset model;
        model.identity.source_name =
            virtual_path_bytes(source_bundle_->main_source());
        model.skeletal_data =
            std::make_shared<assets::SkeletalModelAssetData>(
                std::move(parsed.document->skeletal_model));
        result_.emplace(
            std::move(model), std::move(*source_bundle_));
        source_bundle_.reset();
        dependency_plan_.reset();
        state_ = GoldSrcStudioModelBundleImportState::model_ready;
    }

    void fail_dependency_invalid(
        const std::string_view context,
        const std::optional<std::uint32_t> ordinal = std::nullopt) noexcept
    {
        fail(
            GoldSrcStudioModelBundleImportErrorCode::dependency_invalid,
            GoldSrcStudioModelBundleImportState::dependency_invalid,
            context,
            assets::AssetErrorCode::MalformedData,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            ordinal);
    }

    void fail_total_source_bytes_limit(
        const std::optional<std::uint32_t> ordinal) noexcept
    {
        const auto studio_limit = static_cast<std::uint64_t>(
            limits_.studio.maximum_total_bundle_bytes);
        const auto bundle_limit =
            limits_.bundle.maximum_total_source_bytes;
        fail(
            GoldSrcStudioModelBundleImportErrorCode::
                bundle_validation_failed,
            GoldSrcStudioModelBundleImportState::dependency_invalid,
            "Studio dependency would exceed the cumulative source-byte limit",
            assets::AssetErrorCode::MalformedData,
            studio_limit <= bundle_limit
                ? std::optional{studio::GoldSrcStudioErrorCode::
                      source_limit_exceeded}
                : std::nullopt,
            std::nullopt,
            bundle_limit <= studio_limit
                ? std::optional{
                      GoldSrcStudioModelSourceBundleErrorCode::
                          total_source_bytes_limit_exceeded}
                : std::nullopt,
            std::nullopt,
            std::nullopt,
            ordinal);
    }

    void fail(
        const GoldSrcStudioModelBundleImportErrorCode code,
        const GoldSrcStudioModelBundleImportState terminal,
        const std::string_view context,
        const std::optional<assets::AssetErrorCode> asset_code = std::nullopt,
        const std::optional<studio::GoldSrcStudioErrorCode> studio_code =
            std::nullopt,
        const std::optional<GoldSrcStudioCompanionNameErrorCode>
            companion_name_code = std::nullopt,
        const std::optional<GoldSrcStudioModelSourceBundleErrorCode>
            bundle_code = std::nullopt,
        const std::optional<local_resources::LocalResourceResolutionCode>
            resolution_code = std::nullopt,
        const std::optional<local_assets::LocalAssetSourceOpenErrorCode>
            source_open_code = std::nullopt,
        const std::optional<std::uint32_t> sequence_group_ordinal =
            std::nullopt) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (source_open_operation_ &&
            source_open_operation_->state() !=
                local_assets::LocalAssetSourceOpenState::source_ready &&
            source_open_operation_->state() !=
                local_assets::LocalAssetSourceOpenState::cancelled &&
            source_open_operation_->state() !=
                local_assets::LocalAssetSourceOpenState::timed_out &&
            source_open_operation_->state() !=
                local_assets::LocalAssetSourceOpenState::failed) {
            source_open_operation_->cancel();
        }
        state_ = terminal;
        main_source_.reset();
        texture_source_.reset();
        texture_source_identity_.reset();
        sequence_group_sources_.clear();
        source_bundle_.reset();
        result_.reset();
        source_open_operation_.reset();
        pending_virtual_id_.reset();
        pending_identity_.reset();
        pending_file_size_.reset();
        error_.reset();
        try {
            error_.emplace();
            error_->code = code;
            error_->asset_code = asset_code;
            error_->studio_code = studio_code;
            error_->companion_name_code = companion_name_code;
            error_->bundle_code = bundle_code;
            error_->resolution_code = resolution_code;
            error_->source_open_code = source_open_code;
            error_->sequence_group_ordinal = sequence_group_ordinal;
            const auto bounded = context.substr(
                0U,
                (std::min)(context.size(),
                    kBundleImportDiagnosticTextLimit));
            error_->context.assign(bounded.data(), bounded.size());
        } catch (...) {
        }
    }

public:
    std::optional<assets::AssetSource> main_source_;
    local_resources::LocalResourceRootId root_id_;
    local_resources::LocalVirtualResourceName main_virtual_name_;
    local_resources::LocalStableFileIdentity main_identity_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    GoldSrcStudioModelBundleImportLimits limits_;
    GoldSrcStudioModelBundleImportState state_{
        GoldSrcStudioModelBundleImportState::idle};
    GoldSrcStudioModelBundleImportProgress progress_{};
    std::optional<GoldSrcStudioModelBundleImportTimePoint> started_at_;
    std::optional<GoldSrcStudioModelBundleImportTimePoint> last_update_at_;
    std::optional<GoldSrcStudioResolvedDependencyPlan> dependency_plan_;
    std::optional<assets::AssetSource> texture_source_;
    std::optional<GoldSrcStudioVerifiedSourceIdentity>
        texture_source_identity_;
    std::vector<GoldSrcStudioSequenceGroupSource> sequence_group_sources_;
    std::optional<GoldSrcStudioModelSourceBundle> source_bundle_;
    local_assets::LocalAssetSourceOpener source_opener_;
    std::optional<local_assets::LocalAssetSourceOpenOperation>
        source_open_operation_;
    std::optional<local_resources::LocalVirtualResourceId>
        pending_virtual_id_;
    std::optional<local_resources::LocalStableFileIdentity> pending_identity_;
    std::optional<std::uint64_t> pending_file_size_;
    std::optional<std::uint32_t> pending_sequence_group_ordinal_;
    std::size_t next_sequence_group_index_{0U};
    std::optional<GoldSrcStudioModelBundleImportResult> result_;
    std::optional<GoldSrcStudioModelBundleImportError> error_;
};

GoldSrcStudioModelBundleImportBeginResult
GoldSrcStudioModelBundleImportOperation::begin_validated(
    const assets::AssetSource& main_source,
    local_resources::LocalResourceRootId main_root_id,
    local_resources::LocalVirtualResourceName main_virtual_name,
    const local_resources::LocalStableFileIdentity main_identity,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    GoldSrcStudioModelBundleImportLimits limits)
{
    if (!valid_goldsrc_studio_model_bundle_import_limits(limits) ||
        environment == nullptr || environment->root_count() == 0U) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::invalid_configuration,
            "Studio bundle-import limits or local environment are invalid");
    }
    if (main_source.bytes().size() >
            limits.studio.maximum_main_source_bytes ||
        main_source.bytes().size() >
            effective_total_source_bytes_limit(limits) ||
        !environment->root_metadata(main_root_id)) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::approved_source_invalid,
            "Studio main source evidence is invalid or out of bounds");
    }

    try {
        auto implementation = std::make_unique<Implementation>(
            main_source,
            std::move(main_root_id),
            std::move(main_virtual_name),
            main_identity,
            std::move(environment),
            std::move(limits),
            static_cast<std::uint64_t>(main_source.bytes().size()));
        GoldSrcStudioModelBundleImportBeginResult result;
        result.operation.emplace(
            GoldSrcStudioModelBundleImportOperation{
                std::move(implementation)});
        return result;
    } catch (...) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::unable_to_retain_state,
            "Unable to retain approved Studio bundle-import prerequisites");
    }
}

GoldSrcStudioModelBundleImportBeginResult
GoldSrcStudioModelBundleImportOperation::begin_with_retained_main_evidence(
    const assets::AssetSource& main_source,
    const local_resources::LocalResourceRootId main_root_id,
    const local_resources::LocalVirtualResourceId main_virtual_resource_id,
    const local_resources::LocalStableFileIdentity main_identity,
    const std::uint64_t main_byte_count,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    GoldSrcStudioModelBundleImportLimits limits)
{
    if (!valid_goldsrc_studio_model_bundle_import_limits(limits) ||
        environment == nullptr || environment->root_count() == 0U) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::invalid_configuration,
            "Studio bundle-import limits or local environment are invalid");
    }
    if (main_byte_count !=
            static_cast<std::uint64_t>(main_source.bytes().size()) ||
        !main_identity.valid() ||
        !environment->root_metadata(main_root_id)) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::approved_source_invalid,
            "Retained Studio main-source evidence is invalid or out of bounds");
    }
    try {
        auto classified = local_resources::LocalVirtualResourceName::create(
            virtual_path_bytes(main_source));
        if (!classified || !classified.name ||
            classified.name->id() != main_virtual_resource_id) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    invalid_main_virtual_name,
                "Retained Studio main virtual-name evidence is invalid");
        }
        auto resolution = environment->resolve_exact_root(
            *classified.name, main_root_id);
        if (!resolution || !resolution.file) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    approved_source_invalid,
                "Retained Studio main source failed exact-root evidence resolution");
        }
        auto& file = *resolution.file;
        const bool evidence_matches =
            file.root_id() == main_root_id &&
            file.virtual_resource_id() == main_virtual_resource_id &&
            file.identity() == main_identity &&
            file.file_size() == main_byte_count &&
            file.is_regular_file();
        file.close();
        if (!evidence_matches) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    approved_source_invalid,
                "Retained Studio main source and exact-root evidence disagree");
        }
        return begin_validated(
            main_source,
            main_root_id,
            std::move(*classified.name),
            main_identity,
            std::move(environment),
            std::move(limits));
    } catch (...) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::unable_to_retain_state,
            "Unable to validate retained Studio main-source evidence");
    }
}

GoldSrcStudioModelBundleImportBeginResult
GoldSrcStudioModelBundleImportOperation::begin(
    const local_assets::LocalAssetSource& main_source,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    GoldSrcStudioModelBundleImportLimits limits)
{
    if (!valid_goldsrc_studio_model_bundle_import_limits(limits) ||
        environment == nullptr || environment->root_count() == 0U ||
        main_source.byte_count() != main_source.source().bytes().size() ||
        !main_source.expected_identity().valid() ||
        !environment->root_metadata(main_source.root_id())) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::approved_source_invalid,
            "Verified local Studio main source evidence is invalid");
    }
    try {
        auto classified = local_resources::LocalVirtualResourceName::create(
            virtual_path_bytes(main_source.source()));
        if (!classified || !classified.name ||
            classified.name->id() != main_source.virtual_resource_id()) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    invalid_main_virtual_name,
                "Verified local Studio main virtual-name evidence is invalid");
        }
        auto resolution = environment->resolve_exact_root(
            *classified.name, main_source.root_id());
        if (!resolution || !resolution.file) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    approved_source_invalid,
                "Verified local Studio main source failed exact-root resolution");
        }
        auto& file = *resolution.file;
        const bool evidence_matches =
            file.root_id() == main_source.root_id() &&
            file.virtual_resource_id() ==
                main_source.virtual_resource_id() &&
            file.identity() == main_source.expected_identity() &&
            file.file_size() == main_source.byte_count() &&
            file.is_regular_file();
        file.close();
        if (!evidence_matches) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    approved_source_invalid,
                "Verified local Studio main source and exact-root evidence disagree");
        }
        return begin_validated(
            main_source.source(),
            main_source.root_id(),
            std::move(*classified.name),
            main_source.expected_identity(),
            std::move(environment),
            std::move(limits));
    } catch (...) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::unable_to_retain_state,
            "Unable to validate local Studio main-source evidence");
    }
}

GoldSrcStudioModelBundleImportBeginResult
GoldSrcStudioModelBundleImportOperation::begin(
    const ApprovedAssetSource& approved_main_source,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    GoldSrcStudioModelBundleImportLimits limits)
{
    if (approved_main_source.role() !=
            assets::AssetDispatchRole::model_or_sprite ||
        approved_main_source.byte_count() !=
            approved_main_source.source().bytes().size() ||
        !approved_main_source.expected_identity().valid() ||
        environment == nullptr ||
        !environment->root_metadata(approved_main_source.root_id())) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::approved_source_invalid,
            "Approved Studio main source evidence is invalid or out of bounds");
    }
    try {
        auto classified = local_resources::LocalVirtualResourceName::create(
            virtual_path_bytes(approved_main_source.source()));
        if (!classified || !classified.name ||
            classified.name->id() !=
                approved_main_source.virtual_resource_id()) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    invalid_main_virtual_name,
                "Approved Studio main virtual-name evidence is invalid");
        }
        auto resolution = environment->resolve_exact_root(
            *classified.name, approved_main_source.root_id());
        if (!resolution || !resolution.file) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    approved_source_invalid,
                "Approved Studio main source failed exact-root evidence resolution");
        }
        auto& file = *resolution.file;
        const bool evidence_matches =
            file.root_id() == approved_main_source.root_id() &&
            file.virtual_resource_id() ==
                approved_main_source.virtual_resource_id() &&
            file.identity() == approved_main_source.expected_identity() &&
            file.file_size() == approved_main_source.byte_count() &&
            file.is_regular_file();
        file.close();
        if (!evidence_matches) {
            return begin_failure(
                GoldSrcStudioModelBundleImportErrorCode::
                    approved_source_invalid,
                "Approved Studio main source and exact-root evidence disagree");
        }
        return begin_validated(
            approved_main_source.source(),
            approved_main_source.root_id(),
            std::move(*classified.name),
            approved_main_source.expected_identity(),
            std::move(environment),
            std::move(limits));
    } catch (...) {
        return begin_failure(
            GoldSrcStudioModelBundleImportErrorCode::unable_to_retain_state,
            "Unable to validate approved Studio main-source evidence");
    }
}

GoldSrcStudioModelBundleImportOperation::
    GoldSrcStudioModelBundleImportOperation(
        std::unique_ptr<Implementation> implementation) noexcept
    : implementation_{std::move(implementation)}
{
}

GoldSrcStudioModelBundleImportOperation::
    ~GoldSrcStudioModelBundleImportOperation() = default;
GoldSrcStudioModelBundleImportOperation::
    GoldSrcStudioModelBundleImportOperation(
        GoldSrcStudioModelBundleImportOperation&&) noexcept = default;
GoldSrcStudioModelBundleImportOperation&
GoldSrcStudioModelBundleImportOperation::operator=(
    GoldSrcStudioModelBundleImportOperation&&) noexcept = default;

void GoldSrcStudioModelBundleImportOperation::update(
    const GoldSrcStudioModelBundleImportTimePoint now) noexcept
{
    implementation_->update(now);
}

void GoldSrcStudioModelBundleImportOperation::cancel() noexcept
{
    implementation_->cancel();
}

GoldSrcStudioModelBundleImportState
GoldSrcStudioModelBundleImportOperation::state() const noexcept
{
    return implementation_->state_;
}

bool GoldSrcStudioModelBundleImportOperation::terminal() const noexcept
{
    return terminal_state(implementation_->state_);
}

const GoldSrcStudioModelBundleImportProgress&
GoldSrcStudioModelBundleImportOperation::progress() const noexcept
{
    return implementation_->progress_;
}

const GoldSrcStudioModelBundleImportResult*
GoldSrcStudioModelBundleImportOperation::result() const noexcept
{
    return implementation_->result_ ? &*implementation_->result_ : nullptr;
}

const GoldSrcStudioModelBundleImportError*
GoldSrcStudioModelBundleImportOperation::error() const noexcept
{
    return implementation_->error_ ? &*implementation_->error_ : nullptr;
}

std::optional<GoldSrcStudioModelBundleImportResult>
GoldSrcStudioModelBundleImportOperation::take_result() noexcept
{
    if (implementation_->state_ !=
            GoldSrcStudioModelBundleImportState::model_ready ||
        !implementation_->result_) {
        return std::nullopt;
    }
    return std::exchange(implementation_->result_, std::nullopt);
}

} // namespace hlclient::goldsrc::visual_assets
