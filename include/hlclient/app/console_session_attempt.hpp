#pragma once

#include <hlclient/core/command_line.hpp>
#ifdef _WIN32
#include <hlclient/platform/windows/external_launcher.hpp>
#endif

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::app {

#ifdef _WIN32
namespace native=platform::windows;
namespace console_attempt_detail {
inline std::string open_error(const std::optional<native::SecureOutputError>& error) {
    return error ? std::string{native::to_string(error->code)}+"_native_"+
        std::to_string(error->operating_system_error) : "unknown";
}
inline std::optional<native::SecureOutputDirectory> open_child(
    const native::SecureOutputDirectory& parent,const std::wstring_view leaf,const bool unique,
    std::string* diagnostic=nullptr) {
    if(!parent.valid() || leaf.empty() || leaf.find_first_of(L"/\\:")!=leaf.npos || leaf==L"." || leaf==L"..") {
        if(diagnostic)*diagnostic="invalid_child";return {};
    }
    const auto path=parent.canonical_path()/leaf;
    std::error_code error;const bool created=std::filesystem::create_directory(path,error);
    if(error || (unique && !created)) {
        if(diagnostic)*diagnostic=error ? "create_failed_native_"+std::to_string(error.value()) : "already_exists";
        return {};
    }
    auto opened=native::open_secure_output_directory(path);
    if(!opened && diagnostic)*diagnostic=open_error(opened.error);
    return opened?std::move(opened.directory):std::nullopt;
}
inline std::optional<native::SecureOutputDirectory> open_application_directory(
    const std::filesystem::path& local,std::string& diagnostic) {
    const auto application_path=local/L"HLClientEngine";
    std::error_code status_error;
    const auto status=std::filesystem::symlink_status(application_path,status_error);
    if(!status_error && std::filesystem::exists(status)) {
        // The exact application directory's private child lock already pins
        // its ancestry. Do not additionally acquire a broad LocalAppData lock
        // when no application-directory creation is needed.
        auto opened=native::open_secure_output_directory(application_path);
        if(!opened)diagnostic="application_"+open_error(opened.error);
        return opened?std::move(opened.directory):std::nullopt;
    }
    if(status_error && status_error!=std::errc::no_such_file_or_directory) {
        diagnostic="application_status_native_"+std::to_string(status_error.value());return {};
    }
    // Only the absent fixed child needs a pinned parent during creation.
    // Existing non-directory/reparse children failed closed above.
    auto parent=native::open_secure_output_directory(local);
    if(!parent){diagnostic="local_open_"+open_error(parent.error);return {};}
    std::string child_error;
    auto application=open_child(*parent.directory,L"HLClientEngine",false,&child_error);
    if(!application)diagnostic="application_"+child_error;
    return application;
}
}

// Desktop application bookkeeping, independent of network/gameplay state.
// Every attempt retains its own immutable receipt scope even when the window,
// process and command history survive disconnect/retry.
class ConsoleSessionAttempts final {
public:
    static constexpr std::size_t maximum_attempts=64U;
    explicit ConsoleSessionAttempts(const core::CommandLineOptions& original)
        : original_(original) {records_.reserve(maximum_attempts);}

    [[nodiscard]] std::optional<core::CommandLineOptions> prepare(
        const core::CommandLineOptions& current) {
        try {
            if(finalized_ || (current_ && !records_.empty() && !records_.back().result))return fail("attempt_still_active");
            if(records_.size()>=maximum_attempts)return fail("attempt_limit_reached");
            if(!initialize())return {};
            Record record;record.ordinal=records_.size()+1U;record.role=role_;
            if(record.ordinal==1U) {
                record.run_id=base_id_;record.root=base_->canonical_path();
                auto opened=native::open_secure_output_directory(record.root);
                if(!opened)return fail("attempt_directory_unavailable");
                current_=std::move(opened.directory);
            } else {
                auto group=console_attempt_detail::open_child(*base_,L"attempts-"+native::external_widen(role_),false);
                if(!group)return fail("attempts_directory_unavailable");
                record.run_id=native::new_external_run_id();
                if(!valid_id(record.run_id) || record.run_id==base_id_ ||
                    std::ranges::any_of(records_,[&](const auto& r){return r.run_id==record.run_id;}))
                    return fail("attempt_identity_failed");
                auto opened=console_attempt_detail::open_child(*group,native::external_widen(record.run_id),true);
                if(!opened)return fail("attempt_directory_already_exists_or_unavailable");
                record.root=opened->canonical_path();current_=std::move(opened);
            }
            record.endpoint=current.connect_endpoint.value_or("not_requested");
            if(record.endpoint!="not_requested" && (record.endpoint.empty() || record.endpoint.size()>32U ||
                !std::ranges::all_of(record.endpoint,[](const char c){return (c>='0' && c<='9') || c=='.' || c==':';})))
                return fail("attempt_endpoint_invalid");
            auto out=current;out.open_client=false;
            out.session_run_id=record.run_id;
            out.session_receipt_root=record.ordinal==1U && original_.session_receipt_root
                ? original_.session_receipt_root : std::optional{native::external_utf8(record.root.native())};
            out.session_role=original_.session_role.value_or("A");
            const auto start=encode(record);
            if(!publish(*current_,attempt_leaf(L"start"),start))return fail("attempt_start_publication_failed");
            records_.push_back(std::move(record));
            error_.clear();return out;
        } catch(...) {return fail("attempt_preparation_failed");}
    }

    // A return code is application evidence only. It does not replace durable
    // sign-on/world/gameplay facts or claim server execution/manual acceptance.
    [[nodiscard]] bool finish(const int result,const std::string_view reason) {
        if(!current_ || records_.empty() || records_.back().result)return false;
        auto next=records_.back();next.result=result;
        next.reason=safe_reason(reason)?std::string{reason}:"session_result_unclassified";
        if(!publish(*current_,attempt_leaf(L"result"),encode(next))) {
            error_="attempt_result_publication_failed";return false;
        }
        records_.back()=std::move(next);error_.clear();return true;
    }

    [[nodiscard]] bool finalize() {
        if(finalized_)return true;
        if(records_.empty()){finalized_=true;return true;}
        if(!records_.back().result){error_="attempt_still_active";return false;}
        std::string text="{\"schema\":\"hlclient.console-application-attempts.v1\",\"application_run_id\":"+
            core::external_json_string(base_id_)+",\"gameplay_acceptance\":false,\"attempts\":[";
        for(std::size_t i=0;i<records_.size();++i){if(i)text+=',';text+=encode(records_[i]);}
        text+="]}\n";
        if(text.size()>64U*1024U || !publish(*base_,L"console-application-"+native::external_widen(role_)+L"-attempts.json",text)) {
            error_="application_attempt_index_publication_failed";return false;
        }
        finalized_=true;error_.clear();return true;
    }
    [[nodiscard]] std::string_view error() const noexcept {return error_;}
    [[nodiscard]] std::size_t size() const noexcept {return records_.size();}

private:
    struct Record {
        std::size_t ordinal{};
        std::string run_id,endpoint,reason,role;
        std::filesystem::path root;
        std::optional<int> result;
    };
    static bool valid_id(const std::string_view value) {
        return value.size()==32U && std::ranges::all_of(value,[](const char c) {
            return (c>='0' && c<='9') || (c>='a' && c<='f');
        });
    }
    static bool safe_reason(const std::string_view value) {
        return !value.empty() && value.size()<=96U && std::ranges::all_of(value,[](const char c) {
            return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-';
        });
    }
    bool initialize() {
        if(base_)return true;
        const bool has_scope=original_.session_receipt_root || original_.session_run_id || original_.session_role;
        if(has_scope) {
            if(!original_.session_receipt_root || !original_.session_run_id || !original_.session_role ||
                !valid_id(*original_.session_run_id) ||
                (*original_.session_role!="A" && *original_.session_role!="B")) {
                error_="attempt_original_scope_invalid";return false;
            }
            const auto root=std::filesystem::path{native::external_widen(*original_.session_receipt_root)};
            auto opened=native::open_secure_output_directory(root);
            if(!opened){error_="attempt_original_scope_unavailable";return false;}
            base_id_=*original_.session_run_id;role_=*original_.session_role;base_=std::move(opened.directory);return true;
        }
        const auto settings=native::default_external_settings_path();
        if(!settings){error_="attempt_user_scope_known_folder_unavailable";return false;}
        std::string diagnostic;
        auto application=console_attempt_detail::open_application_directory(settings->parent_path().parent_path(),diagnostic);
        if(!application){error_="attempt_user_scope_"+diagnostic;return false;}
        auto runs=console_attempt_detail::open_child(*application,L"runs",false,&diagnostic);
        if(!runs){error_="attempt_user_scope_runs_"+diagnostic;return false;}
        base_id_=native::new_external_run_id();
        if(!valid_id(base_id_)){error_="attempt_identity_failed";return false;}
        base_=console_attempt_detail::open_child(*runs,native::external_widen(base_id_),true,&diagnostic);
        if(!base_){error_="attempt_user_scope_new_attempt_"+diagnostic;return false;}
        return true;
    }
    static std::string encode(const Record& record) {
        return "{\"schema\":\"hlclient.console-session-attempt.v1\",\"ordinal\":"+
            std::to_string(record.ordinal)+",\"run_id\":"+core::external_json_string(record.run_id)+
            ",\"session_role\":"+core::external_json_string(record.role)+
            ",\"receipt_root\":"+core::external_json_string(native::external_utf8(record.root.native()))+
            ",\"requested_endpoint\":"+core::external_json_string(record.endpoint)+
            ",\"result_code\":"+(record.result?std::to_string(*record.result):"null")+
            ",\"reason\":"+core::external_json_string(record.reason.empty()?"not_finished":record.reason)+
            ",\"gameplay_acceptance\":false}";
    }
    static bool publish(const native::SecureOutputDirectory& directory,const std::wstring_view leaf,
        const std::string_view text) {
        return static_cast<bool>(native::secure_atomic_write_new(directory,leaf,
            std::as_bytes(std::span{text.data(),text.size()})));
    }
    std::optional<core::CommandLineOptions> fail(const std::string_view value) {
        error_=value;return {};
    }
    std::wstring attempt_leaf(const std::wstring_view suffix) const {
        return L"console-attempt-"+native::external_widen(role_)+L"-"+std::wstring{suffix}+L".json";
    }
    core::CommandLineOptions original_;
    std::string base_id_,error_,role_{"A"};
    std::optional<native::SecureOutputDirectory> base_,current_;
    std::vector<Record> records_;
    bool finalized_{};
};

#endif

} // namespace hlclient::app
