#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_source_bundle.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hlclient::goldsrc::visual_assets {
namespace {

inline constexpr std::size_t kBundleDiagnosticTextLimit = 192U;

[[nodiscard]] GoldSrcStudioModelSourceBundleCreateResult failure(
    const GoldSrcStudioModelSourceBundleErrorCode code,
    const std::string_view context,
    const std::optional<std::uint32_t> ordinal = std::nullopt) noexcept
{
    try {
        const auto size = (std::min)(context.size(), kBundleDiagnosticTextLimit);
        return GoldSrcStudioModelSourceBundleCreateResult{
            std::nullopt,
            GoldSrcStudioModelSourceBundleError{
                code,
                ordinal,
                std::string{context.data(), size}}};
    } catch (...) {
        return GoldSrcStudioModelSourceBundleCreateResult{
            std::nullopt,
            GoldSrcStudioModelSourceBundleError{
                GoldSrcStudioModelSourceBundleErrorCode::
                    unable_to_retain_bundle,
                std::nullopt,
                {}}};
    }
}

[[nodiscard]] GoldSrcStudioResolvedDependencyPlanResult resolved_failure(
    const GoldSrcStudioResolvedDependencyPlanErrorCode code,
    const std::string_view context,
    const std::optional<std::uint32_t> ordinal = std::nullopt,
    const std::optional<GoldSrcStudioCompanionNameErrorCode> name_code =
        std::nullopt) noexcept
{
    try {
        const auto size = (std::min)(context.size(), kBundleDiagnosticTextLimit);
        return GoldSrcStudioResolvedDependencyPlanResult{
            std::nullopt,
            GoldSrcStudioResolvedDependencyPlanError{
                code,
                ordinal,
                name_code,
                std::string{context.data(), size}}};
    } catch (...) {
        GoldSrcStudioResolvedDependencyPlanResult result;
        try {
            result.error.emplace();
            result.error->code =
                GoldSrcStudioResolvedDependencyPlanErrorCode::
                    unable_to_retain_plan;
        } catch (...) {
        }
        return result;
    }
}

[[nodiscard]] bool checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] std::string virtual_name_key(
    const assets::AssetSource& source)
{
    const auto encoded = source.virtual_path().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

} // namespace

GoldSrcStudioResolvedSequenceGroupDependency::
    GoldSrcStudioResolvedSequenceGroupDependency(
        const std::uint32_t ordinal_value,
        local_resources::LocalVirtualResourceName virtual_name_value)
    : ordinal{ordinal_value},
      virtual_name{std::move(virtual_name_value)}
{
}

GoldSrcStudioResolvedDependencyPlan::GoldSrcStudioResolvedDependencyPlan(
    studio::GoldSrcStudioModelDependencyPlan source_plan,
    local_resources::LocalResourceRootId main_root_id,
    local_resources::LocalVirtualResourceName main_virtual_name,
    const local_resources::LocalStableFileIdentity main_identity,
    const assets::AssetSourceFingerprint main_fingerprint,
    std::optional<local_resources::LocalVirtualResourceName>
        texture_companion_name,
    std::vector<GoldSrcStudioResolvedSequenceGroupDependency>
        sequence_group_dependencies) noexcept
    : source_plan_{std::move(source_plan)},
      main_root_id_{std::move(main_root_id)},
      main_virtual_name_{std::move(main_virtual_name)},
      main_identity_{main_identity},
      main_fingerprint_{main_fingerprint},
      texture_companion_name_{std::move(texture_companion_name)},
      sequence_group_dependencies_{std::move(sequence_group_dependencies)}
{
}

const studio::GoldSrcStudioModelDependencyPlan&
GoldSrcStudioResolvedDependencyPlan::source_plan() const noexcept
{
    return source_plan_;
}

local_resources::LocalResourceRootId
GoldSrcStudioResolvedDependencyPlan::main_root_id() const noexcept
{
    return main_root_id_;
}

const local_resources::LocalVirtualResourceName&
GoldSrcStudioResolvedDependencyPlan::main_virtual_name() const noexcept
{
    return main_virtual_name_;
}

