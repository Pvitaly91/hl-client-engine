#include <hlclient/goldsrc/move_checksum.hpp>
#include <hlclient/goldsrc/usercmd_packet_planner.hpp>
#include <hlclient/goldsrc/usercmd_schema_binding.hpp>

#include "usercmd_transaction_test_access.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_detail = hlclient::goldsrc::detail;

static_assert(!std::is_copy_constructible_v<goldsrc::GoldSrcUserCmdPacketPlan>);
static_assert(std::is_nothrow_move_constructible_v<
              goldsrc::GoldSrcUserCmdPacketPlan>);
static_assert(!std::is_copy_constructible_v<
              goldsrc::GoldSrcUserCmdPacketPlanner>);
static_assert(!std::is_move_constructible_v<
              goldsrc::GoldSrcUserCmdPacketPlanner>);

[[nodiscard]] goldsrc::GoldSrcUserCmdSequence sequence(
    const std::uint32_t value)
{
    const auto created = goldsrc::GoldSrcUserCmdSequence::create(value);
    if (!created) {
        throw std::runtime_error{"invalid test usercmd sequence"};
    }
    return *created;
}

[[nodiscard]] goldsrc::GoldSrcUserCmdState command(
    const std::uint32_t value)
{
    auto info = goldsrc::goldsrc_usercmd_default_create_info(
        sequence(value), static_cast<std::int64_t>(value) * 10'000'000);
    info.msec = 10U;
    info.sample_duration_nanoseconds = 10'000'000U;
    const auto created = goldsrc::GoldSrcUserCmdState::create(info);
    if (!created || !created.state) {
        throw std::runtime_error{"unable to build test usercmd"};
    }
    return std::move(*created.state);
}

[[nodiscard]] goldsrc::GoldSrcUserCmdSchemaBinding synthetic_binding()
{
    auto registry = goldsrc::make_synthetic_usercmd_schema_registry();
    if (!registry || !registry.registry) {
        throw std::runtime_error{"unable to build synthetic usercmd registry"};
    }
    auto bound = goldsrc::bind_goldsrc_usercmd_schema(*registry.registry);
    if (!bound || !bound.binding) {
        throw std::runtime_error{"unable to bind synthetic usercmd schema"};
    }
    return std::move(*bound.binding);
}

void insert_range(
    goldsrc::GoldSrcUserCmdHistoryBuilder& history,
    const std::uint32_t first,
    const std::uint32_t last)
{
    for (auto value = first; value <= last; ++value) {
        REQUIRE(history.insert(command(value)));
    }
}

