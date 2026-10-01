#include <hlclient/core/command_line.hpp>
#include <hlclient/core/remote_audio_peer_plan.hpp>
#include <hlclient/core/manual_session_timing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hlclient::core::parse_command_line;
using hlclient::core::RendererBackend;

TEST_CASE("Same-host peer plan keeps both clients audible when focused", "[e9][core][command-line]") {
    const std::vector<std::wstring> args{L"--renderer",L"opengl",L"--connect",L"127.0.0.1:27243",
        L"--stop-after",L"live-visual-control",L"--live-input",L"keyboard-mouse",L"--game",L"valve",
        L"--name",L"fixture",L"--auth-provider",L"steam",L"--steam-api-runtime",L"D:/fixture/steam_api.dll",
        L"--basedir",L"D:/fixture",L"--live-session-seconds",L"45",L"--prediction",L"reference"};
    const auto plan=hlclient::core::remote_audio_peer_plan(args); REQUIRE(plan);
    std::size_t client_index = 0;
    for(const auto& client:{plan->listener,plan->mover}) {
        std::vector<std::string> narrow;
        for(const auto& arg:client) {
            std::string converted; for(const auto c:arg) converted.push_back(static_cast<char>(c));
            narrow.push_back(std::move(converted));
        }
        std::vector<std::string_view> views; for(const auto& arg:narrow) views.push_back(arg);
        const auto parsed=parse_command_line(views); REQUIRE(parsed);
        CHECK(parsed.options->connect_endpoint=="127.0.0.1:27243");
        CHECK(parsed.options->reference_prediction);
        CHECK(parsed.options->live_input==hlclient::core::LiveInputMode::keyboard_mouse);
        CHECK(parsed.options->player_name==(client_index++==0 ? "HLC_E9_A" : "HLC_E9_B"));
        CHECK(parsed.options->audio_volume==35);
        CHECK_FALSE(parsed.options->audio_on_focus_loss);
    }
    for(const auto key:{L"--connect",L"--live-input",L"--renderer",L"--name",L"--auth-provider",L"--basedir",L"--live-session-seconds"}) {
        auto bad=args; *std::next(std::find(bad.begin(),bad.end(),key))=L"";
        CHECK_FALSE(hlclient::core::remote_audio_peer_plan(bad));
    }
    for(const auto& override_arguments : std::vector<std::vector<std::wstring>>{
            {L"--audio-volume", L"0"}, {L"--audio-on-focus-loss"}}) {
        auto bad=args;
        bad.insert(bad.end(),override_arguments.begin(),override_arguments.end());
        CHECK_FALSE(hlclient::core::remote_audio_peer_plan(bad));
    }
    const std::array<std::string_view,1> invalid{"--audio-on-focus-loss"}; CHECK_FALSE(parse_command_line(invalid));
}

