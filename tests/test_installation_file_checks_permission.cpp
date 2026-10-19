#include <hlclient/core/external_launch_config.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {
namespace core=hlclient::core;
constexpr std::string_view endpoint="45.83.244.193:27020";
constexpr std::string_view installation="D:/DEV/HLCLIENT-RESEARCH/Half-Life";
constexpr std::string_view game="valve";

std::string permission_json(const std::string_view target=endpoint,
    const std::string_view root=installation,const std::string_view directory=game)
{
    return "{\"schema\":"+core::external_json_string(core::kInstallationFileChecksSchema)+
        ",\"endpoint\":"+core::external_json_string(target)+
        ",\"asset_root\":"+core::external_json_string(root)+
        ",\"game_directory\":"+core::external_json_string(directory)+"}";
}
bool approved(const std::string_view json)
{
    return core::approved_installation_file_checks(json,endpoint,installation,game);
}
}

TEST_CASE("Installation check grants bind one normalized endpoint root and game",
    "[installation-checks][permission]")
{
    const auto json=permission_json();
    REQUIRE(approved(json));
    CHECK(core::approved_installation_file_checks(json,
        " 045.083.244.193:027020 ","d:\\dev\\hlclient-research\\HALF-LIFE\\","VALVE"));
    CHECK(approved(permission_json("connect 045.083.244.193:27020",
        "d:\\DEV/HLCLIENT-RESEARCH\\Half-Life/","VaLvE")));

    // The decoder validates a local grant, not a compiled-in trusted server list.
    const auto other=permission_json("192.168.1.7","E:/Games/Half Life","custom-mod");
    CHECK(core::approved_installation_file_checks(other,"192.168.1.7:27015",
        "e:\\games\\Half Life","CUSTOM-MOD"));
    CHECK_FALSE(approved(other));

    for(const auto target:{"45.83.244.193:27015","45.83.244.194:27020","192.168.0.104:27016"}) {
        INFO(target);
        CHECK_FALSE(core::approved_installation_file_checks(json,target,installation,game));
        CHECK_FALSE(approved(permission_json(target)));
    }
    for(const auto root:{"D:/DEV/HLCLIENT-RESEARCH","D:/DEV/HLCLIENT-RESEARCH/Half-Life/valve",
        "D:/DEV/HLCLIENT-RESEARCH/Half-Life-backup","E:/DEV/HLCLIENT-RESEARCH/Half-Life"}) {
        INFO(root);
        CHECK_FALSE(core::approved_installation_file_checks(json,endpoint,root,game));
        CHECK_FALSE(approved(permission_json(endpoint,root)));
    }
    for(const auto directory:{"gearbox","valve_backup","valve2"}) {
        INFO(directory);
        CHECK_FALSE(core::approved_installation_file_checks(json,endpoint,installation,directory));
        CHECK_FALSE(approved(permission_json(endpoint,installation,directory)));
    }
}

TEST_CASE("Installation check grants reject malformed ambiguous or extended JSON",
    "[installation-checks][permission]")
{
    const auto json=permission_json();
    const auto replace=[&](const std::string_view before,const std::string_view after) {
        auto changed=json;
        const auto at=changed.find(before);
        REQUIRE(at!=std::string::npos);
        changed.replace(at,before.size(),after);
        return changed;
    };
    for(const auto malformed:{"","{}","[]","null","true","{","{\"schema\":\"x\",}",
        "{\"schema\":\"\\uD800\"}","{\"schema\":\"\\q\"}"}) {
        INFO(malformed);
        CHECK_FALSE(approved(malformed));
    }
    CHECK_FALSE(approved(replace(core::kInstallationFileChecksSchema,"hlclient.installation-file-checks.v2")));
    CHECK_FALSE(approved(replace(core::kInstallationFileChecksSchema,"HLCLIENT.installation-file-checks.v1")));
    CHECK_FALSE(approved(replace("\"game_directory\":\"valve\"","\"unknown\":\"valve\"")));
    CHECK_FALSE(approved(replace(",\"game_directory\":\"valve\"","")));
    CHECK_FALSE(approved(replace("\"game_directory\":\"valve\"",
        "\"game_directory\":\"valve\",\"unknown\":\"value\"")));
    CHECK_FALSE(approved(replace("\"game_directory\":\"valve\"",
        "\"game_directory\":\"valve\",\"game_directory\":\"valve\"")));
    CHECK_FALSE(approved(replace("\"game_directory\":\"valve\"",
        "\"game_directory\":\"valve\",\"game_\\u0064irectory\":\"valve\"")));
    for(const auto field:{"schema","endpoint","asset_root","game_directory"}) {
        const std::string prefix="\""+std::string{field}+"\":";
        const auto begin=json.find(prefix)+prefix.size();
        const auto end=json.find('"',begin+1);
        REQUIRE(end!=std::string::npos);
        for(const auto value:{"false","true","1","null","[]","{}"}) {
            INFO(field);
            INFO(value);
            auto changed=json;
            changed.replace(begin,end-begin+1,value);
            CHECK_FALSE(approved(changed));
        }
    }
    CHECK_FALSE(approved(json+"more"));
    CHECK_FALSE(approved(json+json));
    CHECK_FALSE(approved(json.substr(0,json.size()-1)));
    auto nul=json;
    nul.push_back('\0');
    CHECK_FALSE(approved(nul));
    auto invalid_utf8=json;
    invalid_utf8.insert(invalid_utf8.size()-2,1,static_cast<char>(0xff));
    CHECK_FALSE(approved(invalid_utf8));
    CHECK_FALSE(approved(replace("valve","valve\\u0000")));
    CHECK_FALSE(approved(replace("valve","valve\\n")));
    CHECK_FALSE(approved(replace("Half-Life","\\u002e\\u002e")));
    CHECK(approved(replace("D:/DEV","\\u0044:\\/DEV")));

    auto bounded=json;
    bounded.append(core::kInstallationFileChecksMaximumBytes-bounded.size(),' ');
    CHECK(approved(bounded));
    bounded+=' ';
    CHECK_FALSE(approved(bounded));
}

