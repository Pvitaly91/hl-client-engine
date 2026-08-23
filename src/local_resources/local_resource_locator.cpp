#include <hlclient/local_resources/local_resource_locator.hpp>

#include <utility>

namespace hlclient::local_resources {

LocalResourceLocator::LocalResourceLocator(
    const LocalResourceRootId root_id,
    LocalVirtualResourceName virtual_name,
    const LocalStableFileIdentity expected_identity,
    const std::uint64_t expected_file_size,
    const LocalResourceLocatorCompatibilityProfile compatibility_profile,
    std::weak_ptr<const detail::LocalResourceEnvironmentToken>
        environment_token) noexcept
    : root_id_{root_id},
      virtual_name_{std::move(virtual_name)},
      expected_identity_{expected_identity},
      expected_file_size_{expected_file_size},
      compatibility_profile_{compatibility_profile},
      environment_token_{std::move(environment_token)}
{
}

LocalResourceLocator::~LocalResourceLocator() = default;
LocalResourceLocator::LocalResourceLocator(const LocalResourceLocator&) =
    default;
LocalResourceLocator::LocalResourceLocator(LocalResourceLocator&&) noexcept =
    default;

LocalResourceRootId LocalResourceLocator::root_id() const noexcept
{
    return root_id_;
}

const LocalVirtualResourceName& LocalResourceLocator::virtual_name()
    const noexcept
{
    return virtual_name_;
}

LocalStableFileIdentity LocalResourceLocator::expected_identity() const noexcept
{
    return expected_identity_;
}

std::uint64_t LocalResourceLocator::expected_file_size() const noexcept
{
    return expected_file_size_;
}

LocalResourceLocatorCompatibilityProfile
LocalResourceLocator::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

} // namespace hlclient::local_resources
