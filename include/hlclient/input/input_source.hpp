#pragma once

#include <hlclient/input/input_event.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace hlclient::input {

class IInputSource {
public:
    virtual ~IInputSource() = default;

    virtual void begin_frame() = 0;
    [[nodiscard]] virtual bool poll_event(InputEvent& event) = 0;
    virtual void end_frame() = 0;
};

struct NullInputSourceConfig final {
    InputFocusState focus_state{InputFocusState::focused};
    InputCaptureState capture_state{InputCaptureState::released};
};

struct ScriptedInputSourceLimits final {
    static constexpr std::size_t default_maximum_frames = 1'024U;
    static constexpr std::size_t hard_maximum_frames = 8'192U;
    static constexpr std::size_t default_maximum_events_per_frame = 1'024U;
    static constexpr std::size_t hard_maximum_events_per_frame = 8'192U;
    static constexpr std::size_t default_maximum_total_events = 65'536U;
    static constexpr std::size_t hard_maximum_total_events = 1'048'576U;

    std::size_t maximum_frames{default_maximum_frames};
    std::size_t maximum_events_per_frame{default_maximum_events_per_frame};
    std::size_t maximum_total_events{default_maximum_total_events};
};

enum class InputSourceErrorCode : std::uint8_t {
    invalid_configuration,
    frame_limit_exceeded,
    per_frame_event_limit_exceeded,
    total_event_limit_exceeded,
    frame_already_active,
    frame_not_active,
    frame_not_consumed,
};

class InputSourceException final : public std::runtime_error {
public:
    InputSourceException(InputSourceErrorCode code, std::string message);

    [[nodiscard]] InputSourceErrorCode code() const noexcept { return code_; }

private:
    InputSourceErrorCode code_;
};

class NullInputSource final : public IInputSource {
public:
    explicit NullInputSource(NullInputSourceConfig config = {});

    void begin_frame() override;
    [[nodiscard]] bool poll_event(InputEvent& event) override;
    void end_frame() override;

    [[nodiscard]] bool frame_active() const noexcept { return frame_active_; }
    [[nodiscard]] const NullInputSourceConfig& config() const noexcept { return config_; }

private:
    NullInputSourceConfig config_{};
    std::size_t next_initial_event_{0U};
    bool initial_state_published_{false};
    bool frame_active_{false};
};

class ScriptedInputSource final : public IInputSource {
public:
    using Frame = std::vector<InputEvent>;
    using Script = std::vector<Frame>;

    explicit ScriptedInputSource(
        Script frames,
        ScriptedInputSourceLimits limits = {});

    void begin_frame() override;
    [[nodiscard]] bool poll_event(InputEvent& event) override;
    void end_frame() override;

    [[nodiscard]] bool frame_active() const noexcept { return frame_active_; }
    [[nodiscard]] bool exhausted() const noexcept { return frame_index_ >= frames_.size(); }
    [[nodiscard]] std::size_t frame_index() const noexcept { return frame_index_; }
    [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }
    [[nodiscard]] std::size_t total_event_count() const noexcept { return total_event_count_; }
    [[nodiscard]] const ScriptedInputSourceLimits& limits() const noexcept { return limits_; }

private:
    Script frames_{};
    ScriptedInputSourceLimits limits_{};
    std::size_t total_event_count_{0U};
    std::size_t frame_index_{0U};
    std::size_t event_index_{0U};
    bool frame_active_{false};
};

} // namespace hlclient::input
