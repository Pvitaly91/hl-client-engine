#pragma once

#include <hlclient/app/session_receipt.hpp>
#include <utility>

namespace hlclient::app {

// Progress leaves are immutable write-new publications. Publish generation
// start before a follow-up milestone can advance the journal to another leaf.
template<class Publish>
[[nodiscard]] bool begin_published_session_receipt_generation(
    SessionReceiptJournal& journal,SessionReceiptGeneration generation,Publish&& publish) {
    return journal.begin_generation(std::move(generation)) && publish(*journal.snapshot());
}

// Application-side facts from one already committed, immutable observation.
// Planned setup, decoder prefixes, GL/window creation and rollback records
// cannot satisfy this input. Protocol handoff facts retain their own owners.
struct SessionReceiptCommitInput final {
    bool owning_commit{};
    bool receiving_origin_complete{};
    std::uint64_t publication_ordinal{};
    bool world_assets_ready_and_presented{};
    bool control_activated{};
    SessionReceiptCounters counters;
};
struct SessionReceiptCommitPlan final {
    std::uint64_t fact_mask{};
    bool map_entry_committed{};
    bool world_ready{};
    bool first_gameplay_update{};
};
[[nodiscard]] inline SessionReceiptCommitPlan project_session_receipt_commit(
    const SessionReceiptCommitInput& input) noexcept
{
    SessionReceiptCommitPlan plan;
    if (!input.owning_commit || input.publication_ordinal == 0U) return plan;
    if (input.counters.transmitted_usercmd_packet_count.value_or(0U) > 0U)
        plan.fact_mask |= receipt_fact(SessionReceiptFact::usercmd_transmitted);
    if (!input.receiving_origin_complete) return plan;
    plan.fact_mask |= receipt_fact(SessionReceiptFact::world_published);
    plan.map_entry_committed = true;
    plan.world_ready = input.world_assets_ready_and_presented;
    plan.first_gameplay_update = plan.world_ready && input.control_activated &&
        !input.counters.usercmd_zero_observed &&
        input.counters.new_usercmd_count.value_or(0U) > 0U &&
        input.counters.transmitted_usercmd_packet_count.value_or(0U) > 0U &&
        input.counters.server_sample_count.value_or(0U) > 0U;
    if (plan.first_gameplay_update)
        plan.fact_mask |= receipt_fact(SessionReceiptFact::gameplay_committed);
    return plan;
}

} // namespace hlclient::app