local_resources::LocalStableFileIdentity
GoldSrcStudioResolvedDependencyPlan::main_identity() const noexcept
{
    return main_identity_;
}

assets::AssetSourceFingerprint
GoldSrcStudioResolvedDependencyPlan::main_fingerprint() const noexcept
{
    return main_fingerprint_;
}

const std::optional<local_resources::LocalVirtualResourceName>&
GoldSrcStudioResolvedDependencyPlan::texture_companion_name() const noexcept
{
    return texture_companion_name_;
}

std::span<const GoldSrcStudioResolvedSequenceGroupDependency>
GoldSrcStudioResolvedDependencyPlan::sequence_group_dependencies()
    const noexcept
{
    return sequence_group_dependencies_;
}

assets::ModelSkeletalCompatibilityProfile
GoldSrcStudioResolvedDependencyPlan::compatibility_profile() const noexcept
{
    return assets::ModelSkeletalCompatibilityProfile::goldsrc_studio_v10;
}

assets::ModelSkeletalEvidenceProfile
GoldSrcStudioResolvedDependencyPlan::evidence_profile() const noexcept
{
    return assets::ModelSkeletalEvidenceProfile::public_valve_wire_profile;
}

GoldSrcStudioResolvedDependencyPlanResult
resolve_goldsrc_studio_dependency_plan(
    const studio::GoldSrcStudioModelDependencyPlan& source_plan,
    const local_resources::LocalVirtualResourceName& main_virtual_name,
    local_resources::LocalResourceRootId main_root_id,
    const local_resources::LocalStableFileIdentity main_identity,
    const assets::AssetSourceFingerprint main_fingerprint) noexcept
{
    if (!main_root_id.valid() || !main_identity.valid() ||
        main_virtual_name.value().empty() ||
        main_virtual_name.component_count() == 0U) {
        return resolved_failure(
            GoldSrcStudioResolvedDependencyPlanErrorCode::
                invalid_main_evidence,
            "Verified Studio main-source evidence is invalid");
    }
    const auto expected_source_count =
        1U + (source_plan.texture_companion_required ? 1U : 0U) +
        source_plan.required_sequence_group_ordinals.size();
    if (source_plan.expected_source_count != expected_source_count ||
        expected_source_count > kGoldSrcStudioBundleMaximumSources) {
        return resolved_failure(
            GoldSrcStudioResolvedDependencyPlanErrorCode::
                invalid_source_plan,
            "Studio parser dependency-plan source counts disagree");
    }

    try {
        auto retained_source_plan = source_plan;
        std::ranges::sort(
            retained_source_plan.required_sequence_group_ordinals);
        for (std::size_t index = 0U;
             index <
             retained_source_plan.required_sequence_group_ordinals.size();
             ++index) {
            const auto ordinal = retained_source_plan
                                     .required_sequence_group_ordinals[index];
            if (ordinal < kGoldSrcStudioMinimumExternalSequenceGroup ||
                ordinal > kGoldSrcStudioMaximumExternalSequenceGroup ||
                (index > 0U &&
                 retained_source_plan.required_sequence_group_ordinals
                         [index - 1U] == ordinal)) {
                return resolved_failure(
                    GoldSrcStudioResolvedDependencyPlanErrorCode::
                        invalid_source_plan,
                    "Studio parser dependency-plan ordinals are invalid",
                    ordinal);
            }
        }

        std::optional<local_resources::LocalVirtualResourceName> texture_name;
        if (retained_source_plan.texture_companion_required) {
            auto derived = derive_goldsrc_studio_texture_companion_name(
                main_virtual_name);
            if (!derived || !derived.name) {
                return resolved_failure(
                    GoldSrcStudioResolvedDependencyPlanErrorCode::
                        companion_name_invalid,
                    "Unable to derive the safe Studio texture sibling",
                    std::nullopt,
                    derived.error
                        ? std::optional{derived.error->code}
                        : std::nullopt);
            }
            texture_name.emplace(std::move(*derived.name));
        }

        std::vector<GoldSrcStudioResolvedSequenceGroupDependency> groups;
        groups.reserve(
            retained_source_plan.required_sequence_group_ordinals.size());
        for (const auto ordinal :
             retained_source_plan.required_sequence_group_ordinals) {
            auto derived =
                derive_goldsrc_studio_sequence_group_companion_name(
                    main_virtual_name, static_cast<std::uint8_t>(ordinal));
            if (!derived || !derived.name) {
                return resolved_failure(
                    GoldSrcStudioResolvedDependencyPlanErrorCode::
                        companion_name_invalid,
                    "Unable to derive a safe Studio sequence-group sibling",
                    ordinal,
                    derived.error
                        ? std::optional{derived.error->code}
                        : std::nullopt);
            }
            groups.emplace_back(ordinal, std::move(*derived.name));
        }

        GoldSrcStudioResolvedDependencyPlanResult result;
        result.plan.emplace(GoldSrcStudioResolvedDependencyPlan{
            std::move(retained_source_plan),
            std::move(main_root_id),
            main_virtual_name,
            main_identity,
            main_fingerprint,
            std::move(texture_name),
            std::move(groups)});
        return result;
    } catch (...) {
        return resolved_failure(
            GoldSrcStudioResolvedDependencyPlanErrorCode::
                unable_to_retain_plan,
            "Unable to retain the bounded resolved Studio dependency plan");
    }
}

