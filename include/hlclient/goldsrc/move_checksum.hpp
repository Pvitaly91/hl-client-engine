#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultSyntheticMoveChecksumCoverageBytes = 1'024U;
inline constexpr std::size_t kMaximumSyntheticMoveChecksumCoverageBytes = 8'192U;

enum class GoldSrcMoveChecksumProfile : std::uint8_t {
    synthetic_crc8_v1,
    stock_protocol_48_build_10210_evidence_pending,
};

struct GoldSrcMoveChecksumContext {
    std::uint32_t outgoing_netchan_sequence{0U};
    std::size_t body_bit_length{0U};
};

enum class GoldSrcMoveChecksumErrorCode : std::uint8_t {
    invalid_configuration,
    stock_evidence_pending,
    invalid_geometry,
    coverage_limit_exceeded,
};

struct GoldSrcMoveChecksumError {
    GoldSrcMoveChecksumErrorCode code{
        GoldSrcMoveChecksumErrorCode::invalid_configuration};
    std::string_view context;
};

struct GoldSrcMoveChecksumResult {
    std::optional<std::uint8_t> checksum;
    std::optional<GoldSrcMoveChecksumError> error;
    std::size_t covered_bytes{0U};
    bool padding_bits_participate{true};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return checksum.has_value();
    }
};

class GoldSrcMoveChecksum final {
public:
    explicit GoldSrcMoveChecksum(
        GoldSrcMoveChecksumProfile profile =
            GoldSrcMoveChecksumProfile::synthetic_crc8_v1,
        std::size_t maximum_coverage_bytes =
            kDefaultSyntheticMoveChecksumCoverageBytes) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] GoldSrcMoveChecksumProfile profile() const noexcept;
    [[nodiscard]] GoldSrcMoveChecksumResult compute(
        const GoldSrcMoveChecksumContext& context,
        std::span<const std::byte> body) const noexcept;

private:
    GoldSrcMoveChecksumProfile profile_;
    std::size_t maximum_coverage_bytes_{0U};
    bool valid_configuration_{false};
};

} // namespace hlclient::goldsrc