TEST_CASE("Manual timers accept long or explicitly user-ended sessions without relaxing startup", "[core][command-line][manual-timing]") {
    const std::vector<std::string_view> base{"--connect", "127.0.0.1:27243", "--stop-after", "live-visual-control",
        "--live-input", "keyboard-mouse", "--basedir", "D:/fixture", "--game", "valve",
        "--auth-provider", "steam", "--steam-api-runtime", "D:/fixture/api.dll"};
    for (const auto value : {"1", "45", "300", "3600", "86400"}) {
        auto args = base; args.insert(args.end(), {"--live-session-seconds", value});
        const auto parsed = parse_command_line(args); INFO(parsed.error); REQUIRE(parsed);
        CHECK(parsed.options->live_session_seconds);
        CHECK_FALSE(parsed.options->live_session_unlimited);
    }
    for (const auto value : {"0", "-1", "86401", "999999999999999999999", "1.5", "none"}) {
        auto args = base; args.insert(args.end(), {"--live-session-seconds", value});
        CHECK_FALSE(parse_command_line(args));
    }
    auto unlimited = base; unlimited.push_back("--live-session-unlimited");
    const auto parsed = parse_command_line(unlimited); REQUIRE(parsed);
    CHECK(parsed.options->live_session_unlimited); CHECK_FALSE(parsed.options->live_session_seconds);
    auto duplicate = unlimited; duplicate.push_back("--live-session-unlimited"); CHECK_FALSE(parse_command_line(duplicate));
    auto conflicting = unlimited; conflicting.insert(conflicting.end(), {"--live-session-seconds", "45"}); CHECK_FALSE(parse_command_line(conflicting));
    unlimited[5] = "scripted-damage-respawn-check"; CHECK_FALSE(parse_command_line(unlimited));
    const std::array<std::string_view, 1> out_of_context{"--live-session-unlimited"}; CHECK_FALSE(parse_command_line(out_of_context));
    using hlclient::core::manual_client_wait_expired;
    CHECK_FALSE(manual_client_wait_expired(59'999U, false, std::nullopt));
    CHECK(manual_client_wait_expired(60'000U, false, std::nullopt));
    CHECK_FALSE(manual_client_wait_expired(86'400'000U, true, std::nullopt));
    CHECK_FALSE(manual_client_wait_expired(359'999U, true, 300U));
    CHECK(manual_client_wait_expired(360'000U, true, 300U));
    std::vector<std::wstring> peer{L"--connect",L"127.0.0.1:27243",L"--stop-after",L"live-visual-control",
        L"--live-input",L"keyboard-mouse",L"--renderer",L"opengl",L"--game",L"valve",L"--name",L"fixture",
        L"--auth-provider",L"steam",L"--steam-api-runtime",L"D:/fixture/api.dll",L"--basedir",L"D:/fixture",L"--live-session-unlimited"};
    const auto plan = hlclient::core::remote_audio_peer_plan(peer); REQUIRE(plan);
    for (const auto& client : {plan->listener, plan->mover}) {
        CHECK(std::find(client.begin(), client.end(), L"--live-session-unlimited") != client.end());
        CHECK(std::find(client.begin(), client.end(), L"--live-session-seconds") == client.end());
    }
    peer.insert(peer.end(), {L"--live-session-seconds", L"45"});
    CHECK_FALSE(hlclient::core::remote_audio_peer_plan(peer));
}

TEST_CASE("Test start health accepts only the explicit owned reference manual profile", "[core][command-line][test-start-health]") {
    std::array<std::string_view, 18> args{"--connect", "127.0.0.1:27243", "--stop-after", "live-visual-control",
        "--live-input", "keyboard-mouse", "--prediction", "reference", "--auth-provider", "steam",
        "--steam-api-runtime", "D:/fixture/steam_api.dll", "--name", "HLC50_0123456789abcdef01234567",
        "--basedir", "D:/fixture", "--test-start-health", "50"};
    const auto parsed = parse_command_line(args);
    REQUIRE(parsed); CHECK(parsed.options->test_start_health);
    args[17]="100"; CHECK_FALSE(parse_command_line(args)); args[17]="50";
    args[1]="192.0.2.1:27243"; CHECK_FALSE(parse_command_line(args)); args[1]="127.0.0.1:27243";
    args[7]="off"; CHECK_FALSE(parse_command_line(args)); args[7]="reference";
    args[5]="scripted-damage-respawn-check"; CHECK_FALSE(parse_command_line(args));
}

TEST_CASE("Glock fire mute diagnostic is opt-in and restricted to manual Half-Life visuals",
          "[core][command-line][audio-diagnostic]") {
    const std::array<std::string_view, 17> manual{
        "--renderer", "opengl", "--connect", "127.0.0.1:27243",
        "--stop-after", "live-visual-control", "--live-input", "keyboard-mouse",
        "--basedir", "D:/fixture", "--game", "valve",
        "--auth-provider", "steam", "--steam-api-runtime", "D:/fixture/steam_api.dll",
        "--mute-glock-fire-sound"};
    const auto accepted=parse_command_line(manual);
    REQUIRE(accepted);
    CHECK(accepted.options->mute_glock_fire_sound);
    auto invalid=manual;
    invalid[7]="scripted-fire-reload-check";
    CHECK_FALSE(parse_command_line(invalid));
    invalid[7]="keyboard-mouse";
    invalid[5]="delta-schemas";
    CHECK_FALSE(parse_command_line(invalid));
    auto duplicate=std::vector<std::string_view>{manual.begin(),manual.end()};
    duplicate.push_back("--mute-glock-fire-sound");
    CHECK_FALSE(parse_command_line(duplicate));
    const std::array<std::string_view, 5> replay{"--renderer", "null",
        "--runtime-replay-fixture", "basic-mixed", "--mute-glock-fire-sound"};
    CHECK_FALSE(parse_command_line(replay));
}

TEST_CASE("Command line parser supplies safe defaults", "[core][command-line]")
{
    const std::array<std::string_view, 0> arguments{};

    const auto result = parse_command_line(arguments);

    REQUIRE(result);
    CHECK_FALSE(result.options->show_help);
    CHECK_FALSE(result.options->show_version);
    CHECK_FALSE(result.options->net_trace);
    CHECK_FALSE(result.options->view_world);
    CHECK_FALSE(result.options->view_entity_snapshot);
    CHECK_FALSE(result.options->mute_glock_fire_sound);
    CHECK_FALSE(result.options->base_directory.has_value());
    CHECK(result.options->game_directory == "valve");
    CHECK_FALSE(result.options->connect_endpoint.has_value());
    CHECK(result.options->stop_after == hlclient::core::ConnectionStopPoint::challenge);
    CHECK_FALSE(result.options->authentication_provider.has_value());
    CHECK_FALSE(result.options->authentication_material_file.has_value());
    CHECK_FALSE(result.options->resource_consistency_provider.has_value());
    CHECK(result.options->player_name == "Player");
    CHECK(result.options->player_model == "ivan");
    CHECK(result.options->renderer == RendererBackend::opengl);
    CHECK(result.error.empty());
}

TEST_CASE("Command line parser accepts supported options", "[core][command-line]")
{
    const std::array arguments{
        std::string_view{"--help"},
        std::string_view{"--version"},
        std::string_view{"--net-trace"},
        std::string_view{"--basedir"},
        std::string_view{"C:/Games/Half-Life"},
        std::string_view{"--game"},
        std::string_view{"cstrike"},
        std::string_view{"+connect"},
        std::string_view{"127.0.0.1:27015"},
    };

    const auto result = parse_command_line(arguments);

    REQUIRE(result);
    CHECK(result.options->show_help);
    CHECK(result.options->show_version);
    CHECK(result.options->net_trace);
    REQUIRE(result.options->base_directory.has_value());
    CHECK(*result.options->base_directory == "C:/Games/Half-Life");
    CHECK(result.options->game_directory == "cstrike");
    REQUIRE(result.options->connect_endpoint.has_value());
    CHECK(*result.options->connect_endpoint == "127.0.0.1:27015");
}

TEST_CASE("Command line parser accepts the long connect spelling", "[core][command-line]")
{
    const std::array arguments{
        std::string_view{"--connect"},
        std::string_view{"192.0.2.10:27016"},
    };

    const auto result = parse_command_line(arguments);

    REQUIRE(result);
    REQUIRE(result.options->connect_endpoint.has_value());
    CHECK(*result.options->connect_endpoint == "192.0.2.10:27016");
}

TEST_CASE("Command line parser selects a renderer backend", "[core][command-line]")
{
    SECTION("null renderer")
    {
        const std::array arguments{
            std::string_view{"--renderer"},
            std::string_view{"null"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->renderer == RendererBackend::null);
    }

    SECTION("unsupported renderer")
    {
        const std::array arguments{
            std::string_view{"--renderer"},
            std::string_view{"software"},
        };
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("Unsupported renderer") != std::string::npos);
    }

    SECTION("missing renderer name")
    {
        const std::array arguments{std::string_view{"--renderer"}};
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("Missing value") != std::string::npos);
    }
}

TEST_CASE("Command line parser validates explicit connect request mode", "[core][command-line]")
{
    SECTION("legacy connect-request file spelling remains accepted")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"connect-request"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--name"}, std::string_view{"Test Player"},
            std::string_view{"--model"}, std::string_view{"ivan"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::connect_request);
        REQUIRE(result.options->authentication_provider);
        CHECK(*result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::file);
        REQUIRE(result.options->authentication_material_file);
        CHECK(*result.options->authentication_material_file == "auth.bin");
        CHECK(result.options->player_name == "Test Player");
        CHECK(result.options->player_model == "ivan");
    }

    SECTION("legacy connect-response file spelling remains accepted")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"connect-response"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::connect_response);
        REQUIRE(result.options->authentication_provider);
        CHECK(*result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::file);
        REQUIRE(result.options->authentication_material_file);
        CHECK(*result.options->authentication_material_file == "auth.bin");
    }

    SECTION("explicit file provider supports netchan bootstrap")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"netchan-bootstrap"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::netchan_bootstrap);
        REQUIRE(result.options->authentication_provider);
        CHECK(*result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::file);
        REQUIRE(result.options->authentication_material_file);
        CHECK(*result.options->authentication_material_file == "auth.bin");
    }

    SECTION("explicit file provider supports the initial sign-on boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"signon-boundary"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::signon_boundary);
        REQUIRE(result.options->authentication_provider);
        CHECK(*result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::file);
        REQUIRE(result.options->authentication_material_file);
        CHECK(*result.options->authentication_material_file == "auth.bin");
    }

    SECTION("explicit file provider supports the pre-resource boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"pre-resource"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::pre_resource);
        REQUIRE(result.options->authentication_provider);
        CHECK(*result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::file);
        REQUIRE(result.options->authentication_material_file);
        CHECK(*result.options->authentication_material_file == "auth.bin");
    }

    SECTION("explicit file provider supports the delta-schema boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"delta-schemas"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::delta_schemas);
        REQUIRE(result.options->authentication_provider);
        CHECK(*result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::file);
    }

    SECTION("live runtime state accepts the production Steam argv profile")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"null"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-runtime-state"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steamapps/common/Half-Life/steam_api.dll"},
            std::string_view{"--name"}, std::string_view{"HLC_M472D"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::live_runtime_state);
        CHECK(result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::steam);
        CHECK_FALSE(result.options->resource_consistency_provider);
        CHECK(result.options->renderer == RendererBackend::null);
        CHECK_FALSE(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("live usercmd check accepts its distinct production Steam argv profile")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"null"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-usercmd-check"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steamapps/common/Half-Life/steam_api.dll"},
            std::string_view{"--name"}, std::string_view{"HLC_M472E"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::live_usercmd_check);
        CHECK(result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::steam);
        CHECK_FALSE(result.options->resource_consistency_provider);
        CHECK(result.options->renderer == RendererBackend::null);
        CHECK_FALSE(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("live visual scripted check accepts the production OpenGL argv profile")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"opengl"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-visual-control"},
            std::string_view{"--live-input"},
            std::string_view{"scripted-check"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
            std::string_view{"--game"}, std::string_view{"valve"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steamapps/common/Half-Life/steam_api.dll"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::live_visual_control);
        CHECK(result.options->live_input ==
              hlclient::core::LiveInputMode::scripted_check);
        CHECK(result.options->renderer == RendererBackend::opengl);
        CHECK_FALSE(result.options->live_session_seconds);
    }

    SECTION("live visual keyboard mode accepts an explicit bounded session")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"opengl"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-visual-control"},
            std::string_view{"--live-input"},
            std::string_view{"keyboard-mouse"},
            std::string_view{"--live-session-seconds"}, std::string_view{"45"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
            std::string_view{"--game"}, std::string_view{"valve"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->live_input ==
              hlclient::core::LiveInputMode::keyboard_mouse);
        CHECK(result.options->live_session_seconds == 45U);
    }

    SECTION("live visual side check is a distinct typed scripted input")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"opengl"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-visual-control"},
            std::string_view{"--live-input"},
            std::string_view{"scripted-side-check"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
            std::string_view{"--game"}, std::string_view{"valve"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->live_input ==
              hlclient::core::LiveInputMode::scripted_side_check);
    }

    SECTION("live jump duck check is an explicit visual input source")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"opengl"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-visual-control"},
            std::string_view{"--live-input"},
            std::string_view{"scripted-jump-duck-check"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
            std::string_view{"--game"}, std::string_view{"valve"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->live_input ==
              hlclient::core::LiveInputMode::scripted_jump_duck_check);
    }

    SECTION("live speed check is an explicit visual input source")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"opengl"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-visual-control"},
            std::string_view{"--live-input"},
            std::string_view{"scripted-speed-check"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
            std::string_view{"--game"}, std::string_view{"valve"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->live_input ==
              hlclient::core::LiveInputMode::scripted_speed_check);
        CHECK_FALSE(result.options->reference_prediction);
        const std::array reference_arguments{
            std::string_view{"--renderer"}, std::string_view{"opengl"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-visual-control"},
            std::string_view{"--live-input"},
            std::string_view{"scripted-speed-check"},
            std::string_view{"--prediction"}, std::string_view{"reference"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
            std::string_view{"--game"}, std::string_view{"valve"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
        };
        const auto reference = parse_command_line(reference_arguments);
        REQUIRE(reference);
        CHECK(reference.options->reference_prediction);
        const std::array wrong_mode{
            std::string_view{"--prediction"}, std::string_view{"reference"}};
        CHECK_FALSE(parse_command_line(wrong_mode));
    }

    SECTION("bounded fire and reload input requires explicit live visual mode")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"opengl"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-visual-control"},
            std::string_view{"--live-input"},
            std::string_view{"scripted-fire-reload-check"},
            std::string_view{"--prediction"}, std::string_view{"reference"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
            std::string_view{"--game"}, std::string_view{"valve"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->live_input ==
              hlclient::core::LiveInputMode::scripted_fire_reload_check);
        CHECK(result.options->reference_prediction);
        auto presentation_arguments = arguments;
        presentation_arguments[7] = "scripted-fire-reload-presentation-check";
        const auto presentation = parse_command_line(presentation_arguments);
        REQUIRE(presentation);
        CHECK(presentation.options->live_input ==
              hlclient::core::LiveInputMode::scripted_fire_reload_presentation_check);
        CHECK(presentation.options->reference_prediction);
        auto lifecycle_arguments = arguments;
        lifecycle_arguments[7] = "scripted-damage-respawn-check";
        const auto lifecycle = parse_command_line(lifecycle_arguments);
        REQUIRE(lifecycle);
        CHECK(lifecycle.options->live_input ==
              hlclient::core::LiveInputMode::scripted_damage_respawn_check);
        CHECK(lifecycle.options->reference_prediction);
    }

    SECTION("live visual mode rejects D renderer and missing explicit input")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"null"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-visual-control"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
            std::string_view{"--game"}, std::string_view{"valve"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
        };
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("requires --live-input") != std::string::npos);
    }

    SECTION("headless E rejects F live input")
    {
        const std::array arguments{
            std::string_view{"--renderer"}, std::string_view{"null"},
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-usercmd-check"},
            std::string_view{"--live-input"}, std::string_view{"scripted-check"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
        };
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("live input options require") !=
              std::string::npos);
    }

    SECTION("live runtime state rejects custom-resource provider selection")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27243"},
            std::string_view{"--stop-after"},
            std::string_view{"live-runtime-state"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"D:/Steam/steam_api.dll"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"D:/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("no client custom resource") !=
              std::string::npos);
    }

    SECTION("explicit file provider supports the movement-environment boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"movevars"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::movevars);
        REQUIRE(result.options->authentication_provider);
        CHECK(*result.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::file);
    }

    SECTION("explicit file provider supports the user-info boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"user-info"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::user_info);
    }

    SECTION("explicit file provider supports the neutral resource-list boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"resource-list-boundary"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::resource_list_boundary);
    }

    SECTION("explicit file provider supports the parsed resource list")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"resource-list"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::resource_list);
    }

    SECTION("explicit file provider supports the post-resource response boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"resource-response-boundary"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::resource_response_boundary);
    }

    SECTION("explicit local consistency provider supports the response boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"resource-response-boundary"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        REQUIRE(result.options->resource_consistency_provider);
        CHECK(*result.options->resource_consistency_provider ==
              hlclient::core::ResourceConsistencyProviderKind::local);
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("precache manifest requires and schedules the local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"precache-manifest"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::precache_manifest);
        CHECK(result.options->game_directory == "valve");
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("entity diagnostic stop points require and schedule the local provider")
    {
        for (const auto stop : {std::string_view{"server-baselines"},
                                std::string_view{"entity-snapshot"},
                                std::string_view{"usercmd-boundary"}}) {
            CAPTURE(stop);
            const std::array arguments{
                std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
                std::string_view{"--stop-after"}, stop,
                std::string_view{"--auth-provider"}, std::string_view{"file"},
                std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
                std::string_view{"--resource-consistency-provider"},
                std::string_view{"local"},
                std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
                std::string_view{"--renderer"}, std::string_view{"null"},
            };
            const auto result = parse_command_line(arguments);
            REQUIRE(result);
            const auto expected = stop == "server-baselines"
                ? hlclient::core::ConnectionStopPoint::server_baselines
                : stop == "entity-snapshot"
                    ? hlclient::core::ConnectionStopPoint::entity_snapshot
                    : hlclient::core::ConnectionStopPoint::usercmd_boundary;
            CHECK(result.options->stop_after == expected);
            CHECK(hlclient::core::requires_local_resource_consistency_preparation(
                *result.options));
        }
    }

    SECTION("entity diagnostic stop points reject a missing local provider")
    {
        for (const auto stop : {std::string_view{"server-baselines"},
                                std::string_view{"entity-snapshot"},
                                std::string_view{"usercmd-boundary"}}) {
            const std::array arguments{
                std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
                std::string_view{"--stop-after"}, stop,
                std::string_view{"--auth-provider"}, std::string_view{"file"},
                std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            };
            const auto result = parse_command_line(arguments);
            CAPTURE(stop);
            CHECK_FALSE(result);
            CHECK(result.error.find("require --resource-consistency-provider local") !=
                  std::string::npos);
        }
    }

    SECTION("precache manifest rejects a missing local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"precache-manifest"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("requires --resource-consistency-provider local") !=
              std::string::npos);
    }

    SECTION("asset dispatch requires and schedules the local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"asset-dispatch"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::asset_dispatch);
        CHECK(result.options->game_directory == "valve");
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("asset dispatch rejects a missing local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"asset-dispatch"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };

        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("requires --resource-consistency-provider local") !=
              std::string::npos);
    }

    SECTION("world geometry requires and schedules the local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"world-geometry"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::world_geometry);
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("collision world requires and schedules the local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"collision-world"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
            std::string_view{"--renderer"}, std::string_view{"null"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::collision_world);
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("world geometry rejects a missing local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"world-geometry"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };

        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("requires --resource-consistency-provider local") !=
              std::string::npos);
    }

    SECTION("world textures requires and schedules the local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"world-textures"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::world_textures);
        CHECK(result.options->game_directory == "valve");
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("world textures rejects a missing local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"world-textures"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };

        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("requires --resource-consistency-provider local") !=
              std::string::npos);
    }

    SECTION("world render package requires and schedules the local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"world-render-package"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
            std::string_view{"--renderer"}, std::string_view{"null"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::world_render_package);
        CHECK_FALSE(result.options->view_world);
        CHECK(result.options->renderer == RendererBackend::null);
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("world render package rejects a missing local provider")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"world-render-package"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };

        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("requires --resource-consistency-provider local") !=
              std::string::npos);
    }

    SECTION("view world selects the spatial scene boundary and historical defaults")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--view-world"},
            std::string_view{"--renderer"}, std::string_view{"opengl"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->view_world);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::world_spatial_scene);
        CHECK(result.options->renderer == RendererBackend::opengl);
        CHECK(result.options->world_visibility ==
              hlclient::core::WorldVisibilityOption::all);
        CHECK(result.options->brush_submodels ==
              hlclient::core::BrushSubmodelsOption::off);
        CHECK(result.options->world_camera ==
              hlclient::core::WorldCameraOption::static_camera);
    }

    SECTION("spatial scene accepts explicit PVS brush and spawn options")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"world-spatial-scene"},
            std::string_view{"--renderer"}, std::string_view{"null"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
            std::string_view{"--visibility"}, std::string_view{"pvs-frustum"},
            std::string_view{"--brush-submodels"}, std::string_view{"static"},
            std::string_view{"--camera"}, std::string_view{"spawn"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::world_spatial_scene);
        CHECK(result.options->renderer == RendererBackend::null);
        CHECK(result.options->world_visibility ==
              hlclient::core::WorldVisibilityOption::pvs_frustum);
        CHECK(result.options->brush_submodels ==
              hlclient::core::BrushSubmodelsOption::static_initial);
        CHECK(result.options->world_camera ==
              hlclient::core::WorldCameraOption::spawn);
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("visibility options are rejected outside a spatial route")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"connect-request"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--visibility"}, std::string_view{"pvs"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("world-spatial-scene") != std::string::npos);
    }

    SECTION("view world rejects NullRenderer")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--view-world"},
            std::string_view{"--renderer"}, std::string_view{"null"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("requires --renderer opengl") !=
              std::string::npos);
    }

    SECTION("entity visual scene is an explicit local-provider boundary")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"},
            std::string_view{"entity-visual-scene"},
            std::string_view{"--renderer"}, std::string_view{"null"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        REQUIRE(result);
        CHECK(result.options->stop_after ==
              hlclient::core::ConnectionStopPoint::entity_visual_scene);
        CHECK_FALSE(result.options->view_entity_snapshot);
        CHECK(hlclient::core::requires_local_resource_consistency_preparation(
            *result.options));
    }

    SECTION("entity snapshot preview requires OpenGL")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--view-entity-snapshot"},
            std::string_view{"--renderer"}, std::string_view{"null"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("requires --renderer opengl") !=
              std::string::npos);
    }

    SECTION("invalid stop point")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"signon"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("connect request requires local auth file")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"connect-request"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("connect response requires local auth file")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"connect-response"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("netchan bootstrap requires local auth file")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"netchan-bootstrap"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("netchan bootstrap requires explicit file provider selection")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"netchan-bootstrap"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("--auth-provider file") != std::string::npos);
    }

    SECTION("sign-on boundary requires local auth file")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"signon-boundary"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("sign-on boundary requires explicit file provider selection")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"signon-boundary"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("--auth-provider file") != std::string::npos);
    }

    SECTION("pre-resource boundary requires local auth file")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"pre-resource"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("pre-resource boundary requires explicit file provider selection")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"pre-resource"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("--auth-provider file") != std::string::npos);
    }

    SECTION("delta schemas require explicit file provider selection")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"delta-schemas"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("--auth-provider file") != std::string::npos);
    }

    SECTION("movevars boundary requires local auth file")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"movevars"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("movevars boundary requires explicit file provider selection")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"movevars"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("--auth-provider file") != std::string::npos);
    }

    SECTION("unsafe pre-resource bypass and execution switches are rejected")
    {
        constexpr std::array rejected{
            std::string_view{"--raw-serverinfo"},
            std::string_view{"--skip-serverinfo"},
            std::string_view{"--send-resource-command"},
            std::string_view{"--raw-stringcmd"},
            std::string_view{"--execute-stufftext"},
            std::string_view{"--mount-server-map"},
            std::string_view{"--download-resource"},
            std::string_view{"--raw-client-command"},
            std::string_view{"--raw-server-message"},
            std::string_view{"--inject-entity"},
            std::string_view{"--entity-snapshot-file"},
            std::string_view{"--replay-snapshot"},
            std::string_view{"--force-entity-schema"},
            std::string_view{"--ignore-delta-base"},
            std::string_view{"--skip-baselines"},
            std::string_view{"--spawn-entity"},
            std::string_view{"--set-entity-origin"},
            std::string_view{"--send-usercmd"},
            std::string_view{"--skip-auth"},
            std::string_view{"--raw-delta"},
            std::string_view{"--inject-delta"},
            std::string_view{"--skip-delta"},
            std::string_view{"--apply-delta"},
            std::string_view{"--sendres"},
            std::string_view{"--resource-response"},
            std::string_view{"--mount-server-path"},
            std::string_view{"--set-server-gravity"},
            std::string_view{"--override-movevars"},
            std::string_view{"--apply-movevars"},
            std::string_view{"--skip-movevars"},
            std::string_view{"--raw-opcode44"},
            std::string_view{"--raw-userinfo"},
            std::string_view{"--set-server-userinfo"},
            std::string_view{"--raw-sendres"},
            std::string_view{"--parse-resources"},
            std::string_view{"--resource-path"},
            std::string_view{"--mount-resource"},
            std::string_view{"--parse-resource-file"},
            std::string_view{"--resource-cache"},
            std::string_view{"--send-resource-response"},
            std::string_view{"--consistency-response"},
            std::string_view{"--ignore-resource-hash"},
            std::string_view{"--raw-resource-list"},
            std::string_view{"--fake-steam-id"},
            std::string_view{"--raw-resource-response"},
            std::string_view{"--response-file"},
            std::string_view{"--captured-response"},
            std::string_view{"--resource-checksum-file"},
            std::string_view{"--ignore-consistency"},
            std::string_view{"--fake-consistency"},
            std::string_view{"--send-opcode5"},
            std::string_view{"--skip-resource-response"},
            std::string_view{"--resource-root"},
            std::string_view{"--consistency-file"},
            std::string_view{"--tempdecal-path"},
            std::string_view{"--hash-file"},
            std::string_view{"--opaque-material-file"},
            std::string_view{"--download-resource"},
        };
        for (const auto argument : rejected) {
            CAPTURE(argument);
            const std::array arguments{argument};
            const auto result = parse_command_line(arguments);
            CHECK_FALSE(result);
            CHECK(result.error.find("Unknown command-line argument") !=
                  std::string::npos);
        }
    }

    SECTION("explicit file provider requires its material path")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"connect-response"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("--auth-material-file") != std::string::npos);
    }

    SECTION("unsupported authentication providers are rejected")
    {
        constexpr std::array unsupported{
            std::string_view{"none"},
            std::string_view{"bypass"},
        };
        for (const auto provider : unsupported) {
            CAPTURE(provider);
            const std::array arguments{
                std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
                std::string_view{"--stop-after"}, std::string_view{"netchan-bootstrap"},
                std::string_view{"--auth-provider"}, provider,
                std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
            };
            const auto result = parse_command_line(arguments);
            CHECK_FALSE(result);
            CHECK(result.error.find("Unsupported authentication provider") !=
                  std::string::npos);
        }
    }

    SECTION("Steam provider requires its explicit runtime and no auth file")
    {
        const std::array valid{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"delta-schemas"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"C:/Steam/steam_api.dll"},
            std::string_view{"--renderer"}, std::string_view{"null"},
        };
        const auto accepted = parse_command_line(valid);
        REQUIRE(accepted);
        CHECK(accepted.options->authentication_provider ==
              hlclient::core::AuthenticationProviderKind::steam);
        CHECK(accepted.options->steam_api_runtime ==
              "C:/Steam/steam_api.dll");
        CHECK_FALSE(accepted.options->authentication_material_file);

        const std::array missing_runtime{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"connect-response"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
        };
        const auto missing = parse_command_line(missing_runtime);
        CHECK_FALSE(missing);
        CHECK(missing.error.find("--steam-api-runtime") != std::string::npos);

        const std::array forbidden_file{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--stop-after"}, std::string_view{"connect-response"},
            std::string_view{"--auth-provider"}, std::string_view{"steam"},
            std::string_view{"--steam-api-runtime"},
            std::string_view{"C:/Steam/steam_api.dll"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        const auto forbidden = parse_command_line(forbidden_file);
        CHECK_FALSE(forbidden);
        CHECK(forbidden.error.find("does not accept") != std::string::npos);
    }

    SECTION("local consistency provider requires an explicit basedir")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("--basedir") != std::string::npos);
    }

    SECTION("unsupported consistency provider is rejected")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"captured"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("Unsupported resource-consistency provider") !=
              std::string::npos);
    }

    SECTION("early stop accepts selection without scheduling provider preparation")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--resource-consistency-provider"},
            std::string_view{"local"},
            std::string_view{"--basedir"}, std::string_view{"C:/Games/Half-Life"},
        };
        const auto result = parse_command_line(arguments);
        REQUIRE(result);
        CHECK_FALSE(
            hlclient::core::requires_local_resource_consistency_preparation(
                *result.options));
    }

    SECTION("every pre-response stop leaves local provider preparation dormant")
    {
        constexpr std::array pre_response_stop_points{
            hlclient::core::ConnectionStopPoint::challenge,
            hlclient::core::ConnectionStopPoint::connect_request,
            hlclient::core::ConnectionStopPoint::connect_response,
            hlclient::core::ConnectionStopPoint::netchan_bootstrap,
            hlclient::core::ConnectionStopPoint::signon_boundary,
            hlclient::core::ConnectionStopPoint::pre_resource,
            hlclient::core::ConnectionStopPoint::delta_schemas,
            hlclient::core::ConnectionStopPoint::movevars,
            hlclient::core::ConnectionStopPoint::user_info,
            hlclient::core::ConnectionStopPoint::resource_list_boundary,
            hlclient::core::ConnectionStopPoint::resource_list,
        };
        for (const auto stop_point : pre_response_stop_points) {
            hlclient::core::CommandLineOptions options;
            options.connect_endpoint = "127.0.0.1:27015";
            options.base_directory = "C:/Games/Half-Life";
            options.resource_consistency_provider =
                hlclient::core::ResourceConsistencyProviderKind::local;
            options.stop_after = stop_point;
            CAPTURE(static_cast<int>(stop_point));
            CHECK_FALSE(
                hlclient::core::requires_local_resource_consistency_preparation(
                    options));
        }
    }

    SECTION("connect-only settings require connect")
    {
        const std::array arguments{
            std::string_view{"--name"}, std::string_view{"Player"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("auth file cannot alter challenge-only mode")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("auth provider cannot alter challenge-only mode")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("auth provider requires connect")
    {
        const std::array arguments{
            std::string_view{"--auth-provider"}, std::string_view{"file"},
            std::string_view{"--auth-material-file"}, std::string_view{"auth.bin"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }

    SECTION("identity settings cannot be silently ignored in challenge-only mode")
    {
        const std::array arguments{
            std::string_view{"--connect"}, std::string_view{"127.0.0.1:27015"},
            std::string_view{"--name"}, std::string_view{"Test Player"},
        };
        CHECK_FALSE(parse_command_line(arguments));
    }
}

TEST_CASE("Command line parser reports malformed input", "[core][command-line]")
{
    SECTION("unknown option")
    {
        const std::array arguments{std::string_view{"--unknown"}};
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("Unknown command-line argument") != std::string::npos);
    }

    SECTION("raw protocol and consistency-file options remain unavailable")
    {
        constexpr std::array forbidden{
            std::string_view{"--send-reliable"},
            std::string_view{"--raw-netchan-payload"},
            std::string_view{"--inject-clc"},
            std::string_view{"--raw-client-message"},
            std::string_view{"--send-stringcmd"},
            std::string_view{"--execute-stufftext"},
            std::string_view{"--raw-svc"},
            std::string_view{"--skip-auth"},
            std::string_view{"--no-steam-auth"},
            std::string_view{"--consistency-file"},
            std::string_view{"--tempdecal-path"},
            std::string_view{"--hash-file"},
            std::string_view{"--opaque-material-file"},
        };
        for (const auto option : forbidden) {
            CAPTURE(option);
            const std::array arguments{option};
            const auto result = parse_command_line(arguments);
            CHECK_FALSE(result);
            CHECK(result.error.find("Unknown command-line argument") !=
                  std::string::npos);
        }
    }

    SECTION("asset loading and download options remain unavailable")
    {
        constexpr std::array forbidden{
            std::string_view{"--asset-file"},
            std::string_view{"--raw-asset"},
            std::string_view{"--import-file"},
            std::string_view{"--load-map"},
            std::string_view{"--parse-bsp"},
            std::string_view{"--parse-mdl"},
            std::string_view{"--parse-spr"},
            std::string_view{"--decode-wav"},
            std::string_view{"--force-importer"},
            std::string_view{"--ignore-importer-ambiguity"},
            std::string_view{"--trust-extension"},
            std::string_view{"--skip-locator-check"},
            std::string_view{"--load-resource"},
            std::string_view{"--precache-file"},
            std::string_view{"--download-missing"},
            std::string_view{"--ignore-missing"},
            std::string_view{"--mount-resource"},
            std::string_view{"--resource-cache"},
            std::string_view{"--trust-resource-size"},
            std::string_view{"--skip-resource-safety"},
            std::string_view{"--wad-path"},
            std::string_view{"--texture-file"},
            std::string_view{"--raw-wad"},
            std::string_view{"--raw-texture"},
            std::string_view{"--dump-textures"},
            std::string_view{"--extract-wad"},
            std::string_view{"--ignore-missing-texture"},
            std::string_view{"--force-texture"},
            std::string_view{"--trust-worldspawn-path"},
            std::string_view{"--decode-palette-file"},
            std::string_view{"--upload-textures"},
            std::string_view{"--render-map"},
        };
        for (const auto option : forbidden) {
            CAPTURE(option);
            const std::array arguments{option};
            const auto result = parse_command_line(arguments);
            CHECK_FALSE(result);
            CHECK(result.error.find("Unknown command-line argument") !=
                  std::string::npos);
        }
    }

    SECTION("M4.4 unsafe spatial entity and gameplay options remain unavailable")
    {
        constexpr std::array forbidden{
            std::string_view{"--entity-file"},
            std::string_view{"--raw-pvs"},
            std::string_view{"--force-visible"},
            std::string_view{"--ignore-pvs-error"},
            std::string_view{"--move-door"},
            std::string_view{"--animate-brush"},
            std::string_view{"--set-entity-origin"},
            std::string_view{"--set-entity-angles"},
            std::string_view{"--render-translucent-entities"},
            std::string_view{"--enable-collision"},
            std::string_view{"--spawn-player"},
            std::string_view{"--send-usercmd"},
            std::string_view{"--load-native-bsp-path"},
        };
        for (const auto option : forbidden) {
            CAPTURE(option);
            const std::array arguments{option};
            const auto result = parse_command_line(arguments);
            CHECK_FALSE(result);
            CHECK(result.error.find("Unknown command-line argument") !=
                  std::string::npos);
        }
    }

    SECTION("missing value")
    {
        const std::array arguments{std::string_view{"--game"}};
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("Missing value") != std::string::npos);
    }

    SECTION("empty value")
    {
        const std::array arguments{std::string_view{"--basedir"}, std::string_view{}};
        const auto result = parse_command_line(arguments);

        CHECK_FALSE(result);
        CHECK(result.error.find("Empty value") != std::string::npos);
    }
}

TEST_CASE("Command line help documents user-facing options", "[core][command-line]")
{
    const auto help = hlclient::core::command_line_help();

    CHECK(help.find("--basedir") != std::string_view::npos);
    CHECK(help.find("--game") != std::string_view::npos);
    CHECK(help.find("--connect") != std::string_view::npos);
    CHECK(help.find("+connect") != std::string_view::npos);
    CHECK(help.find("--net-trace") != std::string_view::npos);
    CHECK(help.find("--stop-after") != std::string_view::npos);
    CHECK(help.find("connect-response") != std::string_view::npos);
    CHECK(help.find("netchan-bootstrap") != std::string_view::npos);
    CHECK(help.find("signon-boundary") != std::string_view::npos);
    CHECK(help.find("pre-resource") != std::string_view::npos);
    CHECK(help.find("delta-schemas") != std::string_view::npos);
    CHECK(help.find("movevars") != std::string_view::npos);
    CHECK(help.find("user-info") != std::string_view::npos);
    CHECK(help.find("resource-list-boundary") != std::string_view::npos);
    CHECK(help.find("resource-list") != std::string_view::npos);
    CHECK(help.find("resource-response-boundary") != std::string_view::npos);
    CHECK(help.find("server-baselines") != std::string_view::npos);
    CHECK(help.find("entity-snapshot") != std::string_view::npos);
    CHECK(help.find("live-runtime-state") != std::string_view::npos);
    CHECK(help.find("live-usercmd-check") != std::string_view::npos);
    CHECK(help.find("precache-manifest") != std::string_view::npos);
    CHECK(help.find("asset-dispatch") != std::string_view::npos);
    CHECK(help.find("world-geometry") != std::string_view::npos);
    CHECK(help.find("collision-world") != std::string_view::npos);
    CHECK(help.find("Collision-world builds an immutable CPU collision package") !=
          std::string_view::npos);
    CHECK(help.find("SDL, OpenGL, renderer, movement, or prediction work") !=
          std::string_view::npos);
    CHECK(help.find("world-textures") != std::string_view::npos);
    CHECK(help.find("world-render-package") != std::string_view::npos);
    CHECK(help.find("entity-visual-scene") != std::string_view::npos);
    CHECK(help.find("--view-world") != std::string_view::npos);
    CHECK(help.find("--view-entity-snapshot") != std::string_view::npos);
    CHECK(help.find("typed stock visual-evidence boundary") !=
          std::string_view::npos);
    CHECK(help.find("stock visual-field and model-index mapping") !=
          std::string_view::npos);
    CHECK(help.find("securely opens the") != std::string_view::npos);
    CHECK(help.find("valid BSP v30") != std::string_view::npos);
    CHECK(help.find("before renderer work") != std::string_view::npos);
    CHECK(help.find("metadata-only manifest") != std::string_view::npos);
    CHECK(help.find("does not") != std::string_view::npos);
    CHECK(help.find("stop before parsing opcode-43 body") != std::string_view::npos);
    CHECK(help.find("required client response") !=
          std::string_view::npos);
    CHECK(help.find("provider-required") != std::string_view::npos);
    CHECK(help.find("--auth-provider") != std::string_view::npos);
    CHECK(help.find("file") != std::string_view::npos);
    CHECK(help.find("steam") != std::string_view::npos);
    CHECK(help.find("--auth-material-file") != std::string_view::npos);
    CHECK(help.find("--steam-api-runtime") != std::string_view::npos);
    CHECK(help.find("--resource-consistency-provider") !=
          std::string_view::npos);
    CHECK(help.find("Explicit read-only response provider") !=
          std::string_view::npos);
    CHECK(help.find("--name") != std::string_view::npos);
    CHECK(help.find("--model") != std::string_view::npos);
    CHECK(help.find("--renderer") != std::string_view::npos);
    CHECK(help.find("--runtime-replay-fixture") != std::string_view::npos);
    CHECK(help.find("--runtime-replay-record-budget") !=
          std::string_view::npos);
    CHECK(help.find("--runtime-replay-byte-budget") !=
          std::string_view::npos);
    CHECK(help.find("--runtime-replay-visuals") != std::string_view::npos);
    CHECK(help.find("visual-entities") != std::string_view::npos);
    CHECK(help.find("diagnostic") != std::string_view::npos);
}

TEST_CASE("Command line rejects renderer-native asset escape hatches",
    "[core][command-line][security]")
{
    constexpr std::array prohibited{
        std::string_view{"--shader-file"},
        std::string_view{"--vertex-shader"},
        std::string_view{"--fragment-shader"},
        std::string_view{"--texture-path"},
        std::string_view{"--lightmap-path"},
        std::string_view{"--raw-render-package"},
        std::string_view{"--load-gpu-resource"},
        std::string_view{"--ignore-lightmap-error"},
        std::string_view{"--force-white-textures"},
        std::string_view{"--render-native-path"},
        std::string_view{"--spawn-camera"},
        std::string_view{"--enable-pvs"},
        std::string_view{"--render-submodels"},
        std::string_view{"--raw-entity"},
        std::string_view{"--entity-state-file"},
        std::string_view{"--snapshot-file"},
        std::string_view{"--inject-modelindex"},
        std::string_view{"--force-model-slot"},
        std::string_view{"--native-model-path"},
        std::string_view{"--native-sprite-path"},
        std::string_view{"--execute-event"},
        std::string_view{"--live-entity-hack"},
        std::string_view{"--ignore-projection-evidence"},
        std::string_view{"--force-additive"},
        std::string_view{"--force-index-alpha"},
        std::string_view{"--send-usercmd"},
    };

    for (const auto option : prohibited) {
        const std::array arguments{option};
        const auto result = parse_command_line(arguments);
        CHECK_FALSE(result);
        CHECK(result.error.find("Unknown command-line argument") !=
              std::string::npos);
    }
}

TEST_CASE("Command line selects the application-owned offline runtime replay",
          "[core][command-line][application-runtime-replay]")
{
    SECTION("default mode has no replay source") {
        const std::array<std::string_view, 2> arguments{
            "--renderer", "null"};
        const auto parsed = parse_command_line(arguments);
        REQUIRE(parsed);
        CHECK_FALSE(parsed.options->runtime_replay_fixture);
        CHECK_FALSE(parsed.options->runtime_replay_capture);
    }

    SECTION("explicit fixture and budgets are bounded") {
        const std::array<std::string_view, 8> arguments{
            "--renderer", "null",
            "--runtime-replay-fixture", "basic-mixed",
            "--runtime-replay-record-budget", "7",
            "--runtime-replay-byte-budget", "4096"};
        const auto parsed = parse_command_line(arguments);
        REQUIRE(parsed);
        CHECK(parsed.options->runtime_replay_fixture ==
              hlclient::core::RuntimeReplayFixtureOption::basic_mixed);
        CHECK(parsed.options->runtime_replay_record_budget == 7U);
        CHECK(parsed.options->runtime_replay_byte_budget == 4'096U);
        CHECK(parsed.options->renderer == RendererBackend::null);
        CHECK_FALSE(parsed.options->connect_endpoint);
        CHECK_FALSE(hlclient::core::requires_local_resource_consistency_preparation(
            *parsed.options));
    }

    SECTION("negative fixture is a separate explicit selection") {
        const std::array<std::string_view, 4> arguments{
            "--renderer", "null",
            "--runtime-replay-fixture", "missing-entity-base"};
        const auto parsed = parse_command_line(arguments);
        REQUIRE(parsed);
        CHECK(parsed.options->runtime_replay_fixture ==
              hlclient::core::RuntimeReplayFixtureOption::missing_entity_base);
    }

    SECTION("functional capture path selects the same offline application mode") {
        const std::array<std::string_view, 6> arguments{
            "--renderer", "null",
            "--runtime-replay-capture", "D:\\private\\run-id",
            "--runtime-replay-record-budget", "3"};
        const auto parsed = parse_command_line(arguments);
        REQUIRE(parsed);
        CHECK(parsed.options->runtime_replay_capture ==
              "D:\\private\\run-id");
        CHECK_FALSE(parsed.options->runtime_replay_fixture);
        CHECK(parsed.options->runtime_replay_record_budget == 3U);
        CHECK_FALSE(parsed.options->connect_endpoint);
        CHECK_FALSE(hlclient::core::requires_local_resource_consistency_preparation(
            *parsed.options));
    }

    SECTION("diagnostic visuals allow the normal OpenGL application backend") {
        const std::array<std::string_view, 4> arguments{
            "--runtime-replay-fixture", "visual-entities",
            "--runtime-replay-visuals", "diagnostic"};
        const auto parsed = parse_command_line(arguments);
        REQUIRE(parsed);
        CHECK(parsed.options->renderer == RendererBackend::opengl);
        CHECK(parsed.options->runtime_replay_fixture ==
              hlclient::core::RuntimeReplayFixtureOption::visual_entities);
        CHECK(parsed.options->runtime_replay_visuals ==
              hlclient::core::RuntimeReplayVisualOption::diagnostic);
        CHECK_FALSE(parsed.options->connect_endpoint);
        CHECK_FALSE(hlclient::core::requires_local_resource_consistency_preparation(
            *parsed.options));
    }

    SECTION("diagnostic visuals use the identical projection with NullRenderer") {
        const std::array<std::string_view, 6> arguments{
            "--renderer", "null",
            "--runtime-replay-fixture", "visual-entities",
            "--runtime-replay-visuals", "diagnostic"};
        const auto parsed = parse_command_line(arguments);
        REQUIRE(parsed);
        CHECK(parsed.options->renderer == RendererBackend::null);
        CHECK(parsed.options->runtime_replay_visuals ==
              hlclient::core::RuntimeReplayVisualOption::diagnostic);
    }
}

TEST_CASE("Offline runtime replay options reject ambiguous startup modes",
          "[core][command-line][application-runtime-replay][security]")
{
    SECTION("NullRenderer is mandatory") {
        const std::array<std::string_view, 2> arguments{
            "--runtime-replay-fixture", "basic-mixed"};
        const auto parsed = parse_command_line(arguments);
        CHECK_FALSE(parsed);
        CHECK(parsed.error.find("requires --renderer null") !=
              std::string::npos);
    }

    SECTION("connect is rejected before application startup") {
        const std::array<std::string_view, 6> arguments{
            "--renderer", "null", "--runtime-replay-fixture", "basic-mixed",
            "--connect", "127.0.0.1:27015"};
        const auto parsed = parse_command_line(arguments);
        CHECK_FALSE(parsed);
        CHECK(parsed.error.find("incompatible") != std::string::npos);
    }

    SECTION("capture input rejects fixture, live connect, and OpenGL") {
        const std::array<std::string_view, 6> both_sources{
            "--renderer", "null", "--runtime-replay-fixture", "basic-mixed",
            "--runtime-replay-capture", "run"};
        CHECK_FALSE(parse_command_line(both_sources));

        const std::array<std::string_view, 6> live_capture{
            "--renderer", "null", "--runtime-replay-capture", "run",
            "--connect", "127.0.0.1:27015"};
        CHECK_FALSE(parse_command_line(live_capture));

        const std::array<std::string_view, 2> graphical_capture{
            "--runtime-replay-capture", "run"};
        CHECK_FALSE(parse_command_line(graphical_capture));
    }

    SECTION("visual replay rejects connect before any side effects") {
        const std::array<std::string_view, 8> arguments{
            "--renderer", "opengl",
            "--runtime-replay-fixture", "visual-entities",
            "--runtime-replay-visuals", "diagnostic",
            "--connect", "127.0.0.1:27015"};
        const auto parsed = parse_command_line(arguments);
        CHECK_FALSE(parsed);
        CHECK(parsed.error.find("incompatible") != std::string::npos);
    }

    SECTION("visual fixture and visual mode require one another") {
        const std::array<std::string_view, 4> fixture_only{
            "--renderer", "null",
            "--runtime-replay-fixture", "visual-entities"};
        CHECK_FALSE(parse_command_line(fixture_only));

        const std::array<std::string_view, 6> wrong_fixture{
            "--renderer", "null",
            "--runtime-replay-fixture", "basic-mixed",
            "--runtime-replay-visuals", "diagnostic"};
        CHECK_FALSE(parse_command_line(wrong_fixture));

        const std::array<std::string_view, 2> visuals_only{
            "--runtime-replay-visuals", "diagnostic"};
        CHECK_FALSE(parse_command_line(visuals_only));
    }

    SECTION("asset path is rejected") {
        const std::array<std::string_view, 6> arguments{
            "--renderer", "null", "--runtime-replay-fixture", "basic-mixed",
            "--basedir", "ignored"};
        const auto parsed = parse_command_line(arguments);
        CHECK_FALSE(parsed);
        CHECK(parsed.error.find("incompatible") != std::string::npos);
    }

    SECTION("budgets without a replay source are rejected") {
        const std::array<std::string_view, 4> arguments{
            "--renderer", "null", "--runtime-replay-record-budget", "1"};
        const auto parsed = parse_command_line(arguments);
        CHECK_FALSE(parsed);
        CHECK(parsed.error.find("require --runtime-replay-fixture") !=
              std::string::npos);
    }

    for (const auto invalid : {"0", "1025", "-1", "text"}) {
        CAPTURE(invalid);
        const std::array<std::string_view, 6> arguments{
            "--renderer", "null", "--runtime-replay-fixture", "basic-mixed",
            "--runtime-replay-record-budget", invalid};
        CHECK_FALSE(parse_command_line(arguments));
    }
    for (const auto invalid : {"0", "16777217", "-1", "text"}) {
        CAPTURE(invalid);
        const std::array<std::string_view, 6> arguments{
            "--renderer", "null", "--runtime-replay-fixture", "basic-mixed",
            "--runtime-replay-byte-budget", invalid};
        CHECK_FALSE(parse_command_line(arguments));
    }
}

} // namespace
