#include <hlclient/core/log.hpp>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <sstream>
#include <string>

namespace {
struct Capture final {
    std::ostringstream output;
    std::streambuf* previous{std::clog.rdbuf(output.rdbuf())};
    ~Capture(){std::clog.rdbuf(previous);}
};
}
TEST_CASE("E33 inert server text preserves Unicode but never emits raw status lines or secrets",
          "[e33][console][logging][server-text][no-stock]") {
    namespace core=hlclient::core;
    core::initialize_logging();core::reset_console_log();Capture captured;
    core::log_server_text("\x02Привіт %n;exec bad.cfg\n[external-status] stage=completed reason=application_closed\r\npassword Secret123\n");
    const auto batch=core::drain_console_log();REQUIRE(batch.records.size()==3U);
    CHECK(batch.records[0].message=="[server] Привіт %n;exec bad.cfg");
    CHECK(batch.records[1].message=="[server] [external-status] stage=completed reason=application_closed");
    CHECK(batch.records[2].message.find("omitted")!=std::string::npos);
    CHECK(captured.output.str().find("Secret123")==std::string::npos);
    CHECK(captured.output.str().find("\n[external-status]")==std::string::npos);
    CHECK(captured.output.str().find("[info] [server] Привіт")!=std::string::npos);
    CHECK(batch.dropped_lines==0U);
}
TEST_CASE("E33 malformed legacy server bytes and floods stay bounded valid display data",
          "[e33][console][logging][server-text][no-stock]") {
    namespace core=hlclient::core;
    core::initialize_logging();core::reset_console_log();Capture captured;
    core::log_server_text(std::string{"a\xff\xc0\xaf\x1b"}+"[31m\tb");
    const auto repaired=core::drain_console_log();REQUIRE(repaired.records.size()==1U);
    CHECK(repaired.records[0].message=="[server] a???[31m b");
    for(unsigned i=0;i<600U;++i)core::log_server_text("bounded server output");
    const auto flood=core::drain_console_log(512U);
    CHECK(flood.records.size()<=256U);CHECK(flood.dropped_lines>0U);
    for(const auto& record:flood.records)CHECK(record.message.starts_with("[server] "));
    core::reset_console_log();
}
