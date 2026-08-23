#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {
class Opcode5ResourceResponseBuilder;
}

namespace hlclient::resource_consistency {

// Project safety bounds. They describe this provider boundary, not stock
// engine or filesystem limits.
inline constexpr std::size_t kDefaultMaximumResourceConsistencyMaterials = 1U;
inline constexpr std::size_t kMaximumResourceConsistencyMaterials = 256U;
inline constexpr std::size_t kDefaultMaximumResourceConsistencyOpaqueBytes =
    16U;
inline constexpr std::size_t kMaximumResourceConsistencyOpaqueBytes = 4'096U;
inline constexpr std::size_t kResourceConsistencyDiagnosticTextLimit = 256U;

struct ResourceConsistencyLimits {
    std::size_t maximum_material_count{
        kDefaultMaximumResourceConsistencyMaterials};
    std::size_t maximum_opaque_bytes_per_material{
        kDefaultMaximumResourceConsistencyOpaqueBytes};
};

[[nodiscard]] bool valid_resource_consistency_limits(
    const ResourceConsistencyLimits& limits) noexcept;

enum class ResourceConsistencyCompatibilityProfile {
    stock_protocol_48_opcode5_single_resource,
};

// Path-free requirements only. A future M3.2 provider maps this profile to an
// approved local-resource policy; sign-on never passes ResourceName bytes or a
// filesystem path into the provider boundary.
class ResourceConsistencyRequirements final {
public:
    [[nodiscard]] ResourceConsistencyCompatibilityProfile
    compatibility_profile() const noexcept;
    [[nodiscard]] std::size_t material_count() const noexcept;
    [[nodiscard]] std::size_t opaque_bytes_per_material() const noexcept;

    [[nodiscard]] static std::optional<ResourceConsistencyRequirements>
    stock_opcode5_single_resource() noexcept;

private:
    ResourceConsistencyRequirements(
        ResourceConsistencyCompatibilityProfile compatibility_profile,
        std::size_t material_count,
        std::size_t opaque_bytes_per_material) noexcept;

    ResourceConsistencyCompatibilityProfile compatibility_profile_{
        ResourceConsistencyCompatibilityProfile::
            stock_protocol_48_opcode5_single_resource};
    std::size_t material_count_{0U};
    std::size_t opaque_bytes_per_material_{0U};
};

enum class ResourceConsistencyErrorCode {
    unavailable,
    invalid_configuration,
    provider_error,
    invalid_material,
    material_too_large,
    cancelled,
    timed_out,
};

struct ResourceConsistencyError {
    ResourceConsistencyErrorCode code{
        ResourceConsistencyErrorCode::provider_error};
    std::string context;
};

class ResourceConsistencyMaterial;
struct ResourceConsistencyMaterialCreateResult;

[[nodiscard]] ResourceConsistencyMaterialCreateResult
make_resource_consistency_material(
    std::uint32_t byte_count,
    std::span<const std::byte> opaque_bytes,
    ResourceConsistencyLimits limits);

class ResourceConsistencyMaterial final {
public:
    ResourceConsistencyMaterial(const ResourceConsistencyMaterial&) = delete;
    ResourceConsistencyMaterial& operator=(
        const ResourceConsistencyMaterial&) = delete;
    ResourceConsistencyMaterial(ResourceConsistencyMaterial&&) noexcept = default;
    ResourceConsistencyMaterial& operator=(
        ResourceConsistencyMaterial&&) noexcept = default;
    ~ResourceConsistencyMaterial() = default;

    [[nodiscard]] std::uint32_t byte_count() const noexcept;
    [[nodiscard]] std::size_t opaque_byte_count() const noexcept;

private:
    friend class hlclient::goldsrc::Opcode5ResourceResponseBuilder;
    friend ResourceConsistencyMaterialCreateResult
    make_resource_consistency_material(
        std::uint32_t byte_count,
        std::span<const std::byte> opaque_bytes,
        ResourceConsistencyLimits limits);

    ResourceConsistencyMaterial(
        std::uint32_t byte_count,
        std::vector<std::byte> opaque_bytes) noexcept;

    std::uint32_t byte_count_{0U};
    // Sensitive provider material remains private and has no raw getter.
    std::vector<std::byte> opaque_bytes_;
};

struct ResourceConsistencyMaterialCreateResult {
    std::optional<ResourceConsistencyMaterial> material;
    std::optional<ResourceConsistencyError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return material.has_value();
    }
};

[[nodiscard]] ResourceConsistencyMaterialCreateResult
make_resource_consistency_material(
    std::uint32_t byte_count,
    std::span<const std::byte> opaque_bytes,
    ResourceConsistencyLimits limits = {});

class IResourceConsistencySessionLifetime {
public:
    virtual ~IResourceConsistencySessionLifetime() = default;

