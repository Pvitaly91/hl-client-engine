#include <hlclient/goldsrc/usercmd_history.hpp>
#include <hlclient/goldsrc/usercmd_packet_planner.hpp>
#include <hlclient/goldsrc/usercmd_schema_binding.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

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
        const auto inserted = history.insert(command(value));
        REQUIRE(inserted);
    }
}

void commit_new_commands(
    goldsrc::GoldSrcUserCmdHistoryBuilder& history,
    const goldsrc::GoldSrcUserCmdSchemaBinding& binding,
    const std::size_t count,
    const std::uint32_t packet_sequence)
{
    goldsrc::GoldSrcUserCmdPacketPlannerConfig config;
    config.desired_backup_commands = 0U;
    config.maximum_backup_commands = 0U;
    config.maximum_new_commands = count;
    config.maximum_commands_per_packet = count;
    goldsrc::GoldSrcUserCmdPacketPlanner planner{config};
    REQUIRE(planner.valid_configuration());
    auto prepared = planner.prepare(
        history.publish(), binding, packet_sequence);
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    REQUIRE(planner.commit(history, std::move(*prepared.plan)));
}

void require_history_error(
    const goldsrc::GoldSrcUserCmdHistoryOperationResult& result,
    const goldsrc::GoldSrcUserCmdHistoryErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

} // namespace

TEST_CASE("GoldSrc usercmd history publishes immutable transactional snapshots",
          "[goldsrc][usercmd][history][publish][transactional]")
{
    goldsrc::GoldSrcUserCmdHistoryBuilder history{{4U, 1U}};
    REQUIRE(history.valid_configuration());
    REQUIRE(history.insert(command(1U)));
    const auto published = history.publish();
    const auto published_revision = published.revision();
    REQUIRE(history.insert(command(3U)));

    CHECK(published.size() == 1U);
    CHECK(published.revision() == published_revision);
    REQUIRE(published.find(sequence(1U)));
    CHECK_FALSE(published.find(sequence(3U)));
    CHECK(published.unsent_sequences() ==
          std::vector<goldsrc::GoldSrcUserCmdSequence>{sequence(1U)});

    const auto revision_before_failure = history.revision();
    const auto size_before_failure = history.size();
    require_history_error(
        history.insert(command(3U)),
        goldsrc::GoldSrcUserCmdHistoryErrorCode::duplicate_sequence);
    require_history_error(
        history.insert(command(2U)),
        goldsrc::GoldSrcUserCmdHistoryErrorCode::out_of_order_sequence);
    CHECK(history.revision() == revision_before_failure);
    CHECK(history.size() == size_before_failure);
    CHECK(history.unsent_count() == size_before_failure);
}

TEST_CASE("GoldSrc usercmd history never evicts an unsent command",
          "[goldsrc][usercmd][history][eviction][unsent-protection]")
{
    const auto binding = synthetic_binding();
    goldsrc::GoldSrcUserCmdHistoryBuilder history{{3U, 1U}};
    insert_range(history, 1U, 3U);
    const auto revision_at_capacity = history.revision();

    const auto blocked = history.insert(command(4U));
    require_history_error(
        blocked, goldsrc::GoldSrcUserCmdHistoryErrorCode::history_full);
    CHECK(history.revision() == revision_at_capacity);
    CHECK(history.size() == 3U);
    CHECK(history.unsent_count() == 3U);

    commit_new_commands(history, binding, 1U, 40U);
    REQUIRE(history.find(sequence(1U)));
    CHECK(history.find(sequence(1U))->new_transmission_count == 1U);
    const auto inserted = history.insert(command(4U));
    REQUIRE(inserted);
    CHECK(inserted.evicted_count == 1U);
    CHECK_FALSE(history.find(sequence(1U)));
    CHECK(history.find(sequence(2U)));
    CHECK(history.find(sequence(3U)));
    CHECK(history.find(sequence(4U)));
    CHECK(history.unsent_count() == 3U);
}

TEST_CASE("GoldSrc usercmd history retains its protected sent backup window",
          "[goldsrc][usercmd][history][eviction][backup]")
{
    const auto binding = synthetic_binding();
    goldsrc::GoldSrcUserCmdHistoryBuilder history{{3U, 2U}};
    insert_range(history, 1U, 3U);
    commit_new_commands(history, binding, 3U, 70U);
    CHECK(history.unsent_count() == 0U);

    const auto inserted = history.insert(command(4U));
    REQUIRE(inserted);
    CHECK(inserted.evicted_count == 1U);
    CHECK_FALSE(history.find(sequence(1U)));
    REQUIRE(history.find(sequence(2U)));
    REQUIRE(history.find(sequence(3U)));
    REQUIRE(history.find(sequence(4U)));
    CHECK(history.find(sequence(2U))->new_transmission_count == 1U);
    CHECK(history.find(sequence(3U))->new_transmission_count == 1U);

    goldsrc::GoldSrcUserCmdPacketPlannerConfig config;
    config.desired_backup_commands = 2U;
    config.maximum_backup_commands = 2U;
    config.maximum_new_commands = 1U;
    config.maximum_commands_per_packet = 3U;
    goldsrc::GoldSrcUserCmdPacketPlanner planner{config};
    auto prepared = planner.prepare(history.publish(), binding, 71U);
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    CHECK(prepared.plan->ordered_sequences() ==
          std::vector<goldsrc::GoldSrcUserCmdSequence>{
              sequence(2U), sequence(3U), sequence(4U)});
    CHECK(prepared.plan->backup_command_count() == 2U);
    CHECK(prepared.plan->new_command_count() == 1U);
    REQUIRE(planner.commit(history, std::move(*prepared.plan)));

    CHECK(history.find(sequence(2U))->backup_transmission_count == 1U);
    CHECK(history.find(sequence(3U))->backup_transmission_count == 1U);
    CHECK(history.find(sequence(4U))->new_transmission_count == 1U);
    CHECK(history.find(sequence(2U))->last_packet_sequence == 71U);
    CHECK(history.find(sequence(3U))->last_packet_sequence == 71U);
    CHECK(history.find(sequence(4U))->last_packet_sequence == 71U);
}

TEST_CASE("GoldSrc usercmd history configuration bounds fail without mutation",
          "[goldsrc][usercmd][history][limits]")
{
    for (const auto config : {
             goldsrc::GoldSrcUserCmdHistoryConfig{0U, 0U},
             goldsrc::GoldSrcUserCmdHistoryConfig{
                 goldsrc::kMaximumGoldSrcUserCmdHistoryEntries + 1U, 0U},
             goldsrc::GoldSrcUserCmdHistoryConfig{4U, 4U}}) {
        goldsrc::GoldSrcUserCmdHistoryBuilder history{config};
        CHECK_FALSE(history.valid_configuration());
        require_history_error(
            history.insert(command(1U)),
            goldsrc::GoldSrcUserCmdHistoryErrorCode::invalid_configuration);
        CHECK(history.size() == 0U);
        CHECK(history.revision() == 0U);
        CHECK(history.unsent_count() == 0U);
    }
}
