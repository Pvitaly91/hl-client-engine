#pragma once
#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::core {
// Pure opt-in argument transformation; process ownership remains in the host.
// Both clients retain the same approved image, root, provider and server endpoint.
// Both also use ordinary focused-window audio. The original E9 diagnostic
// listener/mover names are retained without permanently muting either client.
struct RemoteAudioPeerPlan {
    std::vector<std::wstring> listener, mover;
};
inline std::optional<RemoteAudioPeerPlan> remote_audio_peer_plan(
    const std::vector<std::wstring>& arguments) {
    const auto value=[&](std::wstring_view key)->std::wstring_view {
        const auto it=std::find(arguments.begin(),arguments.end(),key);
        return it!=arguments.end() && std::next(it)!=arguments.end() ? *std::next(it) : std::wstring_view{};
    };
    const bool timed = std::find(arguments.begin(), arguments.end(), L"--live-session-seconds") != arguments.end();
    const bool unlimited = std::find(arguments.begin(), arguments.end(), L"--live-session-unlimited") != arguments.end();
    if(value(L"--renderer")!=L"opengl" || value(L"--live-input")!=L"keyboard-mouse" ||
       value(L"--stop-after")!=L"live-visual-control" || value(L"--game")!=L"valve" ||
       !value(L"--connect").starts_with(L"127.0.0.1:") || value(L"--name").empty() ||
       value(L"--auth-provider")!=L"steam" || value(L"--steam-api-runtime").empty() ||
       value(L"--basedir").empty() || timed == unlimited ||
       (timed && value(L"--live-session-seconds").empty()) ||
       std::find(arguments.begin(),arguments.end(),L"--audio-volume")!=arguments.end() ||
       std::find(arguments.begin(),arguments.end(),L"--audio-on-focus-loss")!=arguments.end()) return {};
    RemoteAudioPeerPlan plan{arguments,arguments};
    *std::next(std::find(plan.listener.begin(),plan.listener.end(),L"--name"))=L"HLC_E9_A";
    *std::next(std::find(plan.mover.begin(),plan.mover.end(),L"--name"))=L"HLC_E9_B";
    return plan;
}
}
