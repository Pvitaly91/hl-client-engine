#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace hlclient::tools {
struct ReferenceMoveTransportSelfTestResult {
    bool passed{}, cleanup{};
    std::size_t queued{}, new_submitted{}, backup_submitted{}, packets_sent{},
        packets_received{}, checksum_matched{}, expected_commands{}, dropped{},
        would_block{}, retries{}, stale_contexts{};
    std::uint64_t history_revision{};
    std::string failure;
};
// No endpoint, capture, credentials or raw command arguments. Creates both
// peers.
[[nodiscard]] ReferenceMoveTransportSelfTestResult reference_move_transport_self_test();
} // namespace hlclient::tools
