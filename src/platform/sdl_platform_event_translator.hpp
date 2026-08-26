#pragma once

#include <hlclient/platform/platform_event.hpp>

#include <SDL3/SDL_events.h>

#include <optional>

namespace hlclient::platform::detail {

// Private SDL boundary. This header is intentionally not installed beneath
// include/hlclient; only the SDL platform target and its focused tests use it.
class SdlPlatformEventTranslator final {
public:
    [[nodiscard]] std::optional<PlatformEvent> translate(const SDL_Event& event) const noexcept;
};

} // namespace hlclient::platform::detail
