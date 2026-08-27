#include <hlclient/goldsrc/usercmd_history.hpp>

#include <algorithm>
#include <array>
#include <limits>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] GoldSrcUserCmdHistoryOperationResult failure(
    const GoldSrcUserCmdHistoryErrorCode code,
    const std::string_view context,
    const std::optional<GoldSrcUserCmdSequence> sequence = std::nullopt) noexcept
{
    return {GoldSrcUserCmdHistoryError{code, sequence, context}, 0U};
}

inline constexpr std::size_t kMaximumSubmissionCommands = 32U;
using SelectedSubmissionIndexes =
    std::array<std::size_t, kMaximumSubmissionCommands>;

[[nodiscard]] GoldSrcUserCmdHistoryOperationResult validate_submission_preflight(
    const std::vector<GoldSrcUserCmdHistoryEntry>& entries,
    const std::uint64_t current_revision,
    const std::uint64_t expected_revision,
    const std::span<const GoldSrcUserCmdSequence> ordered_sequences,
    const std::size_t backup_count,
    SelectedSubmissionIndexes& selected_indexes,
    std::size_t& selected_count) noexcept
{
    selected_count = 0U;
    if (expected_revision != current_revision) {
        return failure(
            GoldSrcUserCmdHistoryErrorCode::stale_submission,
            "Prepared packet plan no longer matches history revision");
    }
    if (backup_count > ordered_sequences.size() || ordered_sequences.empty()) {
        return failure(
            GoldSrcUserCmdHistoryErrorCode::invalid_command,
            "Prepared packet submission split is invalid");
    }
    if (ordered_sequences.size() > kMaximumSubmissionCommands) {
        return failure(
            GoldSrcUserCmdHistoryErrorCode::invalid_command,
            "Prepared packet submission exceeds the hard command bound");
    }
    for (const auto sequence : ordered_sequences) {
        const auto found = std::ranges::find_if(
            entries, [sequence](const auto& entry) {
                return entry.command &&
                       entry.command->command_sequence() == sequence;
            });
        if (found == entries.end()) {
            return failure(
                GoldSrcUserCmdHistoryErrorCode::unknown_sequence,
                "Prepared packet references a command absent from history",
                sequence);
        }
        const auto submission_index = selected_count;
        const auto count = submission_index < backup_count
            ? found->backup_transmission_count
            : found->new_transmission_count;
        if (count == std::numeric_limits<std::uint32_t>::max()) {
            return failure(
                GoldSrcUserCmdHistoryErrorCode::transmission_count_overflow,
                "Usercmd transmission count is exhausted",
                sequence);
        }
        if (submission_index >= backup_count && count != 0U) {
            return failure(
                GoldSrcUserCmdHistoryErrorCode::stale_submission,
                "Prepared new command has already been submitted",
                sequence);
        }
        const auto selected_index = static_cast<std::size_t>(
            std::distance(entries.begin(), found));
        const auto selected_end =
            selected_indexes.begin() +
            static_cast<std::ptrdiff_t>(selected_count);
        if (std::ranges::find(
                selected_indexes.begin(), selected_end, selected_index) !=
            selected_end) {
            return failure(
                GoldSrcUserCmdHistoryErrorCode::invalid_command,
                "Prepared packet submission repeats a command sequence",
                sequence);
        }
        selected_indexes[selected_count++] = selected_index;
    }
    if (current_revision == std::numeric_limits<std::uint64_t>::max()) {
        return failure(
            GoldSrcUserCmdHistoryErrorCode::revision_overflow,
            "Usercmd history revision is exhausted");
    }
    return {};
}

} // namespace

GoldSrcUserCmdHistoryState::GoldSrcUserCmdHistoryState(
    std::vector<GoldSrcUserCmdHistoryEntry> entries,
    const std::uint64_t revision) noexcept
    : entries_{std::move(entries)}, revision_{revision}
{
}

const std::vector<GoldSrcUserCmdHistoryEntry>&
GoldSrcUserCmdHistoryState::entries() const noexcept
{
    return entries_;
}

std::size_t GoldSrcUserCmdHistoryState::size() const noexcept
{
    return entries_.size();
}

std::uint64_t GoldSrcUserCmdHistoryState::revision() const noexcept
{
    return revision_;
}

const GoldSrcUserCmdHistoryEntry* GoldSrcUserCmdHistoryState::find(
    const GoldSrcUserCmdSequence sequence) const noexcept
{
    const auto found = std::ranges::find_if(entries_, [sequence](const auto& entry) {
        return entry.command && entry.command->command_sequence() == sequence;
    });
    return found == entries_.end() ? nullptr : &*found;
}

std::vector<GoldSrcUserCmdSequence>
GoldSrcUserCmdHistoryState::unsent_sequences() const
{
    std::vector<GoldSrcUserCmdSequence> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (entry.command && entry.new_transmission_count == 0U) {
            result.push_back(entry.command->command_sequence());
        }
    }
    return result;
}

GoldSrcUserCmdHistoryOperationResult
GoldSrcUserCmdHistoryState::preflight_submission(
    const std::uint64_t expected_revision,
    const std::span<const GoldSrcUserCmdSequence> ordered_sequences,
    const std::size_t backup_count) const noexcept
{
    SelectedSubmissionIndexes selected_indexes{};
    std::size_t selected_count = 0U;
    return ::hlclient::goldsrc::validate_submission_preflight(
        entries_,
        revision_,
        expected_revision,
        ordered_sequences,
        backup_count,
        selected_indexes,
        selected_count);
}

