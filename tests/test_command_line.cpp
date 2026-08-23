#include <hlclient/core/command_line.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

namespace {

using hlclient::core::parse_command_line;
using hlclient::core::RendererBackend;

TEST_CASE("Command line parser supplies safe defaults", "[core][command-line]")
{
    const std::array<std::string_view, 0> arguments{};

    const auto result = parse_command_line(arguments);

    REQUIRE(result);
    CHECK_FALSE(result.options->show_help);
    CHECK_FALSE(result.options->show_version);
    CHECK_FALSE(result.options->net_trace);
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
            std::string_view{"steam"},
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
    CHECK(help.find("stop before parsing opcode-43 body") != std::string_view::npos);
    CHECK(help.find("required client response") !=
          std::string_view::npos);
    CHECK(help.find("provider-required") != std::string_view::npos);
    CHECK(help.find("--auth-provider") != std::string_view::npos);
    CHECK(help.find("file") != std::string_view::npos);
    CHECK(help.find("--auth-material-file") != std::string_view::npos);
    CHECK(help.find("--resource-consistency-provider") !=
          std::string_view::npos);
    CHECK(help.find("Explicit read-only response provider") !=
          std::string_view::npos);
    CHECK(help.find("--name") != std::string_view::npos);
    CHECK(help.find("--model") != std::string_view::npos);
    CHECK(help.find("--renderer") != std::string_view::npos);
}

} // namespace
