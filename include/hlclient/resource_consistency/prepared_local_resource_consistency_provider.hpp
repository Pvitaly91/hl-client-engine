#pragma once

#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_file_inspection.hpp>
#include <hlclient/resource_consistency/provider.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::resource_consistency {

inline constexpr std::string_view kStockOpcode5LocalConsistencyTarget =
    "tempdecal.wad";

struct PreparedLocalResourceConsistencyProviderLimits {
    local_resources::LocalResourceResolverLimits resolver;
    local_resources::LocalResourceFileInspectionLimits inspection;
};

[[nodiscard]] bool valid_prepared_local_resource_consistency_provider_limits(
    const PreparedLocalResourceConsistencyProviderLimits& limits) noexcept;

struct PreparedLocalResourceConsistencyMetadata {
    std::size_t validated_root_count{0U};
    local_resources::LocalResourceRootId selected_root_id;
    std::uint32_t byte_count{0U};
    std::size_t opaque_byte_count{0U};
};

struct PreparedLocalResourceConsistencyProviderCreateResult;

// Prepared synchronously by the application composition root before network
// initialization. begin()/update() later perform no filesystem work and return
// promptly. The fixed target is selected by the compatibility profile, never
// by server bytes or a CLI path.
class PreparedLocalResourceConsistencyProvider final
    : public IResourceConsistencyProvider {
public:
    [[nodiscard]] static PreparedLocalResourceConsistencyProviderCreateResult
    prepare(
        local_resources::LocalResourceSearchRoots roots,
        PreparedLocalResourceConsistencyProviderLimits limits = {});
    [[nodiscard]] static PreparedLocalResourceConsistencyProviderCreateResult
    prepare(
        const local_resources::LocalResourceEnvironment& environment,
        local_resources::LocalResourceFileInspectionLimits inspection_limits =
            {});

    ~PreparedLocalResourceConsistencyProvider() override = default;
    PreparedLocalResourceConsistencyProvider(
        const PreparedLocalResourceConsistencyProvider&) = delete;
    PreparedLocalResourceConsistencyProvider& operator=(
        const PreparedLocalResourceConsistencyProvider&) = delete;

    [[nodiscard]] ResourceConsistencyBeginResult begin(
        const ResourceConsistencyRequirements& requirements) override;

    [[nodiscard]] const PreparedLocalResourceConsistencyMetadata& metadata()
        const noexcept;
    [[nodiscard]] std::size_t validated_root_count() const noexcept;
    [[nodiscard]] local_resources::LocalResourceRootId selected_root_id()
        const noexcept;
    [[nodiscard]] std::uint32_t byte_count() const noexcept;
    [[nodiscard]] std::size_t opaque_byte_count() const noexcept;
    [[nodiscard]] bool consumed() const noexcept;

private:
    PreparedLocalResourceConsistencyProvider(
        PreparedLocalResourceConsistencyMetadata metadata,
        std::unique_ptr<ResourceConsistencyOperation> prepared_operation)
        noexcept;

    PreparedLocalResourceConsistencyMetadata metadata_;
    std::unique_ptr<ResourceConsistencyOperation> prepared_operation_;
    bool consumed_{false};
};

struct PreparedLocalResourceConsistencyProviderCreateResult {
    std::unique_ptr<PreparedLocalResourceConsistencyProvider> provider;
    std::optional<ResourceConsistencyError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return provider != nullptr;
    }
};

} // namespace hlclient::resource_consistency
