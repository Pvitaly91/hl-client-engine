#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace hlclient::system_diagnostics {

// Read-only WPR capability check. It never starts, stops or cancels a trace.
// The built-in profiles are considered lightweight only when their declared
// ring buffers fit this deliberately conservative helper budget.
inline constexpr std::uint64_t kWprMaximumDeclaredBufferBytes = 256ULL * 1024ULL * 1024ULL;

struct WprProbeResult final {
    std::string status{"not_probed"};
    std::string reason{"not_probed"};
    std::string version;
    bool installed{};
    bool cpu_light_supported{};
    bool gpu_light_supported{};
    // True also when active-session state cannot be determined: fail closed.
    bool active_other_recording{};
    std::optional<std::uint64_t> declared_cpu_buffer_bytes;
    std::optional<std::uint64_t> declared_gpu_buffer_bytes;
    bool within_memory_budget{};
};

// Each fixed, read-only WPR query has its own bounded deadline. The child
// process and pipe are owned by this call. No command lines or WPR output are
// written to the game's diagnostics, because those may contain private data.
[[nodiscard]] WprProbeResult probe_wpr_read_only(
    std::chrono::milliseconds timeout = std::chrono::milliseconds{2000}) noexcept;

} // namespace hlclient::system_diagnostics
