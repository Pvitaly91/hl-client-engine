#pragma once

namespace hlclient::platform {

class SdlRuntime final {
public:
    SdlRuntime();
    ~SdlRuntime() noexcept;

    SdlRuntime(const SdlRuntime&) = delete;
    SdlRuntime& operator=(const SdlRuntime&) = delete;
    SdlRuntime(SdlRuntime&&) = delete;
    SdlRuntime& operator=(SdlRuntime&&) = delete;
};

} // namespace hlclient::platform
