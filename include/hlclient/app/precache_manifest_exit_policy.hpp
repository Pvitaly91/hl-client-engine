#pragma once

#include <hlclient/goldsrc/precache_manifest.hpp>

namespace hlclient::app {

[[nodiscard]] constexpr int precache_manifest_exit_code(
    const goldsrc::PrecacheManifestCompleteness completeness) noexcept
{
    return completeness == goldsrc::PrecacheManifestCompleteness::
                               complete_for_supported_local_profile
               ? 0
               : 1;
}

} // namespace hlclient::app
