#pragma once

#include <hlclient/goldsrc/usercmd_state.hpp>
#include <hlclient/goldsrc/reference_client_move.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

class GoldSrcUserCmdPacketPlanner;
struct GoldSrcUserCmdHistoryOperationResult;
namespace detail {
class GoldSrcUserCmdTransactionalTestAccess;
}

struct GoldSrcUserCmdHistoryEntry {
    std::shared_ptr<const GoldSrcUserCmdState> command;
    std::uint32_t new_transmission_count{0U};
    std::uint32_t backup_transmission_count{0U};
    std::optional<std::uint32_t> last_packet_sequence;
    std::shared_ptr<const GoldSrcWireUserCmd> reference_command;
    GoldSrcUserCmdSequence reference_identity{};
    [[nodiscard]] bool has_command() const noexcept { return command || reference_command; }
    [[nodiscard]] GoldSrcUserCmdSequence sequence() const noexcept {
        return command ? command->command_sequence() : reference_identity;
    }
};

enum class GoldSrcUserCmdHistoryProfile { synthetic_v1, reference_wire_v1 };

class GoldSrcUserCmdHistoryState final {
public:
    GoldSrcUserCmdHistoryState(const GoldSrcUserCmdHistoryState&) = default;
    GoldSrcUserCmdHistoryState(GoldSrcUserCmdHistoryState&&) noexcept = default;
    GoldSrcUserCmdHistoryState& operator=(const GoldSrcUserCmdHistoryState&) = delete;
    GoldSrcUserCmdHistoryState& operator=(GoldSrcUserCmdHistoryState&&) = delete;
    ~GoldSrcUserCmdHistoryState() = default;

    [[nodiscard]] const std::vector<GoldSrcUserCmdHistoryEntry>& entries()
        const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] GoldSrcUserCmdHistoryProfile profile() const noexcept { return profile_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] const GoldSrcUserCmdHistoryEntry* find(
        GoldSrcUserCmdSequence sequence) const noexcept;
    [[nodiscard]] std::vector<GoldSrcUserCmdSequence> unsent_sequences() const;

private:
    friend class GoldSrcUserCmdHistoryBuilder;
    friend class GoldSrcUserCmdPacketPlanner;
    friend class detail::GoldSrcUserCmdTransactionalTestAccess;

    GoldSrcUserCmdHistoryState(
        std::vector<GoldSrcUserCmdHistoryEntry> entries,
        std::uint64_t revision, GoldSrcUserCmdHistoryProfile profile,
        std::uint64_t generation, std::shared_ptr<const std::uint8_t> owner) noexcept;
    [[nodiscard]] GoldSrcUserCmdHistoryOperationResult preflight_submission(
        std::uint64_t expected_revision,
        std::span<const GoldSrcUserCmdSequence> ordered_sequences,
        std::size_t backup_count) const noexcept;

    std::vector<GoldSrcUserCmdHistoryEntry> entries_;
    std::uint64_t revision_{0U};
    GoldSrcUserCmdHistoryProfile profile_;
    std::uint64_t generation_;
    std::shared_ptr<const std::uint8_t> owner_;
};

struct GoldSrcUserCmdHistoryConfig {
    std::size_t maximum_entries{kDefaultGoldSrcUserCmdHistoryEntries};
    std::size_t protected_backup_window{7U};
    GoldSrcUserCmdHistoryProfile profile{GoldSrcUserCmdHistoryProfile::synthetic_v1};
    std::uint64_t generation{1};
};

enum class GoldSrcUserCmdHistoryErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_command,
    duplicate_sequence,
    out_of_order_sequence,
    history_full,
    revision_overflow,
    transmission_count_overflow,
    unknown_sequence,
    stale_submission,
    pending_submission,
};

struct GoldSrcUserCmdHistoryError {
    GoldSrcUserCmdHistoryErrorCode code{
        GoldSrcUserCmdHistoryErrorCode::invalid_configuration};
    std::optional<GoldSrcUserCmdSequence> sequence;
    std::string_view context;
};

struct GoldSrcUserCmdHistoryOperationResult {
    std::optional<GoldSrcUserCmdHistoryError> error;
    std::size_t evicted_count{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class GoldSrcUserCmdHistoryBuilder final {
public:
    explicit GoldSrcUserCmdHistoryBuilder(
        GoldSrcUserCmdHistoryConfig config = {});

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const GoldSrcUserCmdHistoryConfig& config() const noexcept;
    [[nodiscard]] GoldSrcUserCmdHistoryOperationResult insert(
        const GoldSrcUserCmdState& command);
    [[nodiscard]] GoldSrcUserCmdHistoryOperationResult insert(
        GoldSrcUserCmdSequence identity, const GoldSrcWireUserCmd& command,
        std::uint64_t generation);
    [[nodiscard]] GoldSrcUserCmdHistoryState publish() const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const GoldSrcUserCmdHistoryEntry* find(
        GoldSrcUserCmdSequence sequence) const noexcept;
    [[nodiscard]] std::size_t unsent_count() const noexcept;

private:
    friend class GoldSrcUserCmdPacketPlanner;
    friend class detail::GoldSrcUserCmdTransactionalTestAccess;
    [[nodiscard]] GoldSrcUserCmdHistoryOperationResult insert_owned(GoldSrcUserCmdHistoryEntry entry);
    [[nodiscard]] GoldSrcUserCmdHistoryOperationResult preflight_submission(
        std::uint64_t expected_revision, std::span<const GoldSrcUserCmdSequence> sequences,
        std::size_t backups) const noexcept;

    [[nodiscard]] GoldSrcUserCmdHistoryOperationResult commit_submission(
        std::uint64_t expected_revision,
        std::span<const GoldSrcUserCmdSequence> ordered_sequences,
        std::size_t backup_count,
        std::uint32_t packet_sequence) noexcept;

    GoldSrcUserCmdHistoryConfig config_;
    bool valid_configuration_{false};
    std::vector<GoldSrcUserCmdHistoryEntry> entries_;
    std::uint64_t revision_{0U};
    std::shared_ptr<const std::uint8_t> owner_{std::make_shared<const std::uint8_t>(std::uint8_t{0})};
};

} // namespace hlclient::goldsrc