GoldSrcUserCmdHistoryBuilder::GoldSrcUserCmdHistoryBuilder(
    GoldSrcUserCmdHistoryConfig config)
    : config_{config},
      valid_configuration_{
          config_.maximum_entries > 0U &&
          config_.maximum_entries <= kMaximumGoldSrcUserCmdHistoryEntries &&
          config_.protected_backup_window < config_.maximum_entries}
{
    if (valid_configuration_) {
        entries_.reserve(config_.maximum_entries);
    }
}

bool GoldSrcUserCmdHistoryBuilder::valid_configuration() const noexcept
{
    return valid_configuration_;
}

const GoldSrcUserCmdHistoryConfig&
GoldSrcUserCmdHistoryBuilder::config() const noexcept
{
    return config_;
}

GoldSrcUserCmdHistoryOperationResult GoldSrcUserCmdHistoryBuilder::insert(
    const GoldSrcUserCmdState& command)
{
    if (!valid_configuration_) {
        return failure(
            GoldSrcUserCmdHistoryErrorCode::invalid_configuration,
            "Usercmd history configuration is invalid");
    }
    const auto sequence = command.command_sequence();
    if (!sequence.valid()) {
        return failure(
            GoldSrcUserCmdHistoryErrorCode::invalid_command,
            "Usercmd history insertion requires a valid command sequence");
    }
    if (!entries_.empty()) {
        const auto last_sequence = entries_.back().command->command_sequence();
        if (sequence == last_sequence || find(sequence) != nullptr) {
            return failure(
                GoldSrcUserCmdHistoryErrorCode::duplicate_sequence,
                "Usercmd history already contains this command sequence",
                sequence);
        }
        if (sequence.value() < last_sequence.value()) {
            return failure(
                GoldSrcUserCmdHistoryErrorCode::out_of_order_sequence,
                "Usercmd history insertion is not strictly increasing",
                sequence);
        }
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return failure(
            GoldSrcUserCmdHistoryErrorCode::revision_overflow,
            "Usercmd history revision is exhausted");
    }

    // Allocate immutable ownership before any possible eviction so allocation
    // failure cannot leave a partially mutated history.
    auto owned_command = std::make_shared<const GoldSrcUserCmdState>(command);

    std::size_t evicted = 0U;
    if (entries_.size() == config_.maximum_entries) {
        const auto protected_begin = entries_.size() > config_.protected_backup_window
            ? entries_.size() - config_.protected_backup_window
            : 0U;
        auto evict = entries_.end();
        for (std::size_t index = 0U; index < protected_begin; ++index) {
            if (entries_[index].new_transmission_count != 0U) {
                evict = entries_.begin() + static_cast<std::ptrdiff_t>(index);
                break;
            }
        }
        if (evict == entries_.end()) {
            return failure(
                GoldSrcUserCmdHistoryErrorCode::history_full,
                "No sent command outside the protected backup window can be evicted");
        }
        entries_.erase(evict);
        evicted = 1U;
    }

    entries_.push_back(GoldSrcUserCmdHistoryEntry{
        std::move(owned_command),
        0U,
        0U,
        std::nullopt,
    });
    ++revision_;
    return {std::nullopt, evicted};
}

GoldSrcUserCmdHistoryState GoldSrcUserCmdHistoryBuilder::publish() const
{
    return GoldSrcUserCmdHistoryState{entries_, revision_};
}

std::size_t GoldSrcUserCmdHistoryBuilder::size() const noexcept
{
    return entries_.size();
}

std::uint64_t GoldSrcUserCmdHistoryBuilder::revision() const noexcept
{
    return revision_;
}

const GoldSrcUserCmdHistoryEntry* GoldSrcUserCmdHistoryBuilder::find(
    const GoldSrcUserCmdSequence sequence) const noexcept
{
    const auto found = std::ranges::find_if(entries_, [sequence](const auto& entry) {
        return entry.command && entry.command->command_sequence() == sequence;
    });
    return found == entries_.end() ? nullptr : &*found;
}

std::size_t GoldSrcUserCmdHistoryBuilder::unsent_count() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(
        entries_, [](const auto& entry) {
            return entry.command && entry.new_transmission_count == 0U;
        }));
}

GoldSrcUserCmdHistoryOperationResult
GoldSrcUserCmdHistoryBuilder::commit_submission(
    const std::uint64_t expected_revision,
    const std::span<const GoldSrcUserCmdSequence> ordered_sequences,
    const std::size_t backup_count,
    const std::uint32_t packet_sequence) noexcept
{
    SelectedSubmissionIndexes selected_indexes{};
    std::size_t selected_count = 0U;
    const auto preflight = ::hlclient::goldsrc::validate_submission_preflight(
        entries_,
        revision_,
        expected_revision,
        ordered_sequences,
        backup_count,
        selected_indexes,
        selected_count);
    if (!preflight) {
        return preflight;
    }
    for (std::size_t index = 0U; index < selected_count; ++index) {
        auto& entry = entries_[selected_indexes[index]];
        if (index < backup_count) {
            ++entry.backup_transmission_count;
        } else {
            ++entry.new_transmission_count;
        }
        entry.last_packet_sequence = packet_sequence;
    }
    ++revision_;
    return {};
}

} // namespace hlclient::goldsrc