bool valid_goldsrc_studio_model_source_bundle_limits(
    const GoldSrcStudioModelSourceBundleLimits& limits) noexcept
{
    return limits.maximum_total_source_bytes > 0U &&
           limits.maximum_total_source_bytes <=
               kHardGoldSrcStudioBundleMaximumBytes &&
           limits.maximum_source_count > 0U &&
           limits.maximum_source_count <=
               kGoldSrcStudioBundleMaximumSources;
}

assets::AssetSourceFingerprint goldsrc_studio_source_fingerprint(
    const std::span<const std::byte> source) noexcept
{
    constexpr std::uint64_t first_offset = 14'695'981'039'346'656'037ULL;
    constexpr std::uint64_t first_prime = 1'099'511'628'211ULL;
    constexpr std::uint64_t second_offset = 7'806'984'959'868'165'187ULL;
    constexpr std::uint64_t second_prime = 14'029'467'366'897'019'727ULL;
    std::uint64_t first = first_offset;
    std::uint64_t second = second_offset;
    const auto add = [](std::uint64_t& hash,
                         const std::uint8_t value,
                         const std::uint64_t prime) noexcept {
        hash ^= value;
        hash *= prime;
    };
    for (const auto value : source) {
        const auto byte = std::to_integer<std::uint8_t>(value);
        add(first, byte, first_prime);
        add(second, byte, second_prime);
    }
    auto size = static_cast<std::uint64_t>(source.size());
    for (std::size_t index = 0U; index < sizeof(size); ++index) {
        const auto byte = static_cast<std::uint8_t>(size & 0xFFU);
        add(first, byte, first_prime);
        add(second, byte, second_prime);
        size >>= 8U;
    }
    return {first, second};
}

GoldSrcStudioVerifiedSourceIdentity::GoldSrcStudioVerifiedSourceIdentity(
    local_resources::LocalResourceRootId root_id,
    const local_resources::LocalVirtualResourceId virtual_resource_id,
    const local_resources::LocalStableFileIdentity stable_file_identity)
    noexcept
    : root_id_{std::move(root_id)},
      virtual_resource_id_{virtual_resource_id},
      stable_file_identity_{stable_file_identity}
{
}

bool GoldSrcStudioVerifiedSourceIdentity::valid() const noexcept
{
    return root_id_.valid() && stable_file_identity_.valid();
}

local_resources::LocalResourceRootId
GoldSrcStudioVerifiedSourceIdentity::root_id() const noexcept
{
    return root_id_;
}

local_resources::LocalVirtualResourceId
GoldSrcStudioVerifiedSourceIdentity::virtual_resource_id() const noexcept
{
    return virtual_resource_id_;
}

local_resources::LocalStableFileIdentity
GoldSrcStudioVerifiedSourceIdentity::stable_file_identity() const noexcept
{
    return stable_file_identity_;
}

