#include <hlclient/goldsrc/client_move_message.hpp>
#include <hlclient/goldsrc/usercmd_schema_binding.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

struct Options {
    std::string_view profile;
    std::string_view scenario;
};

[[nodiscard]] std::optional<Options> parse_options(
    const std::span<const std::string_view> arguments)
{
    Options options;
    bool profile_seen = false;
    bool scenario_seen = false;
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument != "--profile" && argument != "--scenario") {
            return std::nullopt;
        }
        if (index + 1U == arguments.size()) {
            return std::nullopt;
        }
        const auto value = arguments[++index];
        if (argument == "--profile") {
            if (profile_seen) {
                return std::nullopt;
            }
            profile_seen = true;
            options.profile = value;
        } else {
            if (scenario_seen) {
                return std::nullopt;
            }
            scenario_seen = true;
            options.scenario = value;
        }
    }
    if (!profile_seen || !scenario_seen || options.profile != "synthetic") {
        return std::nullopt;
    }
    constexpr std::string_view scenarios[]{
        "idle", "move", "look", "buttons", "batch", "loss-recovery"};
    for (const auto scenario : scenarios) {
        if (options.scenario == scenario) {
            return options;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<goldsrc::GoldSrcUserCmdState> make_command(
    const std::uint32_t sequence,
    const std::string_view scenario,
    const std::size_t index)
{
    const auto identity = goldsrc::GoldSrcUserCmdSequence::create(sequence);
    if (!identity) {
        return std::nullopt;
    }
    auto info = goldsrc::goldsrc_usercmd_default_create_info(
        *identity, static_cast<std::int64_t>(index) * 10'000'000);
    info.msec = 10U;
    info.sample_duration_nanoseconds = 10'000'000U;
    info.source_input_sequence = static_cast<std::uint64_t>(index + 1U);
    if (scenario == "move" || scenario == "batch" ||
        scenario == "loss-recovery") {
        info.forward_move = 200.0F + static_cast<float>(index);
        info.side_move = index == 0U ? 0.0F : -100.0F;
    }
    if (scenario == "look" || scenario == "batch") {
        info.view_angles = {
            10.0F,
            45.0F + static_cast<float>(index * 5U),
            0.0F,
        };
    }
    if (scenario == "buttons" || scenario == "batch") {
        // Project-owned typed fixture: synthetic attack + jump bits.
        info.buttons = 0x0003U;
    }
    auto created = goldsrc::GoldSrcUserCmdState::create(info);
    if (!created || !created.state) {
        return std::nullopt;
    }
    return std::move(*created.state);
}

} // namespace

int main(const int argc, const char* const* argv)
{
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const auto options = parse_options(arguments);
    if (!options) {
        std::cerr << "Usage: hlclient_usercmd_check --profile synthetic "
                     "--scenario idle|move|look|buttons|batch|loss-recovery\n";
        return 2;
    }

    auto registry = goldsrc::make_synthetic_usercmd_schema_registry();
    if (!registry || !registry.registry) {
        std::cerr << "[usercmd] synthetic schema construction failed\n";
        return 3;
    }
    auto binding = goldsrc::bind_goldsrc_usercmd_schema(*registry.registry);
    if (!binding || !binding.binding) {
        std::cerr << "[usercmd] synthetic schema binding failed\n";
        return 4;
    }

    const std::size_t command_count = options->scenario == "batch" ? 3U
        : options->scenario == "loss-recovery" ? 2U
                                                : 1U;
    const std::size_t backup_count =
        options->scenario == "loss-recovery" ? 1U : 0U;
    const std::size_t new_count = command_count - backup_count;
    std::vector<std::shared_ptr<const goldsrc::GoldSrcUserCmdState>> commands;
    commands.reserve(command_count);
    for (std::size_t index = 0U; index < command_count; ++index) {
        auto command = make_command(
            static_cast<std::uint32_t>(index + 1U),
            options->scenario,
            index);
        if (!command) {
            std::cerr << "[usercmd] typed fixture construction failed\n";
            return 5;
        }
        commands.push_back(
            std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command));
    }

    constexpr std::uint32_t kSyntheticSequence = 7U;
    const auto encoded = goldsrc::GoldSrcClientMoveMessageCodec{
        {}, goldsrc::GoldSrcClientMoveCompatibilityProfile::
                synthetic_client_move_v1}
                             .encode(
                                 commands,
                                 *binding.binding,
                                 goldsrc::GoldSrcClientMoveEncodeContext{
                                     kSyntheticSequence,
                                     0U,
                                     backup_count,
                                     new_count});
    if (!encoded || !encoded.message) {
        std::cerr << "[usercmd] synthetic envelope encoding failed\n";
        return 6;
    }
    const auto decoded = goldsrc::GoldSrcClientMoveMessageCodec{
        {}, goldsrc::GoldSrcClientMoveCompatibilityProfile::
                synthetic_client_move_v1}
                             .decode(
                                 encoded.message->bytes(),
                                 *binding.binding,
                                 goldsrc::GoldSrcClientMoveDecodeContext{
                                     kSyntheticSequence,
                                     *goldsrc::GoldSrcUserCmdSequence::create(1U),
                                     0,
                                     goldsrc::GoldSrcClientMoveEndPolicy::
                                         require_exact_end});
    bool roundtrip = decoded && decoded.message &&
        decoded.message->commands().size() == command_count &&
        decoded.message->backup_command_count() == backup_count &&
        decoded.message->new_command_count() == new_count &&
        decoded.bytes_consumed == encoded.message->bytes().size() &&
        decoded.next_byte_offset == encoded.message->bytes().size();
    std::vector<std::shared_ptr<const goldsrc::GoldSrcUserCmdState>>
        decoded_commands;
    if (roundtrip) {
        decoded_commands.reserve(decoded.message->commands().size());
        for (std::size_t index = 0U;
             index < decoded.message->commands().size();
             ++index) {
            const auto& command = decoded.message->commands()[index];
            roundtrip = roundtrip &&
                command.command_sequence().value() == index + 1U;
            decoded_commands.push_back(
                std::make_shared<const goldsrc::GoldSrcUserCmdState>(command));
        }
    }
    if (roundtrip) {
        const auto reencoded = goldsrc::GoldSrcClientMoveMessageCodec{
            {}, goldsrc::GoldSrcClientMoveCompatibilityProfile::
                    synthetic_client_move_v1}
                                   .encode(
                                       decoded_commands,
                                       *binding.binding,
                                       goldsrc::GoldSrcClientMoveEncodeContext{
                                           kSyntheticSequence,
                                           0U,
                                           backup_count,
                                           new_count});
        roundtrip = reencoded && reencoded.message &&
            reencoded.message->bit_length() == encoded.message->bit_length() &&
            reencoded.message->changed_field_count() ==
                encoded.message->changed_field_count() &&
            reencoded.message->bytes() == encoded.message->bytes();
    }

    std::cout << "[usercmd] profile=synthetic\n";
    std::cout << "[usercmd] schema-fields="
              << binding.binding->entries().size() << '\n';
    std::cout << "[usercmd] changed-fields="
              << encoded.message->changed_field_count() << '\n';
    std::cout << "[usercmd] commands=" << command_count << '\n';
    std::cout << "[usercmd] new=" << new_count << '\n';
    std::cout << "[usercmd] backup=" << backup_count << '\n';
    std::cout << "[usercmd] encoded-bits="
              << encoded.message->bit_length() << '\n';
    std::cout << "[usercmd] checksum=confirmed-synthetic\n";
    std::cout << "[usercmd] roundtrip=" << (roundtrip ? "ok" : "failed")
              << '\n';
    return roundtrip ? 0 : 7;
}
