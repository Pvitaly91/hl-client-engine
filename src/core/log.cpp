#include <hlclient/core/log.hpp>
#include <hlclient/core/console_text_privacy.hpp>

#include <array>
#include <atomic>
#include <iostream>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <deque>
#include <cstdint>

namespace hlclient::core {
namespace {

std::atomic<LogLevel> minimum_level{LogLevel::info};
std::mutex output_mutex;
std::mutex console_mutex;
std::deque<ConsoleLogRecord> console_records;
std::size_t console_bytes{};
std::size_t console_dropped{};
std::size_t console_window_count{};
std::size_t console_error_window_count{};
std::chrono::steady_clock::time_point console_window{};

void queue_console(const LogLevel level, const std::string_view input, const LogChannel channel)
{
    std::string text{input.substr(0, 4096)};
    // Retain complete UTF-8 code points when the diagnostic exceeds the cap.
    if(input.size()>text.size()) {
        while(!text.empty() && (static_cast<unsigned char>(input[text.size()])&0xc0U)==0x80U)text.pop_back();
    }
    if(channel!=LogChannel::game_event && contains_sensitive_console_text(input))text="[sensitive diagnostic omitted]";
    const std::scoped_lock lock{console_mutex};
    const auto now=std::chrono::steady_clock::now();
    if(now-console_window>=std::chrono::seconds{1}){
        console_window=now;console_window_count=0;console_error_window_count=0;
    }
    const bool critical=level>=LogLevel::error;
    // A burst of server notices or warnings cannot consume the first error's
    // rate, record or byte allowance. Both classes remain strictly bounded.
    auto& count=critical?console_error_window_count:console_window_count;
    if(channel!=LogChannel::game_event && count++>=(critical?64U:256U)){++console_dropped;return;}
    const std::size_t record_limit=critical?512U:480U;
    const std::size_t byte_limit=critical?256U*1024U:248U*1024U;
    if(console_records.size()>=record_limit || console_bytes>=byte_limit ||
        text.size()>byte_limit-console_bytes){++console_dropped;return;}
    console_bytes+=text.size();console_records.push_back({level,std::move(text),channel});
}

[[nodiscard]] constexpr std::string_view level_name(const LogLevel level) noexcept
{
    constexpr std::array names{
        std::string_view{"trace"},
        std::string_view{"debug"},
        std::string_view{"info"},
        std::string_view{"warning"},
        std::string_view{"error"},
        std::string_view{"fatal"},
    };
    return names[static_cast<std::size_t>(level)];
}

} // namespace

void initialize_logging(const LogLevel requested_minimum_level) noexcept
{
    minimum_level.store(requested_minimum_level, std::memory_order_relaxed);
}

void log(const LogLevel level, const std::string_view message, const LogChannel channel) noexcept
{
    if (level < minimum_level.load(std::memory_order_relaxed)) {
        return;
    }

    try {
        if(channel!=LogChannel::diagnostic || level>=LogLevel::warning)
            queue_console(level,message,channel);
        const std::scoped_lock lock{output_mutex};
        std::clog << '[' << level_name(level) << "] " <<
            (channel!=LogChannel::game_event && contains_sensitive_console_text(message) ? std::string_view{"[sensitive diagnostic omitted]"} : message) << '\n';
    } catch (...) {
        // Logging must never make shutdown or error handling fail.
    }
}

ConsoleLogBatch drain_console_log(const std::size_t maximum_records) noexcept
{
    ConsoleLogBatch batch;
    try {
        const std::scoped_lock lock{console_mutex};
        const auto count=std::min({maximum_records,console_records.size(),std::size_t{512}});
        batch.records.reserve(count);
        for(std::size_t i=0;i<count;++i){console_bytes-=console_records.front().message.size();batch.records.push_back(std::move(console_records.front()));console_records.pop_front();}
        batch.dropped_lines=console_dropped;console_dropped=0;
    }catch(...){}
    return batch;
}
namespace {
void log_display_text(const std::string_view message,const bool game_event,const LogLevel level) noexcept
{
    try {
        const auto input=message.substr(0,4096U);
        std::string line;line.reserve(256U);std::size_t lines{};
        const auto flush=[&] {
            if(line.empty() || lines>=32U){line.clear();return;}
            if(!game_event && contains_sensitive_console_text(line))line="[sensitive server text omitted]";
            log(level,std::string{game_event?"[game] ":"[server] "}+line,
                game_event?LogChannel::game_event:LogChannel::server_text);++lines;line.clear();
        };
        for(std::size_t at=0;at<input.size() && lines<32U;) {
            const auto c=static_cast<unsigned char>(input[at]);
            if(c=='\n' || c=='\r') {
                ++at;if(c=='\r' && at<input.size() && input[at]=='\n')++at;
                flush();continue;
            }
            if(c<32U || c==127U) {
                if(c=='\t')line+=' ';
                ++at;continue; // Includes HL chat color bytes and terminal ESC.
            }
            if(c<128U){line+=static_cast<char>(c);++at;}
            else {
                std::size_t count{};std::uint32_t code{},minimum{};
                if(c>=0xc2U && c<=0xdfU){count=2U;code=c&31U;minimum=0x80U;}
                else if(c>=0xe0U && c<=0xefU){count=3U;code=c&15U;minimum=0x800U;}
                else if(c>=0xf0U && c<=0xf4U){count=4U;code=c&7U;minimum=0x10000U;}
                bool valid=count && count<=input.size()-at;
                for(std::size_t i=1U;valid && i<count;++i) {
                    const auto next=static_cast<unsigned char>(input[at+i]);
                    if((next&0xc0U)!=0x80U)valid=false;else code=(code<<6U)|(next&63U);
                }
                valid=valid && code>=minimum && code<=0x10ffffU && !(code>=0xd800U && code<=0xdfffU);
                if(valid){line.append(input.substr(at,count));at+=count;}
                else {line+='?';++at;}
            }
            if(line.size()>=1024U)flush();
        }
        flush();
    }catch(...){}
}
}
void log_server_text(std::string_view message,LogLevel level) noexcept {log_display_text(message,false,level);}
void log_game_event(std::string_view message) noexcept {log_display_text(message,true,LogLevel::info);}
void reset_console_log() noexcept
{
    try {const std::scoped_lock lock{console_mutex};console_records.clear();console_bytes=0;console_dropped=0;console_window_count=0;console_error_window_count=0;console_window={};}catch(...){}
}

} // namespace hlclient::core