TEST_CASE("Installation check grants reject unsafe Windows root aliases on both sides",
    "[installation-checks][permission]")
{
    std::vector<std::string> unsafe{
        "", "D:", "D:/", "D:\\", "/DEV/Half-Life", "DEV/Half-Life", "D:DEV/Half-Life",
        "//server/share/Half-Life", "\\\\server\\share\\Half-Life",
        "\\\\?\\D:\\DEV\\Half-Life", "\\\\.\\D:\\DEV\\Half-Life",
        "\\??\\D:\\DEV\\Half-Life", "D:/DEV/../Half-Life", "D:/DEV/./Half-Life",
        "D:/DEV\\..\\Half-Life", "D:/DEV//Half-Life", "D:/DEV/Half-Life//",
        "D://DEV/Half-Life", "D:/DEV/Half-Life:stream", "D:/DEV/Half-Life::$DATA",
        "D:/DEV/Half-Life.", "D:/DEV/Half-Life ", "D:/ DEV/Half-Life",
        "D:/DEV /Half-Life", "D:/DEV./Half-Life", "D:/DEV/Half*Life",
        "D:/DEV/Half?Life", "D:/DEV/Half|Life", "D:/DEV/Half<Life", "D:/DEV/Half>Life",
        "D:/DEV/Half\"Life", "D:/CON/Half-Life", "D:/nul.txt/Half-Life",
        "D:/AUX .txt/Half-Life", "D:/PRN/Half-Life", "D:/COM1/Half-Life",
        "D:/lpt9.txt/Half-Life", "D:/CONIN$/Half-Life", "D:/CONOUT$/Half-Life",
        "D:/CLOCK$/Half-Life", "D:/Ігри/Half-Life", "D:/DEV/Half\nLife",
        "D:/DEV/"+std::string(256,'x'), std::string(4097,'x')};
    unsafe.emplace_back("D:/DEV/Half-Life\0hidden",23);
    unsafe.push_back("D:/DEV/Half"+std::string(1,static_cast<char>(127))+"Life");
    for(const auto& root:unsafe) {
        INFO(core::external_json_string(root));
        // Matching malformed strings must not turn into a grant.
        CHECK_FALSE(core::approved_installation_file_checks(permission_json(endpoint,root),
            endpoint,root,game));
        CHECK_FALSE(core::approved_installation_file_checks(permission_json(),endpoint,root,game));
        CHECK_FALSE(approved(permission_json(endpoint,root)));
    }
}

TEST_CASE("Installation check grants require a safe single game component and literal target",
    "[installation-checks][permission]")
{
    std::vector<std::string> directories{"", ".", "..", "../valve", "valve/..", "valve/custom",
        "valve\\custom", "D:valve", "valve:stream", "valve.", "valve ", " valve", "val*ve",
        "val?ve", "val|ve", "val<ve", "val>ve", "val\"ve", "val\nve", "CON", "prn.txt",
        "aux", "NUL", "COM9", "lpt1.cfg", "CONIN$", "CONOUT$", "CLOCK$", "vаlve",
        std::string(256,'v')};
    directories.emplace_back("valve\0hidden",12);
    for(const auto& directory:directories) {
        INFO(core::external_json_string(directory));
        CHECK_FALSE(core::approved_installation_file_checks(permission_json(endpoint,installation,directory),
            endpoint,installation,directory));
        CHECK_FALSE(core::approved_installation_file_checks(permission_json(),endpoint,installation,directory));
        CHECK_FALSE(approved(permission_json(endpoint,installation,directory)));
    }
    std::vector<std::string> targets{"", "localhost:27020", "45.83.244.193:0", "45.83.244.193:65536",
        "45.83.244.193:27020;quit", "45.83.244.193:27020\n", "45.83.244.193:27020 more",
        "0.0.0.0:27020", "127.0.0.1:27020", "255.255.255.255:27020", "239.0.0.1:27020"};
    targets.emplace_back("45.83.244.193:27020\0hidden",25);
    for(const auto& target:targets) {
        INFO(core::external_json_string(target));
        CHECK_FALSE(core::approved_installation_file_checks(permission_json(target),target,installation,game));
        CHECK_FALSE(core::approved_installation_file_checks(permission_json(),target,installation,game));
        CHECK_FALSE(approved(permission_json(target)));
    }
}
