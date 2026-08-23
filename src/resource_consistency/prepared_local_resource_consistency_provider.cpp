#include <hlclient/resource_consistency/prepared_local_resource_consistency_provider.hpp>

#include <utility>

namespace hlclient::resource_consistency {
namespace {

namespace local = hlclient::local_resources;

[[nodiscard]] PreparedLocalResourceConsistencyProviderCreateResult failure(
    const ResourceConsistencyErrorCode code,
    std::string context)
{
    if (context.size() > kResourceConsistencyDiagnosticTextLimit) {
        context.resize(kResourceConsistencyDiagnosticTextLimit);
    }
    return PreparedLocalResourceConsistencyProviderCreateResult{
        nullptr,
        ResourceConsistencyError{code, std::move(context)},
    };
}

[[nodiscard]] ResourceConsistencyErrorCode map_resolution_error(
    const local::LocalResourceResolutionCode code) noexcept
{
    switch (code) {
    case local::LocalResourceResolutionCode::not_found:
        return ResourceConsistencyErrorCode::unavailable;
    case local::LocalResourceResolutionCode::too_large:
        return ResourceConsistencyErrorCode::material_too_large;
    case local::LocalResourceResolutionCode::unsafe_name:
    case local::LocalResourceResolutionCode::unsupported_name_encoding:
    case local::LocalResourceResolutionCode::unsupported_resource_mapping:
        return ResourceConsistencyErrorCode::invalid_configuration;
    case local::LocalResourceResolutionCode::resolved:
    case local::LocalResourceResolutionCode::ambiguous_case:
    case local::LocalResourceResolutionCode::not_regular_file:
    case local::LocalResourceResolutionCode::reparse_escape:
    case local::LocalResourceResolutionCode::remote_volume_unsupported:
    case local::LocalResourceResolutionCode::io_error:
        return ResourceConsistencyErrorCode::provider_error;
    }
    return ResourceConsistencyErrorCode::provider_error;
}

[[nodiscard]] ResourceConsistencyErrorCode map_inspection_error(
    const local::LocalResourceFileInspectionErrorCode code) noexcept
{
    switch (code) {
    case local::LocalResourceFileInspectionErrorCode::invalid_configuration:
    case local::LocalResourceFileInspectionErrorCode::invalid_state:
        return ResourceConsistencyErrorCode::invalid_configuration;
    case local::LocalResourceFileInspectionErrorCode::too_large:
        return ResourceConsistencyErrorCode::material_too_large;
    case local::LocalResourceFileInspectionErrorCode::empty_file:
        return ResourceConsistencyErrorCode::invalid_material;
    case local::LocalResourceFileInspectionErrorCode::read_failed:
    case local::LocalResourceFileInspectionErrorCode::state_changed:
    case local::LocalResourceFileInspectionErrorCode::hash_failed:
        return ResourceConsistencyErrorCode::provider_error;
    }
    return ResourceConsistencyErrorCode::provider_error;
}

class LocalFileSessionLifetime final
    : public IResourceConsistencySessionLifetime {
public:
    explicit LocalFileSessionLifetime(local::LocalReadOnlyFile file) noexcept
        : file_{std::move(file)}
    {
    }

private:
    local::LocalReadOnlyFile file_;
};

class PreparedLocalResourceConsistencyOperation final
    : public ResourceConsistencyOperation {
public:
    PreparedLocalResourceConsistencyOperation(
        ResourceConsistencyMaterial material,
        std::unique_ptr<IResourceConsistencySessionLifetime> lifetime) noexcept
        : material_{std::move(material)}, lifetime_{std::move(lifetime)}
    {
    }

    [[nodiscard]] ResourceConsistencyUpdateResult update() override
    {
        if (cancelled_) {
            return ResourceConsistencyUpdateResult::failed({
                ResourceConsistencyErrorCode::cancelled,
                "Prepared local resource consistency operation was cancelled",
            });
        }
        if (!material_) {
            return ResourceConsistencyUpdateResult::failed({
                ResourceConsistencyErrorCode::unavailable,
                "Prepared local resource consistency material was already consumed",
            });
        }

        auto material = std::move(*material_);
        material_.reset();
        return ResourceConsistencyUpdateResult::succeeded(
            ResourceConsistencySession{
                std::move(material), std::move(lifetime_)});
    }

    void cancel() noexcept override
    {
        if (!cancelled_) {
            cancelled_ = true;
            material_.reset();
            lifetime_.reset();
        }
    }

private:
    std::optional<ResourceConsistencyMaterial> material_;
    std::unique_ptr<IResourceConsistencySessionLifetime> lifetime_;
    bool cancelled_{false};
};

} // namespace

bool valid_prepared_local_resource_consistency_provider_limits(
    const PreparedLocalResourceConsistencyProviderLimits& limits) noexcept
{
    return local::valid_local_resource_resolver_limits(limits.resolver) &&
           local::valid_local_resource_file_inspection_limits(
               limits.inspection) &&
           limits.inspection.maximum_file_size <=
               limits.resolver.maximum_file_size &&
           limits.inspection.require_non_empty;
}

PreparedLocalResourceConsistencyProvider::
    PreparedLocalResourceConsistencyProvider(
        PreparedLocalResourceConsistencyMetadata metadata,
        std::unique_ptr<ResourceConsistencyOperation> prepared_operation)
    noexcept
    : metadata_{std::move(metadata)},
      prepared_operation_{std::move(prepared_operation)}
{
}

PreparedLocalResourceConsistencyProviderCreateResult
PreparedLocalResourceConsistencyProvider::prepare(
    local::LocalResourceSearchRoots roots,
    const PreparedLocalResourceConsistencyProviderLimits limits)
{
    if (!valid_prepared_local_resource_consistency_provider_limits(limits)) {
        return failure(
            ResourceConsistencyErrorCode::invalid_configuration,
            "Local consistency provider limits are outside project hard caps");
    }

    auto environment =
        local::LocalResourceEnvironment::create(std::move(roots), limits.resolver);
    if (!environment || !environment.environment) {
        return failure(
            ResourceConsistencyErrorCode::invalid_configuration,
            environment.error
                ? environment.error->context
                : "Unable to create the local resource environment");
    }
    return prepare(*environment.environment, limits.inspection);
}

PreparedLocalResourceConsistencyProviderCreateResult
PreparedLocalResourceConsistencyProvider::prepare(
    const local::LocalResourceEnvironment& environment,
    const local::LocalResourceFileInspectionLimits inspection_limits)
{
    if (environment.root_count() == 0U ||
        !local::valid_local_resource_resolver_limits(environment.limits()) ||
        !local::valid_local_resource_file_inspection_limits(
            inspection_limits) ||
        inspection_limits.maximum_file_size >
            environment.limits().maximum_file_size ||
        !inspection_limits.require_non_empty) {
        return failure(
            ResourceConsistencyErrorCode::invalid_configuration,
            "Local consistency provider environment or inspection limits are invalid");
    }

    const auto root_count = environment.root_count();

    auto target =
        local::LocalVirtualResourceName::create(kStockOpcode5LocalConsistencyTarget);
    if (!target) {
        return failure(
            ResourceConsistencyErrorCode::invalid_configuration,
            "The fixed local consistency target is invalid");
    }
    auto resolution = environment.resolver().resolve(*target.name);
    if (!resolution) {
        return failure(map_resolution_error(resolution.code), resolution.context);
    }

    auto file = std::move(*resolution.file);
    const auto selected_root = file.root_id();
    auto inspection =
        local::inspect_local_resource_file(file, inspection_limits);
    if (!inspection) {
        return failure(
            map_inspection_error(inspection.error->code),
            inspection.error->context);
    }

    auto material = make_resource_consistency_material(
        inspection.inspection->byte_count,
        inspection.inspection->compatibility_md5);
    if (!material) {
        return failure(
            material.error ? material.error->code
                           : ResourceConsistencyErrorCode::invalid_material,
            material.error
                ? material.error->context
                : "Unable to construct local consistency material");
    }

    try {
        auto lifetime =
            std::make_unique<LocalFileSessionLifetime>(std::move(file));
        auto operation =
            std::make_unique<PreparedLocalResourceConsistencyOperation>(
                std::move(*material.material), std::move(lifetime));
        const PreparedLocalResourceConsistencyMetadata metadata{
            root_count,
            selected_root,
            inspection.inspection->byte_count,
            inspection.inspection->compatibility_md5.size(),
        };
        return PreparedLocalResourceConsistencyProviderCreateResult{
            std::unique_ptr<PreparedLocalResourceConsistencyProvider>{
                new PreparedLocalResourceConsistencyProvider{
                    metadata, std::move(operation)}},
            std::nullopt,
        };
    } catch (...) {
        return failure(
            ResourceConsistencyErrorCode::provider_error,
            "Unable to retain prepared local consistency provider state");
    }
}

ResourceConsistencyBeginResult PreparedLocalResourceConsistencyProvider::begin(
    const ResourceConsistencyRequirements& requirements)
{
    if (consumed_ || !prepared_operation_) {
        return ResourceConsistencyBeginResult::failed({
            ResourceConsistencyErrorCode::unavailable,
            "Prepared local resource consistency provider was already consumed",
        });
    }
    if (requirements.compatibility_profile() !=
            ResourceConsistencyCompatibilityProfile::
                stock_protocol_48_opcode5_single_resource ||
        requirements.material_count() != 1U ||
        requirements.opaque_bytes_per_material() != hash::kMd5DigestSize) {
        return ResourceConsistencyBeginResult::failed({
            ResourceConsistencyErrorCode::invalid_configuration,
            "Prepared local resource consistency profile is unsupported",
        });
    }

    consumed_ = true;
    return ResourceConsistencyBeginResult::started(
        std::move(prepared_operation_));
}

const PreparedLocalResourceConsistencyMetadata&
PreparedLocalResourceConsistencyProvider::metadata() const noexcept
{
    return metadata_;
}

std::size_t
PreparedLocalResourceConsistencyProvider::validated_root_count() const noexcept
{
    return metadata_.validated_root_count;
}

local::LocalResourceRootId
PreparedLocalResourceConsistencyProvider::selected_root_id() const noexcept
{
    return metadata_.selected_root_id;
}

std::uint32_t PreparedLocalResourceConsistencyProvider::byte_count() const noexcept
{
    return metadata_.byte_count;
}

std::size_t
PreparedLocalResourceConsistencyProvider::opaque_byte_count() const noexcept
{
    return metadata_.opaque_byte_count;
}

bool PreparedLocalResourceConsistencyProvider::consumed() const noexcept
{
    return consumed_;
}

} // namespace hlclient::resource_consistency
