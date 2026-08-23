#pragma once

#include <hlclient/local_resources/local_resource_identity.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

namespace hlclient::local_resources {

namespace detail {
struct LocalResourceEnvironmentToken;
}

class LocalResourceEnvironment;

// Identifies the local sandbox and metadata contract under which a locator
// was created. It does not make any content-validity or compatibility claim
// about the resource bytes.
enum class LocalResourceLocatorCompatibilityProfile {
    validated_fixed_local_volume_v1,
};

// Path-free, handle-free metadata for reopening one previously resolved local
// candidate. Construction is restricted to LocalResourceEnvironment so an
// arbitrary root ordinal cannot be promoted to a capability.
class LocalResourceLocator final {
public:
    ~LocalResourceLocator();
    LocalResourceLocator(const LocalResourceLocator&);
    LocalResourceLocator(LocalResourceLocator&&) noexcept;
    LocalResourceLocator& operator=(const LocalResourceLocator&) = delete;
    LocalResourceLocator& operator=(LocalResourceLocator&&) = delete;

    [[nodiscard]] LocalResourceRootId root_id() const noexcept;
    [[nodiscard]] const LocalVirtualResourceName& virtual_name() const noexcept;
    [[nodiscard]] LocalStableFileIdentity expected_identity() const noexcept;
    [[nodiscard]] std::uint64_t expected_file_size() const noexcept;
    [[nodiscard]] LocalResourceLocatorCompatibilityProfile
    compatibility_profile() const noexcept;

private:
    friend class LocalResourceEnvironment;

    LocalResourceLocator(
        LocalResourceRootId root_id,
        LocalVirtualResourceName virtual_name,
        LocalStableFileIdentity expected_identity,
        std::uint64_t expected_file_size,
        LocalResourceLocatorCompatibilityProfile compatibility_profile,
        std::weak_ptr<const detail::LocalResourceEnvironmentToken>
            environment_token) noexcept;

    LocalResourceRootId root_id_;
    LocalVirtualResourceName virtual_name_;
    LocalStableFileIdentity expected_identity_;
    std::uint64_t expected_file_size_{0U};
    LocalResourceLocatorCompatibilityProfile compatibility_profile_{
        LocalResourceLocatorCompatibilityProfile::
            validated_fixed_local_volume_v1};
    // Opaque instance provenance. This neither exposes nor extends the
    // lifetime of validated root handles.
    std::weak_ptr<const detail::LocalResourceEnvironmentToken>
        environment_token_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceLocatorCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case LocalResourceLocatorCompatibilityProfile::
        validated_fixed_local_volume_v1:
        return "validated_fixed_local_volume_v1";
    }
    return "unknown";
}

} // namespace hlclient::local_resources
