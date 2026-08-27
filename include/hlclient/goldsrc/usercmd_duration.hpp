#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultMaximumUserCmdDurationSegments = 8U;
inline constexpr std::size_t kMaximumUserCmdDurationSegments = 256U;

enum class GoldSrcUserCmdDurationErrorCode : std::uint8_t {
    invalid_configuration,
    non_finite_duration,
    negative_duration,
    duration_overflow,
    segment_limit_exceeded,
    allocation_failed,
};

struct GoldSrcUserCmdDurationError {
    GoldSrcUserCmdDurationErrorCode code{
        GoldSrcUserCmdDurationErrorCode::invalid_configuration};
    std::string_view context;
};

struct GoldSrcUserCmdDurationQuantization {
    std::vector<std::uint8_t> command_msec;
    std::int64_t remainder_nanoseconds{0};
    std::uint64_t requested_nanoseconds{0U};
    std::uint64_t represented_milliseconds{0U};
};

struct GoldSrcUserCmdDurationResult {
    std::optional<GoldSrcUserCmdDurationQuantization> quantization;
    std::optional<GoldSrcUserCmdDurationError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return quantization.has_value();
    }
};

class GoldSrcUserCmdDurationQuantizer final {
public:
    [[nodiscard]] static GoldSrcUserCmdDurationResult quantize(
        double duration_seconds,
        std::int64_t remainder_nanoseconds = 0,
        std::uint8_t maximum_msec = 255U,
        std::size_t maximum_segments =
            kDefaultMaximumUserCmdDurationSegments) noexcept;
};

} // namespace hlclient::goldsrc