    IResourceConsistencySessionLifetime(
        const IResourceConsistencySessionLifetime&) = delete;
    IResourceConsistencySessionLifetime& operator=(
        const IResourceConsistencySessionLifetime&) = delete;
    IResourceConsistencySessionLifetime(
        IResourceConsistencySessionLifetime&&) = delete;
    IResourceConsistencySessionLifetime& operator=(
        IResourceConsistencySessionLifetime&&) = delete;

protected:
    IResourceConsistencySessionLifetime() = default;
};

// Move-only owner of one bounded typed material and a provider-specific
// lifetime guard. Moving material out never releases the guard early.
class ResourceConsistencySession final {
public:
    explicit ResourceConsistencySession(
        ResourceConsistencyMaterial material,
        std::unique_ptr<IResourceConsistencySessionLifetime> lifetime = {})
        noexcept;
    ~ResourceConsistencySession() = default;

    ResourceConsistencySession(ResourceConsistencySession&& other) noexcept;
    ResourceConsistencySession& operator=(
        ResourceConsistencySession&& other) noexcept;
    ResourceConsistencySession(const ResourceConsistencySession&) = delete;
    ResourceConsistencySession& operator=(
        const ResourceConsistencySession&) = delete;

    [[nodiscard]] bool has_material() const noexcept;
    [[nodiscard]] std::size_t opaque_byte_count() const noexcept;
    [[nodiscard]] std::optional<ResourceConsistencyMaterial>
    take_material() noexcept;

private:
    // Material is destroyed before its provider-specific lifetime guard.
    std::unique_ptr<IResourceConsistencySessionLifetime> lifetime_;
    std::optional<ResourceConsistencyMaterial> material_;
};

enum class ResourceConsistencyUpdateState {
    pending,
    succeeded,
    failed,
};

struct ResourceConsistencyUpdateResult {
    ResourceConsistencyUpdateState state{
        ResourceConsistencyUpdateState::pending};
    std::optional<ResourceConsistencySession> session;
    std::optional<ResourceConsistencyError> error;

    [[nodiscard]] static ResourceConsistencyUpdateResult pending();
    [[nodiscard]] static ResourceConsistencyUpdateResult succeeded(
        ResourceConsistencySession session);
    [[nodiscard]] static ResourceConsistencyUpdateResult failed(
        ResourceConsistencyError error);
};

class ResourceConsistencyOperation {
public:
    virtual ~ResourceConsistencyOperation() = default;

    ResourceConsistencyOperation(const ResourceConsistencyOperation&) = delete;
    ResourceConsistencyOperation& operator=(
        const ResourceConsistencyOperation&) = delete;
    ResourceConsistencyOperation(ResourceConsistencyOperation&&) = delete;
    ResourceConsistencyOperation& operator=(
        ResourceConsistencyOperation&&) = delete;

    // Called from the sign-on update path. Implementations must return
    // promptly without waiting for filesystem, network, or worker completion;
    // pending work is reported with ResourceConsistencyUpdateResult::pending().
    [[nodiscard]] virtual ResourceConsistencyUpdateResult update() = 0;
    // Cancellation must likewise be prompt and nonblocking.
    virtual void cancel() noexcept = 0;

protected:
    ResourceConsistencyOperation() = default;
};

struct ResourceConsistencyBeginResult {
    std::unique_ptr<ResourceConsistencyOperation> operation;
    std::optional<ResourceConsistencyError> error;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] static ResourceConsistencyBeginResult started(
        std::unique_ptr<ResourceConsistencyOperation> operation);
    [[nodiscard]] static ResourceConsistencyBeginResult failed(
        ResourceConsistencyError error);
};

class IResourceConsistencyProvider {
public:
    virtual ~IResourceConsistencyProvider() = default;

    IResourceConsistencyProvider(const IResourceConsistencyProvider&) = delete;
    IResourceConsistencyProvider& operator=(
        const IResourceConsistencyProvider&) = delete;
    IResourceConsistencyProvider(IResourceConsistencyProvider&&) = delete;
    IResourceConsistencyProvider& operator=(
        IResourceConsistencyProvider&&) = delete;

    // Called synchronously from the sign-on update path and therefore must be
    // nonblocking. Slow work belongs behind the owning operation returned here.
    // A provider passed through a non-owning stage/coordinator pointer must
    // outlive that stage/coordinator; returned operations own their own state.
    [[nodiscard]] virtual ResourceConsistencyBeginResult begin(
        const ResourceConsistencyRequirements& requirements) = 0;

protected:
    IResourceConsistencyProvider() = default;
};

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceConsistencyErrorCode code) noexcept
{
    switch (code) {
    case ResourceConsistencyErrorCode::unavailable: return "unavailable";
    case ResourceConsistencyErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ResourceConsistencyErrorCode::provider_error: return "provider_error";
    case ResourceConsistencyErrorCode::invalid_material: return "invalid_material";
    case ResourceConsistencyErrorCode::material_too_large:
        return "material_too_large";
    case ResourceConsistencyErrorCode::cancelled: return "cancelled";
    case ResourceConsistencyErrorCode::timed_out: return "timed_out";
    }
    return "unknown";
}

} // namespace hlclient::resource_consistency