GoldSrcStudioSequenceGroupSource::GoldSrcStudioSequenceGroupSource(
    const std::uint32_t ordinal_value,
    assets::AssetSource source_value,
    GoldSrcStudioVerifiedSourceIdentity identity_value)
    : ordinal{ordinal_value},
      source{std::move(source_value)},
      identity{std::move(identity_value)},
      fingerprint{goldsrc_studio_source_fingerprint(source.bytes())}
{
}

GoldSrcStudioModelSourceBundle::GoldSrcStudioModelSourceBundle(
    assets::AssetSource main_source,
    std::optional<assets::AssetSource> texture_source,
    GoldSrcStudioVerifiedSourceIdentity main_source_identity,
    std::optional<GoldSrcStudioVerifiedSourceIdentity>
        texture_source_identity,
    std::vector<GoldSrcStudioSequenceGroupSource> sequence_group_sources,
    GoldSrcStudioResolvedDependencyPlan dependency_plan,
    const assets::AssetSourceFingerprint main_fingerprint,
    std::optional<assets::AssetSourceFingerprint> texture_fingerprint,
    const GoldSrcStudioModelDependencyStatistics statistics) noexcept
    : main_source_{std::move(main_source)},
      texture_source_{std::move(texture_source)},
      main_source_identity_{std::move(main_source_identity)},
      texture_source_identity_{std::move(texture_source_identity)},
      sequence_group_sources_{std::move(sequence_group_sources)},
      dependency_plan_{std::move(dependency_plan)},
      main_fingerprint_{main_fingerprint},
      texture_fingerprint_{std::move(texture_fingerprint)},
      statistics_{statistics}
{
}

