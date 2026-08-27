#include <hlclient/goldsrc/move_checksum.hpp>

#include <limits>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] constexpr bool valid_profile(
    const GoldSrcMoveChecksumProfile profile) noexcept
{
    return profile == GoldSrcMoveChecksumProfile::synthetic_crc8_v1 ||
        profile == GoldSrcMoveChecksumProfile::
                       stock_protocol_48_build_10210_evidence_pending;
}

[[nodiscard]] std::uint8_t crc8_update(
    std::uint8_t crc,
    const std::uint8_t value) noexcept
{
    crc ^= value;
    for (std::size_t bit = 0U; bit < 8U; ++bit) {
        crc = (crc & 0x80U) != 0U
            ? static_cast<std::uint8_t>((crc << 1U) ^ 0x07U)
            : static_cast<std::uint8_t>(crc << 1U);
    }
    return crc;
}

[[nodiscard]] GoldSrcMoveChecksumResult failure(
    const GoldSrcMoveChecksumErrorCode code,
    const std::string_view context) noexcept
{
    GoldSrcMoveChecksumResult result;
    result.error = GoldSrcMoveChecksumError{code, context};
    return result;
}

} // namespace

GoldSrcMoveChecksum::GoldSrcMoveChecksum(
    const GoldSrcMoveChecksumProfile profile,
    const std::size_t maximum_coverage_bytes) noexcept
    : profile_{profile},
      maximum_coverage_bytes_{maximum_coverage_bytes},
      valid_configuration_{
          valid_profile(profile_) && maximum_coverage_bytes_ > 0U &&
          maximum_coverage_bytes_ <=
              kMaximumSyntheticMoveChecksumCoverageBytes}
{
}

bool GoldSrcMoveChecksum::valid_configuration() const noexcept
{
    return valid_configuration_;
}

GoldSrcMoveChecksumProfile GoldSrcMoveChecksum::profile() const noexcept
{
    return profile_;
}

GoldSrcMoveChecksumResult GoldSrcMoveChecksum::compute(
    const GoldSrcMoveChecksumContext& context,
    const std::span<const std::byte> body) const noexcept
{
    if (!valid_configuration_) {
        return failure(
            GoldSrcMoveChecksumErrorCode::invalid_configuration,
            "Move checksum profile or coverage bound is invalid");
    }
    if (profile_ != GoldSrcMoveChecksumProfile::synthetic_crc8_v1) {
        return failure(
            GoldSrcMoveChecksumErrorCode::stock_evidence_pending,
            "Stock move checksum constants and coverage remain evidence-pending");
    }
    if (body.size() > maximum_coverage_bytes_) {
        return failure(
            GoldSrcMoveChecksumErrorCode::coverage_limit_exceeded,
            "Move checksum body exceeds its configured coverage bound");
    }
    if (context.body_bit_length > body.size() * 8U ||
        (!body.empty() &&
         context.body_bit_length <= (body.size() - 1U) * 8U)) {
        return failure(
            GoldSrcMoveChecksumErrorCode::invalid_geometry,
            "Move checksum body bit length does not match its owning bytes");
    }

    std::uint8_t crc = 0xA7U;
    for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        crc = crc8_update(
            crc,
            static_cast<std::uint8_t>(
                context.outgoing_netchan_sequence >> (byte_index * 8U)));
    }
    for (const auto value : body) {
        crc = crc8_update(crc, std::to_integer<std::uint8_t>(value));
    }
    // Domain-separate equal byte strings whose final byte has a different
    // meaningful-bit count. Zero padding itself remains covered above.
    crc = crc8_update(
        crc,
        static_cast<std::uint8_t>(context.body_bit_length & 7U));
    return {crc, std::nullopt, body.size(), true};
}

} // namespace hlclient::goldsrc
