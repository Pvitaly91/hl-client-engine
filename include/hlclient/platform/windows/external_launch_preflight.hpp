#pragma once
#include <hlclient/platform/windows/binary_identity.hpp>
#include <filesystem>
#include <span>
#include <string_view>

namespace hlclient::platform::windows {
enum class ResearchIsolationConflict { absent, present, unavailable };
struct ResearchIsolationObservation {
    ResearchIsolationConflict state{ResearchIsolationConflict::unavailable};
    std::uint32_t native_error{};
};
// Read-only inspection; never creates/deletes filters, sessions or providers.
[[nodiscard]] ResearchIsolationObservation observe_research_isolation_conflict(
    const std::filesystem::path& executable) noexcept;
// Pure exact match used by the bounded WFP reader and project-owned fixtures.
[[nodiscard]] bool is_research_application_block(std::wstring_view filter_name,
    bool outbound_v4, bool blocking, std::span<const std::byte> filter_app,
    std::span<const std::byte> expected_app) noexcept;
struct OfficialSteamRuntimeObservation {
    WindowsBinaryIdentityResult binary;
    bool valve_signer{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return binary && binary.identity->pe_machine == WindowsPeMachine::x86 && valve_signer;
    }
};
[[nodiscard]] OfficialSteamRuntimeObservation observe_official_steam_runtime(
    const std::filesystem::path& runtime) noexcept;
} // namespace hlclient::platform::windows