GoldSrcStudioModelSourceBundleCreateResult
GoldSrcStudioModelSourceBundle::create(
    assets::AssetSource main_source,
    std::optional<assets::AssetSource> texture_source,
    std::optional<GoldSrcStudioVerifiedSourceIdentity>
        texture_source_identity,
    std::vector<GoldSrcStudioSequenceGroupSource> sequence_group_sources,
    const GoldSrcStudioResolvedDependencyPlan& dependency_plan,
    const GoldSrcStudioModelSourceBundleLimits limits) noexcept
{
    if (!valid_goldsrc_studio_model_source_bundle_limits(limits)) {
        return failure(
            GoldSrcStudioModelSourceBundleErrorCode::invalid_configuration,
            "Studio source-bundle limits are invalid");
    }

    if (texture_source.has_value() != texture_source_identity.has_value()) {
        return failure(
            GoldSrcStudioModelSourceBundleErrorCode::
                dependency_plan_mismatch,
            "Studio texture source and verified identity disagree");
    }

    const auto source_count =
        1U + (texture_source ? 1U : 0U) + sequence_group_sources.size();
    if (source_count > limits.maximum_source_count ||
        source_count > kGoldSrcStudioBundleMaximumSources) {
        return failure(
            GoldSrcStudioModelSourceBundleErrorCode::
                source_count_limit_exceeded,
            "Studio source bundle exceeds the source-count limit");
    }

    std::ranges::sort(
        sequence_group_sources, {}, &GoldSrcStudioSequenceGroupSource::ordinal);
    std::optional<std::uint32_t> previous_ordinal;
    for (const auto& group : sequence_group_sources) {
        if (group.ordinal < kGoldSrcStudioMinimumExternalSequenceGroup ||
            group.ordinal > kGoldSrcStudioMaximumExternalSequenceGroup) {
            return failure(
                GoldSrcStudioModelSourceBundleErrorCode::
                    invalid_sequence_group_ordinal,
                "Studio sequence-group source ordinal is out of range",
                group.ordinal);
        }
        if (previous_ordinal && *previous_ordinal == group.ordinal) {
            return failure(
                GoldSrcStudioModelSourceBundleErrorCode::
                    duplicate_sequence_group,
                "Studio source bundle contains a duplicate sequence group",
                group.ordinal);
        }
        previous_ordinal = group.ordinal;
    }

    const auto& source_plan = dependency_plan.source_plan();
    if (source_plan.expected_source_count != source_count ||
        source_plan.texture_companion_required !=
            texture_source.has_value() ||
        source_plan.required_sequence_group_ordinals.size() !=
            sequence_group_sources.size()) {
        return failure(
            GoldSrcStudioModelSourceBundleErrorCode::dependency_plan_mismatch,
            "Studio source bundle does not satisfy its dependency plan");
    }
    std::vector<std::uint32_t> required_ordinals;
    try {
        required_ordinals = source_plan.required_sequence_group_ordinals;
        std::ranges::sort(required_ordinals);
    } catch (...) {
        return failure(
            GoldSrcStudioModelSourceBundleErrorCode::unable_to_retain_bundle,
            "Unable to retain Studio dependency-plan ordinals");
    }
    for (std::size_t index = 0U; index < required_ordinals.size(); ++index) {
        if (required_ordinals[index] <
                kGoldSrcStudioMinimumExternalSequenceGroup ||
            required_ordinals[index] >
                kGoldSrcStudioMaximumExternalSequenceGroup ||
            (index > 0U &&
             required_ordinals[index - 1U] == required_ordinals[index]) ||
            required_ordinals[index] != sequence_group_sources[index].ordinal) {
            return failure(
                GoldSrcStudioModelSourceBundleErrorCode::
                    dependency_plan_mismatch,
                "Studio source bundle sequence groups do not match the dependency plan",
                required_ordinals[index]);
        }
    }

    std::uint64_t total_source_bytes = 0U;
    const auto include_bytes = [&](const assets::AssetSource& source) {
        std::uint64_t next = 0U;
        if (!checked_add(
                total_source_bytes,
                static_cast<std::uint64_t>(source.bytes().size()),
                next) ||
            next > limits.maximum_total_source_bytes) {
            return false;
        }
        total_source_bytes = next;
        return true;
    };
    if (!include_bytes(main_source) ||
        (texture_source && !include_bytes(*texture_source))) {
        return failure(
            GoldSrcStudioModelSourceBundleErrorCode::
                total_source_bytes_limit_exceeded,
            "Studio source bundle exceeds the total-byte limit");
    }
    for (const auto& group : sequence_group_sources) {
        if (!include_bytes(group.source)) {
            return failure(
                GoldSrcStudioModelSourceBundleErrorCode::
                    total_source_bytes_limit_exceeded,
                "Studio source bundle exceeds the total-byte limit",
                group.ordinal);
        }
    }

    try {
        if (virtual_name_key(main_source) !=
                dependency_plan.main_virtual_name().value() ||
            !dependency_plan.main_root_id().valid() ||
            !dependency_plan.main_identity().valid() ||
            goldsrc_studio_source_fingerprint(main_source.bytes()) !=
                dependency_plan.main_fingerprint() ||
            (texture_source.has_value() !=
             dependency_plan.texture_companion_name().has_value()) ||
            (texture_source &&
             virtual_name_key(*texture_source) !=
                 dependency_plan.texture_companion_name()->value()) ||
            dependency_plan.sequence_group_dependencies().size() !=
                sequence_group_sources.size()) {
            return failure(
                GoldSrcStudioModelSourceBundleErrorCode::
                    dependency_plan_mismatch,
                "Studio source identities do not match the resolved dependency plan");
        }
        if (texture_source_identity &&
            (!texture_source_identity->valid() ||
             texture_source_identity->root_id() !=
                 dependency_plan.main_root_id() ||
             texture_source_identity->virtual_resource_id() !=
                 dependency_plan.texture_companion_name()->id())) {
            return failure(
                GoldSrcStudioModelSourceBundleErrorCode::
                    dependency_plan_mismatch,
                "Studio texture identity does not match the resolved dependency plan");
        }
        for (std::size_t index = 0U;
             index < sequence_group_sources.size();
             ++index) {
            if (sequence_group_sources[index].ordinal !=
                    dependency_plan.sequence_group_dependencies()[index]
                        .ordinal ||
                virtual_name_key(sequence_group_sources[index].source) !=
                    dependency_plan.sequence_group_dependencies()[index]
                        .virtual_name.value() ||
                !sequence_group_sources[index].identity.valid() ||
                sequence_group_sources[index].identity.root_id() !=
                    dependency_plan.main_root_id() ||
                sequence_group_sources[index]
                        .identity.virtual_resource_id() !=
                    dependency_plan.sequence_group_dependencies()[index]
                        .virtual_name.id()) {
                return failure(
                    GoldSrcStudioModelSourceBundleErrorCode::
                        dependency_plan_mismatch,
                    "Studio sequence-group identity does not match the resolved plan",
                    sequence_group_sources[index].ordinal);
            }
        }
        std::vector<std::string> virtual_names;
        virtual_names.reserve(source_count);
        virtual_names.push_back(virtual_name_key(main_source));
        if (texture_source) {
            virtual_names.push_back(virtual_name_key(*texture_source));
        }
        for (const auto& group : sequence_group_sources) {
            virtual_names.push_back(virtual_name_key(group.source));
        }
        std::ranges::sort(virtual_names);
        if (std::ranges::adjacent_find(virtual_names) != virtual_names.end()) {
            return failure(
                GoldSrcStudioModelSourceBundleErrorCode::
                    duplicate_virtual_source,
                "Studio source bundle reuses one virtual source for multiple roles");
        }

        const auto main_fingerprint =
            goldsrc_studio_source_fingerprint(main_source.bytes());
        std::optional<assets::AssetSourceFingerprint> texture_fingerprint;
        if (texture_source) {
            texture_fingerprint =
                goldsrc_studio_source_fingerprint(texture_source->bytes());
        }
        GoldSrcStudioModelDependencyStatistics statistics{
            source_count,
            sequence_group_sources.size(),
            total_source_bytes,
            texture_source.has_value()};
        GoldSrcStudioModelSourceBundleCreateResult result;
        GoldSrcStudioVerifiedSourceIdentity main_source_identity{
            dependency_plan.main_root_id(),
            dependency_plan.main_virtual_name().id(),
            dependency_plan.main_identity()};
        result.bundle.emplace(
            GoldSrcStudioModelSourceBundle{
                std::move(main_source),
                std::move(texture_source),
                std::move(main_source_identity),
                std::move(texture_source_identity),
                std::move(sequence_group_sources),
                dependency_plan,
                main_fingerprint,
                std::move(texture_fingerprint),
                statistics});
        return result;
    } catch (...) {
        return failure(
            GoldSrcStudioModelSourceBundleErrorCode::unable_to_retain_bundle,
            "Unable to retain the bounded owning Studio source bundle");
    }
}

