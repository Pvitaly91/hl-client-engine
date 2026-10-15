#pragma once

#include <hlclient/core/presentation_settings.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::core {
inline constexpr std::string_view kExternalLaunchSchema = "hlclient.external-launch.v3";
inline constexpr std::size_t kExternalLaunchMaximumBytes = 16'384;
struct ExternalLaunchConfiguration final {
    std::string endpoint{"192.168.0.104:27016"};
    std::string name{"Player"};
    std::string asset_root{"D:\\DEV\\HLCLIENT-RESEARCH\\Half-Life"};
    std::string steam_api_runtime{"D:\\DEV\\HLCLIENT-RESEARCH\\Half-Life\\steam_api.dll"};
    VideoSettings video;
    ImageAdjustments image;
    HudScaleSettings hud_scale;
    unsigned volume{35};
    bool movement_diagnostics{};
};
struct ExternalLaunchConfigurationResult final {
    std::optional<ExternalLaunchConfiguration> configuration;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return configuration.has_value(); }
};
// Literal unicast IPv4 only. Default port 27015; optional exact "connect "
// prefix is parsed as input, never submitted as a console or shell command.
[[nodiscard]] std::optional<std::string> normalize_external_endpoint(std::string_view);
[[nodiscard]] bool valid_external_player_name(std::string_view) noexcept;
[[nodiscard]] std::string validate_external_launch_configuration(const ExternalLaunchConfiguration&);
[[nodiscard]] ExternalLaunchConfigurationResult decode_external_launch_configuration(std::string_view);
[[nodiscard]] std::optional<std::string> encode_external_launch_configuration(const ExternalLaunchConfiguration&);
// JSON string utility for project-owned reports; invalid controls are escaped.
[[nodiscard]] std::string external_json_string(std::string_view);
struct SceneAntialiasingObservation {
    unsigned requested_samples{},actual_samples{};
    std::string fallback_reason;
};
// Only a bounded, identity-bound renderer observation is displayable as actual
// AA. A request stored in launcher.json is never evidence that MSAA is active.
[[nodiscard]] std::optional<SceneAntialiasingObservation> decode_external_msaa_observation(
    std::string_view json,std::string_view expected_run_id);
enum class ExternalLauncherPhase { ready, starting, active, cancelling, completed, failed };
class ExternalLauncherLifecycle final {
public:
    [[nodiscard]] bool begin() noexcept;
    [[nodiscard]] bool child_started() noexcept;
    [[nodiscard]] bool cancel() noexcept;
    void finish(bool success) noexcept;
    [[nodiscard]] ExternalLauncherPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool busy() const noexcept;
private:
    ExternalLauncherPhase phase_{ExternalLauncherPhase::ready};
};
} // namespace hlclient::core
