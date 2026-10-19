#include <hlclient/console/console.hpp>
#include <algorithm>
#include <cmath>

namespace hlclient::console {
namespace {
std::size_t next_utf8(const std::string_view s, std::size_t at) noexcept
{
    if (at < s.size()) ++at;
    while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xc0U) == 0x80U) ++at;
    return at;
}
std::size_t previous(const std::string_view s, std::size_t at) noexcept
{
    if (at > 0) --at;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xc0U) == 0x80U) --at;
    return at;
}
template<class Visitor>
void visit_wrapped_rows(const std::deque<std::string>& lines,const std::size_t columns,Visitor&& visit)
{
    std::size_t row{};
    for(std::size_t line_index=0;line_index<lines.size();++line_index) {
        const auto& line=lines[line_index];std::size_t begin{},at{},characters{};
        while(at<line.size()) {
            at=next_utf8(line,at);
            if(++characters==columns) {
                visit(line_index,begin,at,row++);begin=at;characters=0;
            }
        }
        if(begin<line.size()||line.empty())visit(line_index,begin,line.size(),row++);
    }
}
}
bool valid_utf8(const std::string_view text) noexcept
{
    for (std::size_t i = 0; i < text.size();) {
        const auto a = static_cast<unsigned char>(text[i++]);
        if (a < 0x80U) continue;
        unsigned count{}, code{}, minimum{};
        if (a >= 0xc2U && a <= 0xdfU) { count=1; code=a&31U; minimum=0x80U; }
        else if (a >= 0xe0U && a <= 0xefU) { count=2; code=a&15U; minimum=0x800U; }
        else if (a >= 0xf0U && a <= 0xf4U) { count=3; code=a&7U; minimum=0x10000U; }
        else return false;
        while (count-- > 0) {
            if (i == text.size()) return false;
            const auto b = static_cast<unsigned char>(text[i++]);
            if ((b & 0xc0U) != 0x80U) return false;
            code = (code << 6U) | (b & 63U);
        }
        if (code < minimum || code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)) return false;
    }
    return true;
}
std::string utf8_prefix(const std::string_view text, std::size_t bytes)
{
    bytes = std::min(bytes, text.size());
    while (bytes > 0 && bytes < text.size() && (static_cast<unsigned char>(text[bytes]) & 0xc0U) == 0x80U) --bytes;
    const auto prefix = text.substr(0, bytes);
    return valid_utf8(prefix) ? std::string{prefix} : std::string{};
}
std::string quote_argument(const std::string_view text)
{
    std::string out{"\""};
    for (const char c : text) {
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) >= 32U) out += c;
    }
    return out + '"';
}
ConsoleModel::ConsoleModel(ConsoleLimits limits) : limits_{limits}
{
    limits_.scrollback_lines = std::clamp(limits_.scrollback_lines, std::size_t{1}, std::size_t{8192});
    limits_.scrollback_bytes = std::clamp(limits_.scrollback_bytes, std::size_t{4}, std::size_t{1024*1024});
    limits_.editor_bytes = std::clamp(limits_.editor_bytes, std::size_t{4}, std::size_t{16384});
    limits_.history_lines = std::clamp(limits_.history_lines, std::size_t{1}, std::size_t{512});
    limits_.paste_lines = std::clamp(limits_.paste_lines, std::size_t{1}, std::size_t{64});
}
void ConsoleModel::append(const std::string_view input)
{
    const auto text = valid_utf8(input) ? input : std::string_view{"[invalid UTF-8 output]"};
    std::size_t begin = 0;
    do {
        const auto end = text.find_first_of("\r\n", begin);
        auto part = text.substr(begin, end == text.npos ? text.npos : end - begin);
        if (!part.empty() && part.back() == '\r') part.remove_suffix(1);
        std::string clean;
        for (const char c : part) if (static_cast<unsigned char>(c) >= 32U || c == '\t') clean += c;
        clean = utf8_prefix(clean, limits_.scrollback_bytes);
        line_bytes_ += clean.size();
        lines_.push_back(std::move(clean));
        while (lines_.size() > limits_.scrollback_lines || line_bytes_ > limits_.scrollback_bytes) {
            line_bytes_ -= lines_.front().size(); lines_.pop_front(); ++dropped_; ++first_line_id_;
        }
        if (end == text.npos) break;
        begin = end + 1;
        if(text[end]=='\r'&&begin<text.size()&&text[begin]=='\n')++begin;
    } while (begin < text.size());
}
void ConsoleModel::clear() noexcept { first_line_id_+=lines_.size();lines_.clear();line_bytes_=0;scroll_anchor_.reset(); }
const std::deque<std::string>& ConsoleModel::lines() const noexcept { return lines_; }
std::size_t ConsoleModel::dropped_lines() const noexcept { return dropped_; }
ConsoleModel::ScrollWindow ConsoleModel::scroll_window() const noexcept
{
    ScrollWindow window;bool anchor_found{};
    visit_wrapped_rows(lines_,viewport_columns_,[&](std::size_t line,std::size_t,std::size_t end,std::size_t row) {
        window.total=row+1;
        const auto identity=first_line_id_+line;
        if(scroll_anchor_&&!anchor_found&&(identity>scroll_anchor_->line||
            (identity==scroll_anchor_->line&&end>=scroll_anchor_->byte_end))) {
            window.end=row+1;anchor_found=true;
        }
    });
    if(!scroll_anchor_||!anchor_found)window.end=window.total;
    // The oldest legal endpoint still fills the viewport when enough rows
    // exist. PageUp/wheel cannot leave only the first history record visible.
    window.end=std::clamp(window.end,std::min(viewport_rows_,window.total),window.total);
    window.begin=window.end-std::min(viewport_rows_,window.end);
    return window;
}
std::size_t ConsoleModel::scroll_offset() const noexcept { const auto view=scroll_window();return view.total-view.end; }
void ConsoleModel::scroll(const int count) noexcept
{
    const auto view=scroll_window();
    const auto end=static_cast<std::size_t>(std::clamp(static_cast<std::int64_t>(view.end)-count,
        static_cast<std::int64_t>(std::min(viewport_rows_,view.total)),static_cast<std::int64_t>(view.total)));
    if(end==view.total){bottom();return;}
    visit_wrapped_rows(lines_,viewport_columns_,[&](std::size_t line,std::size_t,std::size_t byte_end,std::size_t row) {
        if(row+1==end)scroll_anchor_=ScrollAnchor{first_line_id_+line,byte_end};
    });
}
void ConsoleModel::bottom() noexcept { scroll_anchor_.reset(); }
std::vector<std::string> ConsoleModel::visible_lines(std::size_t columns, std::size_t rows) const
{
    columns=std::clamp(columns,std::size_t{1},std::size_t{4096});
    rows=std::min(rows,std::size_t{1024});
    if(!rows)return {};
    viewport_columns_=columns;viewport_rows_=rows;
    const auto view=scroll_window();std::vector<std::string> result;result.reserve(view.end-view.begin);
    visit_wrapped_rows(lines_,columns,[&](std::size_t line,std::size_t begin,std::size_t end,std::size_t row) {
        if(row>=view.begin&&row<view.end)result.push_back(lines_[line].substr(begin,end-begin));
    });
    return result;
}
const std::string& ConsoleModel::editor() const noexcept { return editor_; }
std::size_t ConsoleModel::cursor() const noexcept { return cursor_; }
bool ConsoleModel::selected_all() const noexcept { return select_all_; }
bool ConsoleModel::set_editor(const std::string_view text)
{
    if (!valid_utf8(text) || text.size()>limits_.editor_bytes || text.find('\0')!=text.npos) return false;
    editor_=text; cursor_=editor_.size(); select_all_=false; return true;
}
bool ConsoleModel::insert(const std::string_view text)
{
    if (!valid_utf8(text) || text.find('\0')!=text.npos ||
        static_cast<std::size_t>(std::count(text.begin(),text.end(),'\n'))>=limits_.paste_lines) return false;
    for (const auto c:text) if (static_cast<unsigned char>(c)<32U && c!='\n' && c!='\r' && c!='\t') return false;
    const auto retained=select_all_?0:editor_.size();
    if (text.size()>limits_.editor_bytes-retained) return false;
    if (select_all_) { editor_.clear(); cursor_=0; select_all_=false; }
    editor_.insert(cursor_,text); cursor_+=text.size(); return true;
}
void ConsoleModel::left() noexcept { cursor_=select_all_?0:previous(editor_,cursor_); select_all_=false; }
void ConsoleModel::right() noexcept { cursor_=select_all_?editor_.size():next_utf8(editor_,cursor_); select_all_=false; }
void ConsoleModel::home() noexcept { cursor_=0; select_all_=false; }
void ConsoleModel::end() noexcept { cursor_=editor_.size(); select_all_=false; }
void ConsoleModel::backspace()
{
    if (select_all_) { editor_.clear(); cursor_=0; select_all_=false; }
    else { const auto at=previous(editor_,cursor_); editor_.erase(at,cursor_-at); cursor_=at; }
}
void ConsoleModel::erase_forward()
{
    if (select_all_) { editor_.clear(); cursor_=0; select_all_=false; }
    else editor_.erase(cursor_,next_utf8(editor_,cursor_)-cursor_);
}
void ConsoleModel::select_all() noexcept { select_all_=true; }
std::string ConsoleModel::copy_selection() const { return select_all_?editor_:std::string{}; }
void ConsoleModel::history_previous()
{
    if(history_.empty() || history_offset_==history_.size()) return;
    if(history_offset_==0) history_draft_=editor_;
    ++history_offset_; (void)set_editor(history_[history_.size()-history_offset_]);
}
void ConsoleModel::history_next()
{
    if(history_offset_==0) return;
    --history_offset_; (void)set_editor(history_offset_==0?history_draft_:history_[history_.size()-history_offset_]);
}
std::string ConsoleModel::submit(const bool retain_history)
{
    auto result=editor_;
    if(retain_history && !result.empty() && (history_.empty() || history_.back()!=result)) {
        history_.push_back(result); while(history_.size()>limits_.history_lines) history_.pop_front();
    }
    editor_.clear(); cursor_=0; select_all_=false; history_offset_=0; history_draft_.clear(); bottom();
    return result;
}
void ConsoleModel::complete(const std::span<const std::string> candidates)
{
    if(candidates.empty()) return;
    auto prefix=candidates.front();
    for(const auto& s:candidates) { std::size_t i=0; while(i<prefix.size() && i<s.size() && prefix[i]==s[i]) ++i; prefix=utf8_prefix(prefix,i); }
    if(prefix.size()>=editor_.size()) (void)set_editor(prefix+(candidates.size()==1?" ":""));
    if(candidates.size()>1) { std::string output; for(const auto& s:candidates){if(!output.empty())output+="  ";output+=s;} append(output); }
}
void ConsoleController::advance(const double now) noexcept
{
    if(!std::isfinite(now)) return;
    if(!has_time_) { last_time_=now; has_time_=true; return; }
    if(now<last_time_) return;
    const double delta=now-last_time_; last_time_=now;
    height_=std::clamp(height_+(open_?1.0:-1.0)*delta*(0.5/0.18),0.0,0.5);
}
void ConsoleController::toggle(const double now,const bool repeated) noexcept { if(!repeated)set_open(!open_,now); }
void ConsoleController::set_open(const bool open,const double now) noexcept { advance(now); open_=open; }
bool ConsoleController::takes_input() const noexcept { return open_ || height_>0; }
bool ConsoleController::target_open() const noexcept { return open_; }
double ConsoleController::height_fraction() const noexcept { return height_; }
} // namespace hlclient::console