const assets::AssetSource& GoldSrcStudioModelSourceBundle::main_source()
    const noexcept
{
    return main_source_;
}

const std::optional<assets::AssetSource>&
GoldSrcStudioModelSourceBundle::texture_source() const noexcept
{
    return texture_source_;
}

const GoldSrcStudioVerifiedSourceIdentity&
GoldSrcStudioModelSourceBundle::main_source_identity() const noexcept
{
    return main_source_identity_;
}

const std::optional<GoldSrcStudioVerifiedSourceIdentity>&
GoldSrcStudioModelSourceBundle::texture_source_identity() const noexcept
{
    return texture_source_identity_;
}

std::span<const GoldSrcStudioSequenceGroupSource>
GoldSrcStudioModelSourceBundle::sequence_group_sources() const noexcept
{
    return sequence_group_sources_;
}

assets::AssetSourceFingerprint
GoldSrcStudioModelSourceBundle::main_fingerprint() const noexcept
{
    return main_fingerprint_;
}

const std::optional<assets::AssetSourceFingerprint>&
GoldSrcStudioModelSourceBundle::texture_fingerprint() const noexcept
{
    return texture_fingerprint_;
}

const GoldSrcStudioModelDependencyStatistics&
GoldSrcStudioModelSourceBundle::statistics() const noexcept
{
    return statistics_;
}

const studio::GoldSrcStudioModelDependencyPlan&
GoldSrcStudioModelSourceBundle::dependency_plan() const noexcept
{
    return dependency_plan_.source_plan();
}

const GoldSrcStudioResolvedDependencyPlan&
GoldSrcStudioModelSourceBundle::resolved_dependency_plan() const noexcept
{
    return dependency_plan_;
}

} // namespace hlclient::goldsrc::visual_assets
