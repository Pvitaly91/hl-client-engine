#pragma once

#include <string_view>

namespace hlclient::core {

inline constexpr std::string_view kApplicationName = "HL Client Engine";
inline constexpr std::string_view kVersion = "0.0.1-dev";

[[nodiscard]] constexpr std::string_view build_platform() noexcept
{
#if defined(_WIN32) && defined(_M_IX86)
    return "Windows x86";
#elif defined(_WIN32) && defined(_M_X64)
    return "Windows x64";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__linux__) && defined(__i386__)
    return "Linux x86";
#elif defined(__linux__) && defined(__x86_64__)
    return "Linux x64";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown platform";
#endif
}

} // namespace hlclient::core
