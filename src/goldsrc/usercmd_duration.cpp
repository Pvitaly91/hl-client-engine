#include <hlclient/goldsrc/usercmd_duration.hpp>

#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] GoldSrcUserCmdDurationResult failure(
    const GoldSrcUserCmdDurationErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, GoldSrcUserCmdDurationError{code, context}};
}

} // namespace

GoldSrcUserCmdDurationResult GoldSrcUserCmdDurationQuantizer::quantize(
    const double duration_seconds,
    const std::int64_t remainder_nanoseconds,
    const std::uint8_t maximum_msec,
    const std::size_t maximum_segments) noexcept
{
    if (maximum_msec == 0U || maximum_segments == 0U ||
        maximum_segments > kMaximumUserCmdDurationSegments ||
        remainder_nanoseconds < 0 || remainder_nanoseconds >= 1'000'000) {
        return failure(
            GoldSrcUserCmdDurationErrorCode::invalid_configuration,
            "Duration quantizer configuration or remainder is invalid");
    }
    if (!std::isfinite(duration_seconds)) {
        return failure(
            GoldSrcUserCmdDurationErrorCode::non_finite_duration,
            "Command duration must be finite");
    }
    if (duration_seconds < 0.0) {
        return failure(
            GoldSrcUserCmdDurationErrorCode::negative_duration,
            "Command duration must not be negative");
    }

    constexpr long double kNanosecondsPerSecond = 1'000'000'000.0L;
    const auto exact_nanoseconds =
        static_cast<long double>(duration_seconds) * kNanosecondsPerSecond;
    if (exact_nanoseconds >
        static_cast<long double>(std::numeric_limits<std::int64_t>::max()) -
            static_cast<long double>(remainder_nanoseconds)) {
        return failure(
            GoldSrcUserCmdDurationErrorCode::duration_overflow,
            "Command duration cannot be represented in nanoseconds");
    }

    const auto rounded_nanoseconds = std::llround(exact_nanoseconds);
    if (rounded_nanoseconds < 0 ||
        rounded_nanoseconds >
            std::numeric_limits<std::int64_t>::max() - remainder_nanoseconds) {
        return failure(
            GoldSrcUserCmdDurationErrorCode::duration_overflow,
            "Command duration plus remainder overflowed");
    }
    const auto accumulated_nanoseconds =
        rounded_nanoseconds + remainder_nanoseconds;
    const auto whole_milliseconds = accumulated_nanoseconds / 1'000'000;
    const auto next_remainder = accumulated_nanoseconds % 1'000'000;
    const auto required_segments_wide = whole_milliseconds == 0
        ? std::uint64_t{0U}
        : (static_cast<std::uint64_t>(whole_milliseconds) + maximum_msec - 1U) /
              maximum_msec;
    if (required_segments_wide > maximum_segments) {
        return failure(
            GoldSrcUserCmdDurationErrorCode::segment_limit_exceeded,
            "Command duration requires more bounded wire segments than allowed");
    }
    const auto required_segments =
        static_cast<std::size_t>(required_segments_wide);

    GoldSrcUserCmdDurationQuantization result;
    result.remainder_nanoseconds = next_remainder;
    result.requested_nanoseconds =
        static_cast<std::uint64_t>(rounded_nanoseconds);
    result.represented_milliseconds =
        static_cast<std::uint64_t>(whole_milliseconds);
    try {
        result.command_msec.reserve(required_segments);
        auto remaining = whole_milliseconds;
        while (remaining > 0) {
            const auto segment = remaining > maximum_msec
                ? maximum_msec
                : static_cast<std::uint8_t>(remaining);
            result.command_msec.push_back(segment);
            remaining -= segment;
        }
    } catch (const std::bad_alloc&) {
        return failure(
            GoldSrcUserCmdDurationErrorCode::allocation_failed,
            "Duration quantizer could not allocate its bounded segments");
    } catch (const std::length_error&) {
        return failure(
            GoldSrcUserCmdDurationErrorCode::allocation_failed,
            "Duration quantizer rejected its bounded segment allocation");
    }
    return {std::move(result), std::nullopt};
}

} // namespace hlclient::goldsrc
