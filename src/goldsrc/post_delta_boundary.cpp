#include <hlclient/goldsrc/delta_description.hpp>

namespace hlclient::goldsrc {

PostDeltaBoundary::PostDeltaBoundary(
    const std::uint8_t opcode,
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::size_t remaining_byte_count,
    const PostDeltaBoundaryCategory category,
    const PostDeltaBoundaryEvidenceStatus evidence_status) noexcept
    : opcode_{opcode},
      byte_offset_{byte_offset},
      bit_offset_{bit_offset},
      remaining_byte_count_{remaining_byte_count},
      category_{category},
      evidence_status_{evidence_status}
{
}

std::uint8_t PostDeltaBoundary::opcode() const noexcept
{
    return opcode_;
}

std::size_t PostDeltaBoundary::byte_offset() const noexcept
{
    return byte_offset_;
}

std::size_t PostDeltaBoundary::bit_offset() const noexcept
{
    return bit_offset_;
}

std::size_t PostDeltaBoundary::remaining_byte_count() const noexcept
{
    return remaining_byte_count_;
}

PostDeltaBoundaryCategory PostDeltaBoundary::category() const noexcept
{
    return category_;
}

PostDeltaBoundaryEvidenceStatus PostDeltaBoundary::evidence_status() const noexcept
{
    return evidence_status_;
}

} // namespace hlclient::goldsrc