void require_planner_error(
    const goldsrc::GoldSrcUserCmdPacketPlanResult& result,
    const goldsrc::GoldSrcUserCmdPacketPlannerErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

void require_planner_history_preflight_error(
    const goldsrc::GoldSrcUserCmdPacketPlanResult& result,
    const goldsrc::GoldSrcUserCmdHistoryErrorCode expected)
{
    require_planner_error(
        result,
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::history_commit_failed);
    REQUIRE(result.error->history_code);
    CHECK(*result.error->history_code == expected);
}

void check_same_history(
    const goldsrc::GoldSrcUserCmdHistoryState& before,
    const goldsrc::GoldSrcUserCmdHistoryState& after)
{
    CHECK(after.revision() == before.revision());
    REQUIRE(after.entries().size() == before.entries().size());
    for (std::size_t index = 0U; index < before.entries().size(); ++index) {
        const auto& expected = before.entries()[index];
        const auto& actual = after.entries()[index];
        CHECK(actual.command == expected.command);
        CHECK(actual.new_transmission_count == expected.new_transmission_count);
        CHECK(actual.backup_transmission_count ==
              expected.backup_transmission_count);
        CHECK(actual.last_packet_sequence == expected.last_packet_sequence);
    }
}

void require_planner_error(
    const goldsrc::GoldSrcUserCmdPacketPlannerOperationResult& result,
    const goldsrc::GoldSrcUserCmdPacketPlannerErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

[[nodiscard]] std::uint8_t expected_checksum(
    const goldsrc::GoldSrcClientMoveMessage& message,
    const std::uint32_t outgoing_sequence)
{
    const auto bytes = std::span<const std::byte>{message.bytes()};
    if (bytes.size() < 2U) {
        throw std::runtime_error{"invalid test client-move message"};
    }
    const auto body = bytes.subspan(2U);
    const auto computed = goldsrc::GoldSrcMoveChecksum{}.compute(
        goldsrc::GoldSrcMoveChecksumContext{
            outgoing_sequence, body.size() * 8U},
        body);
    if (!computed || !computed.checksum) {
        throw std::runtime_error{"unable to recompute test checksum"};
    }
    return *computed.checksum;
}

} // namespace

TEST_CASE("GoldSrc packet planner orders sent backups before bounded new commands",
          "[goldsrc][usercmd][planner][ordering][commit]")
{
    const auto binding = synthetic_binding();
    goldsrc::GoldSrcUserCmdHistoryBuilder history{{8U, 2U}};
    insert_range(history, 1U, 5U);

    goldsrc::GoldSrcUserCmdPacketPlannerConfig bootstrap_config;
    bootstrap_config.desired_backup_commands = 0U;
    bootstrap_config.maximum_backup_commands = 0U;
    bootstrap_config.maximum_new_commands = 3U;
    bootstrap_config.maximum_commands_per_packet = 3U;
    goldsrc::GoldSrcUserCmdPacketPlanner bootstrap{bootstrap_config};
    auto first = bootstrap.prepare(history.publish(), binding, 90U);
    REQUIRE(first);
    REQUIRE(first.plan);
    CHECK(first.plan->ordered_sequences() ==
          std::vector<goldsrc::GoldSrcUserCmdSequence>{
              sequence(1U), sequence(2U), sequence(3U)});
    REQUIRE(bootstrap.commit(history, std::move(*first.plan)));

    goldsrc::GoldSrcUserCmdPacketPlannerConfig config;
    config.desired_backup_commands = 2U;
    config.maximum_backup_commands = 2U;
    config.maximum_new_commands = 2U;
    config.maximum_commands_per_packet = 4U;
    goldsrc::GoldSrcUserCmdPacketPlanner planner{config};
    const auto history_revision = history.revision();
    auto prepared = planner.prepare(history.publish(), binding, 91U);
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    CHECK(prepared.plan->ordered_sequences() ==
          std::vector<goldsrc::GoldSrcUserCmdSequence>{
              sequence(2U), sequence(3U), sequence(4U), sequence(5U)});
    CHECK(prepared.plan->ordered_commands().size() == 4U);
    CHECK(prepared.plan->backup_command_count() == 2U);
    CHECK(prepared.plan->new_command_count() == 2U);
    CHECK(prepared.plan->history_revision() == history_revision);
    CHECK(prepared.plan->planner_revision() == 0U);
    CHECK(prepared.plan->plan_identity() != 0U);
    CHECK(prepared.plan->outgoing_netchan_sequence() == 91U);
    CHECK(prepared.plan->expected_encoded_bits() ==
          prepared.plan->encoded_message().bit_length());
    CHECK(prepared.plan->expected_encoded_bytes() ==
          prepared.plan->encoded_message().bytes().size());
    CHECK(prepared.plan->encoded_message().backup_command_count() == 2U);
    CHECK(prepared.plan->encoded_message().new_command_count() == 2U);
    CHECK(prepared.plan->encoded_message().checksum() ==
          expected_checksum(prepared.plan->encoded_message(), 91U));

    REQUIRE(planner.commit(history, std::move(*prepared.plan)));
    CHECK(history.find(sequence(2U))->backup_transmission_count == 1U);
    CHECK(history.find(sequence(3U))->backup_transmission_count == 1U);
    CHECK(history.find(sequence(4U))->new_transmission_count == 1U);
    CHECK(history.find(sequence(5U))->new_transmission_count == 1U);
    CHECK(history.find(sequence(2U))->last_packet_sequence == 91U);
    CHECK(history.unsent_count() == 0U);

    require_planner_error(
        planner.prepare(history.publish(), binding, 92U),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::no_new_commands);
}

TEST_CASE("GoldSrc packet planner checksum is bound to the supplied netchan sequence",
          "[goldsrc][usercmd][planner][checksum][sequence]")
{
    const auto binding = synthetic_binding();
    goldsrc::GoldSrcUserCmdHistoryBuilder history;
    REQUIRE(history.insert(command(1U)));
    goldsrc::GoldSrcUserCmdPacketPlanner planner;

    auto first = planner.prepare(history.publish(), binding, 100U);
    auto second = planner.prepare(history.publish(), binding, 101U);
    REQUIRE(first);
    REQUIRE(first.plan);
    REQUIRE(second);
    REQUIRE(second.plan);
    CHECK(first.plan->plan_identity() != second.plan->plan_identity());
    CHECK(first.plan->outgoing_netchan_sequence() == 100U);
    CHECK(second.plan->outgoing_netchan_sequence() == 101U);

    const auto& first_message = first.plan->encoded_message();
    const auto& second_message = second.plan->encoded_message();
    REQUIRE(first_message.bytes().size() >= 2U);
    REQUIRE(second_message.bytes().size() == first_message.bytes().size());
    CHECK(std::ranges::equal(
        first_message.bytes() | std::views::drop(2),
        second_message.bytes() | std::views::drop(2)));
    CHECK(first_message.checksum() == expected_checksum(first_message, 100U));
    CHECK(second_message.checksum() == expected_checksum(second_message, 101U));
    CHECK(first_message.bytes()[1U] ==
          static_cast<std::byte>(first_message.checksum()));
    CHECK(second_message.bytes()[1U] ==
          static_cast<std::byte>(second_message.checksum()));

    REQUIRE(planner.abandon(std::move(*first.plan)));
    REQUIRE(planner.abandon(std::move(*second.plan)));
}

TEST_CASE("Foreign planners cannot consume an owner's packet capability",
          "[goldsrc][usercmd][planner][transactional][foreign]")
{
    const auto binding = synthetic_binding();
    goldsrc::GoldSrcUserCmdHistoryBuilder history;
    REQUIRE(history.insert(command(1U)));
    goldsrc::GoldSrcUserCmdPacketPlanner owner;
    goldsrc::GoldSrcUserCmdPacketPlanner foreign;
    auto prepared = owner.prepare(history.publish(), binding, 22U);
    REQUIRE(prepared);
    REQUIRE(prepared.plan);

    require_planner_error(
        foreign.abandon(std::move(*prepared.plan)),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::foreign_plan);
    REQUIRE(owner.commit(history, std::move(*prepared.plan)));
    REQUIRE(history.find(sequence(1U)) != nullptr);
    CHECK(history.find(sequence(1U))->new_transmission_count == 1U);
}

TEST_CASE("GoldSrc packet plans are consumable and stale transactionally",
          "[goldsrc][usercmd][planner][transactional][stale]")
{
    const auto binding = synthetic_binding();
    goldsrc::GoldSrcUserCmdHistoryBuilder history;
    REQUIRE(history.insert(command(1U)));
    goldsrc::GoldSrcUserCmdPacketPlanner planner;

    auto abandoned = planner.prepare(history.publish(), binding, 10U);
    auto sibling = planner.prepare(history.publish(), binding, 11U);
    REQUIRE(abandoned);
    REQUIRE(abandoned.plan);
    REQUIRE(sibling);
    REQUIRE(sibling.plan);
    const auto revision_before = history.revision();
    REQUIRE(planner.abandon(std::move(*abandoned.plan)));
    CHECK(history.revision() == revision_before);
    REQUIRE(planner.commit(history, std::move(*sibling.plan)));
    require_planner_error(
        planner.abandon(std::move(*abandoned.plan)),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::consumed_plan);

    REQUIRE(history.insert(command(2U)));
    auto stale = planner.prepare(history.publish(), binding, 12U);
    REQUIRE(stale);
    REQUIRE(stale.plan);
    REQUIRE(history.insert(command(3U)));
    const auto revision_after_mutation = history.revision();
    require_planner_error(
        planner.commit(history, std::move(*stale.plan)),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::stale_plan);
    CHECK(history.revision() == revision_after_mutation);
    CHECK(history.find(sequence(2U))->new_transmission_count == 0U);
    CHECK(history.find(sequence(3U))->new_transmission_count == 0U);
    require_planner_error(
        planner.abandon(std::move(*stale.plan)),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::consumed_plan);

    auto accepted = planner.prepare(history.publish(), binding, 13U);
    auto stale_sibling = planner.prepare(history.publish(), binding, 14U);
    REQUIRE(accepted);
    REQUIRE(accepted.plan);
    REQUIRE(stale_sibling);
    REQUIRE(stale_sibling.plan);
    REQUIRE(planner.commit(history, std::move(*accepted.plan)));
    require_planner_error(
        planner.abandon(std::move(*stale_sibling.plan)),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::stale_plan);
}

TEST_CASE("GoldSrc packet planner enforces profiles command and payload budgets",
          "[goldsrc][usercmd][planner][limits][stock-pending]")
{
    const auto binding = synthetic_binding();
    goldsrc::GoldSrcUserCmdHistoryBuilder history;
    REQUIRE(history.insert(command(1U)));

    auto invalid_config = goldsrc::GoldSrcUserCmdPacketPlannerConfig{};
    invalid_config.desired_backup_commands = 8U;
    invalid_config.maximum_backup_commands = 7U;
    CHECK_FALSE(goldsrc::valid_goldsrc_usercmd_packet_planner_config(
        invalid_config));
    goldsrc::GoldSrcUserCmdPacketPlanner invalid{invalid_config};
    require_planner_error(
        invalid.prepare(history.publish(), binding, 1U),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::invalid_configuration);

    auto stock_config = goldsrc::GoldSrcUserCmdPacketPlannerConfig{};
    stock_config.profile =
        goldsrc::GoldSrcUserCmdPacketPlannerProfile::
            stock_protocol_48_evidence_pending;
    goldsrc::GoldSrcUserCmdPacketPlanner stock{stock_config};
    CHECK_FALSE(stock.valid_configuration());
    require_planner_error(
        stock.prepare(history.publish(), binding, 1U),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::stock_evidence_pending);

    goldsrc::GoldSrcUserCmdPacketPlannerConfig tight_config;
    tight_config.desired_backup_commands = 0U;
    tight_config.maximum_backup_commands = 0U;
    tight_config.maximum_new_commands = 1U;
    tight_config.maximum_commands_per_packet = 1U;
    tight_config.maximum_packet_bytes = 4U;
    tight_config.maximum_packet_bits = 32U;
    REQUIRE(goldsrc::valid_goldsrc_usercmd_packet_planner_config(tight_config));
    goldsrc::GoldSrcUserCmdPacketPlanner tight{tight_config};
    const auto revision_before = history.revision();
    require_planner_error(
        tight.prepare(history.publish(), binding, 2U),
        goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::packet_budget_exceeded);
    CHECK(history.revision() == revision_before);
    CHECK(history.unsent_count() == 1U);
}

TEST_CASE("GoldSrc packet planner preflights every commit counter exhaustion",
          "[goldsrc][usercmd][planner][transactional][overflow][preflight]")
{
    const auto binding = synthetic_binding();

    SECTION("history revision")
    {
        goldsrc::GoldSrcUserCmdHistoryBuilder history;
        REQUIRE(history.insert(command(1U)));
        goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
            set_history_revision(
                history, std::numeric_limits<std::uint64_t>::max());
        const auto before = history.publish();
        goldsrc::GoldSrcUserCmdPacketPlanner planner;

        const auto prepared = planner.prepare(before, binding, 30U);
        require_planner_history_preflight_error(
            prepared,
            goldsrc::GoldSrcUserCmdHistoryErrorCode::revision_overflow);
        check_same_history(before, history.publish());
    }

    SECTION("selected backup transmission count")
    {
        goldsrc::GoldSrcUserCmdHistoryBuilder history;
        insert_range(history, 1U, 2U);
        REQUIRE(goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
                    set_transmission_counts(
                        history,
                        sequence(1U),
                        1U,
                        std::numeric_limits<std::uint32_t>::max()));
        const auto before = history.publish();
        goldsrc::GoldSrcUserCmdPacketPlanner planner;

        const auto prepared = planner.prepare(before, binding, 31U);
        require_planner_history_preflight_error(
            prepared,
            goldsrc::GoldSrcUserCmdHistoryErrorCode::
                transmission_count_overflow);
        check_same_history(before, history.publish());
    }

    SECTION("planner revision")
    {
        goldsrc::GoldSrcUserCmdHistoryBuilder history;
        REQUIRE(history.insert(command(1U)));
        const auto before = history.publish();
        goldsrc::GoldSrcUserCmdPacketPlanner planner;
        goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
            set_planner_revision(
                planner, std::numeric_limits<std::uint64_t>::max());

        require_planner_error(
            planner.prepare(before, binding, 32U),
            goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::revision_overflow);
        check_same_history(before, history.publish());
    }
}
