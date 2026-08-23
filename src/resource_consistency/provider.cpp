#include <hlclient/resource_consistency/provider.hpp>

#include <algorithm>
#include <utility>

namespace hlclient::resource_consistency {
namespace {

[[nodiscard]] ResourceConsistencyError bounded_error(
    const ResourceConsistencyErrorCode code,
    const std::string_view context)
{
    const auto bounded = context.substr(
        0U,
        (std::min)(context.size(), kResourceConsistencyDiagnosticTextLimit));
    return ResourceConsistencyError{
        code,
        std::string{bounded.data(), bounded.size()},
    };
}

} // namespace

bool valid_resource_consistency_limits(
    const ResourceConsistencyLimits& limits) noexcept
{
    return limits.maximum_material_count > 0U &&
           limits.maximum_material_count <=
               kMaximumResourceConsistencyMaterials &&
           limits.maximum_opaque_bytes_per_material > 0U &&
           limits.maximum_opaque_bytes_per_material <=
               kMaximumResourceConsistencyOpaqueBytes;
}

ResourceConsistencyRequirements::ResourceConsistencyRequirements(
    const ResourceConsistencyCompatibilityProfile compatibility_profile,
    const std::size_t material_count,
    const std::size_t opaque_bytes_per_material) noexcept
    : compatibility_profile_{compatibility_profile},
      material_count_{material_count},
      opaque_bytes_per_material_{opaque_bytes_per_material}
{
}

ResourceConsistencyCompatibilityProfile
ResourceConsistencyRequirements::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

std::size_t ResourceConsistencyRequirements::material_count() const noexcept
{
    return material_count_;
}

std::size_t
ResourceConsistencyRequirements::opaque_bytes_per_material() const noexcept
{
    return opaque_bytes_per_material_;
}

std::optional<ResourceConsistencyRequirements>
ResourceConsistencyRequirements::stock_opcode5_single_resource() noexcept
{
    return ResourceConsistencyRequirements{
        ResourceConsistencyCompatibilityProfile::
            stock_protocol_48_opcode5_single_resource,
        1U,
        16U,
    };
}

ResourceConsistencyMaterial::ResourceConsistencyMaterial(
    const std::uint32_t byte_count,
    std::vector<std::byte> opaque_bytes) noexcept
    : byte_count_{byte_count}, opaque_bytes_{std::move(opaque_bytes)}
{
}

std::uint32_t ResourceConsistencyMaterial::byte_count() const noexcept
{
    return byte_count_;
}

std::size_t ResourceConsistencyMaterial::opaque_byte_count() const noexcept
{
    return opaque_bytes_.size();
}

ResourceConsistencyMaterialCreateResult make_resource_consistency_material(
    const std::uint32_t byte_count,
    const std::span<const std::byte> opaque_bytes,
    const ResourceConsistencyLimits limits)
{
    if (!valid_resource_consistency_limits(limits)) {
        return ResourceConsistencyMaterialCreateResult{
            std::nullopt,
            bounded_error(
                ResourceConsistencyErrorCode::invalid_configuration,
                "Resource-consistency limits are outside project hard caps"),
        };
    }
    if (opaque_bytes.empty()) {
        return ResourceConsistencyMaterialCreateResult{
            std::nullopt,
            bounded_error(
                ResourceConsistencyErrorCode::invalid_material,
                "Resource-consistency opaque material is empty"),
        };
    }
    if (opaque_bytes.size() > limits.maximum_opaque_bytes_per_material) {
        return ResourceConsistencyMaterialCreateResult{
            std::nullopt,
            bounded_error(
                ResourceConsistencyErrorCode::material_too_large,
                "Resource-consistency opaque material exceeds its configured bound"),
        };
    }

    try {
        return ResourceConsistencyMaterialCreateResult{
            ResourceConsistencyMaterial{
                byte_count,
                std::vector<std::byte>{
                    opaque_bytes.begin(), opaque_bytes.end()}},
            std::nullopt,
        };
    } catch (...) {
        return ResourceConsistencyMaterialCreateResult{
            std::nullopt,
            bounded_error(
                ResourceConsistencyErrorCode::provider_error,
                "Unable to retain bounded resource-consistency material"),
        };
    }
}

ResourceConsistencySession::ResourceConsistencySession(
    ResourceConsistencyMaterial material,
    std::unique_ptr<IResourceConsistencySessionLifetime> lifetime) noexcept
    : lifetime_{std::move(lifetime)}, material_{std::move(material)}
{
}

ResourceConsistencySession::ResourceConsistencySession(
    ResourceConsistencySession&& other) noexcept
    : lifetime_{std::move(other.lifetime_)},
      material_{std::move(other.material_)}
{
    other.material_.reset();
}

ResourceConsistencySession& ResourceConsistencySession::operator=(
    ResourceConsistencySession&& other) noexcept
{
    if (this != &other) {
        material_.reset();
        lifetime_.reset();
        lifetime_ = std::move(other.lifetime_);
        material_ = std::move(other.material_);
        other.material_.reset();
    }
    return *this;
}

bool ResourceConsistencySession::has_material() const noexcept
{
    return material_.has_value();
}

std::size_t ResourceConsistencySession::opaque_byte_count() const noexcept
{
    return material_ ? material_->opaque_byte_count() : 0U;
}

std::optional<ResourceConsistencyMaterial>
ResourceConsistencySession::take_material() noexcept
{
    if (!material_) {
        return std::nullopt;
    }
    std::optional<ResourceConsistencyMaterial> result{std::move(*material_)};
    material_.reset();
    return result;
}

ResourceConsistencyUpdateResult ResourceConsistencyUpdateResult::pending()
{
    return ResourceConsistencyUpdateResult{
        ResourceConsistencyUpdateState::pending,
        std::nullopt,
        std::nullopt,
    };
}

ResourceConsistencyUpdateResult ResourceConsistencyUpdateResult::succeeded(
    ResourceConsistencySession session)
{
    return ResourceConsistencyUpdateResult{
        ResourceConsistencyUpdateState::succeeded,
        std::move(session),
        std::nullopt,
    };
}

ResourceConsistencyUpdateResult ResourceConsistencyUpdateResult::failed(
    ResourceConsistencyError error)
{
    if (error.context.size() > kResourceConsistencyDiagnosticTextLimit) {
        error.context.resize(kResourceConsistencyDiagnosticTextLimit);
    }
    return ResourceConsistencyUpdateResult{
        ResourceConsistencyUpdateState::failed,
        std::nullopt,
        std::move(error),
    };
}

ResourceConsistencyBeginResult::operator bool() const noexcept
{
    return operation != nullptr;
}

ResourceConsistencyBeginResult ResourceConsistencyBeginResult::started(
    std::unique_ptr<ResourceConsistencyOperation> operation)
{
    if (!operation) {
        return failed(bounded_error(
            ResourceConsistencyErrorCode::provider_error,
            "Resource-consistency provider did not create an operation"));
    }
    return ResourceConsistencyBeginResult{
        std::move(operation),
        std::nullopt,
    };
}

ResourceConsistencyBeginResult ResourceConsistencyBeginResult::failed(
    ResourceConsistencyError error)
{
    if (error.context.size() > kResourceConsistencyDiagnosticTextLimit) {
        error.context.resize(kResourceConsistencyDiagnosticTextLimit);
    }
    return ResourceConsistencyBeginResult{
        nullptr,
        std::move(error),
    };
}

} // namespace hlclient::resource_consistency
