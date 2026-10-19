#include <hlclient/app/console_steam_authentication.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace hlclient::app {
namespace {
using Result=auth::AuthenticationUpdateResult;
using Status=auth::AuthenticationUpdateState;
auth::AuthenticationError cancelled_error(){
    return {auth::AuthenticationErrorCode::cancelled,"Steam authentication operation was cancelled"};
}

// One worker per fresh application attempt. There is one outstanding poll bit,
// not an unbounded task queue. Every native API call, including API destruction
// and the session lifetime guard, stays on this worker. No socket/renderer or
// gameplay object is passed to it.
class Worker final {
public:
    Worker(SteamAuthenticationProviderConfig config,std::function<void()> pump)
        :config_(std::move(config)),trace_(std::move(config_.trace)),pump_(std::move(pump)) {
        config_.trace=[this](const auto& event){
            std::lock_guard lock(mutex_);
            // A single provider operation emits fewer than 16 typed milestones.
            // Keep a defensive bound without ever carrying ticket bytes.
            if(traces_.size()<64U)traces_.push_back(event);
        };
        thread_=std::thread([this]{run();});
    }
    ~Worker(){
        {std::lock_guard lock(mutex_);stop_=true;}cv_.notify_one();
        for(;;){
            drain_traces();
            {std::unique_lock lock(mutex_);if(done_)break;
                if(!pump_)cv_.wait_for(lock,std::chrono::milliseconds{5});}
            // Only this application's owner pumps its UI, outside all locks.
            // An unpreemptible native ABI call is never force-terminated.
            if(pump_)try{pump_();}catch(...){}
        }
        thread_.join();drain_traces();
    }
    bool begin(const auth::AuthenticationRequestContext& context){
        std::lock_guard lock(mutex_);if(begun_)return false;
        begun_=true;context_=context;cv_.notify_one();return true;
    }
    void cancel()noexcept{
        {std::lock_guard lock(mutex_);cancelled_=true;}cv_.notify_one();
    }
    void release()noexcept{
        {std::lock_guard lock(mutex_);released_=true;}cv_.notify_one();
    }
    struct Poll {Status status{Status::pending};std::optional<goldsrc::AuthenticationMaterial> material;std::optional<auth::AuthenticationError> error;};
    Poll poll(){
        drain_traces();Poll result;
        {std::lock_guard lock(mutex_);
            if(cancelled_)result={Status::failed,{},cancelled_error()};
            else if(error_)result={Status::failed,{},error_};
            else if(material_)result={Status::succeeded,std::exchange(material_,{}),{}};
            else if(!terminal_){poll_=true;cv_.notify_one();}}
        // The native milestone is queued before its result is published, but
        // may race the first drain. Deliver it before handshake consumes result.
        drain_traces();return result;
    }
private:
    void drain_traces()noexcept{
        std::vector<SteamAuthenticationTraceEvent> events;
        {std::lock_guard lock(mutex_);events.swap(traces_);}
        if(trace_)for(const auto& event:events)try{trace_(event);}catch(...){}
    }
    void fail(auth::AuthenticationError error){
        std::lock_guard lock(mutex_);error_=std::move(error);terminal_=true;
    }
    void run()noexcept{
        try{
            run_operation();
        }catch(...){
            // Native diagnostics may contain protected data; do not propagate
            // arbitrary exception text through the user console or receipts.
            fail({auth::AuthenticationErrorCode::provider_error,"Steam authentication worker failed"});
        }
        {std::lock_guard lock(mutex_);done_=true;}cv_.notify_all();
    }
    void run_operation(){
        auth::AuthenticationRequestContext context;
        {std::unique_lock lock(mutex_);cv_.wait(lock,[this]{return begun_||stop_;});
            if(stop_)return;context=*context_;}
        SteamAuthenticationProvider provider{std::move(config_)};
        auto begun=provider.begin(context);
        if(!begun){fail(begun.error.value_or(auth::AuthenticationError{
            auth::AuthenticationErrorCode::provider_error,"Steam authentication could not begin"}));return;}
        auto operation=std::move(begun.operation);
        std::optional<auth::AuthenticationSession> retained_session;
        bool cancellation_seen{};
        for(;;){
            bool cancel{},release{},stop{};
            {std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stop_||released_||poll_||(cancelled_&&!cancellation_seen);});
                stop=stop_;cancel=cancelled_&&!cancellation_seen;release=std::exchange(released_,false);poll_=false;}
            if(stop||cancel){
                cancellation_seen=true;if(operation)operation->cancel();
                operation.reset();retained_session.reset();
                {std::lock_guard lock(mutex_);material_.reset();terminal_=true;}
                if(stop)break;continue;
            }
            if(release){retained_session.reset();continue;}
            if(!operation)continue;
            auto result=operation->update();
            bool discard{};
            {std::lock_guard lock(mutex_);discard=cancelled_||stop_;}
            if(discard)continue; // result (and any native lifetime) dies here on worker
            if(result.state==Status::pending)continue;
            operation.reset();
            if(result.state==Status::succeeded && result.session){
                auto material=result.session->take_material();
                retained_session=std::move(result.session);
                std::lock_guard lock(mutex_);terminal_=true;
                if(material)material_=std::move(material);
                else error_=auth::AuthenticationError{auth::AuthenticationErrorCode::invalid_material,
                    "Steam authentication returned no material"};
            }else fail(result.error.value_or(auth::AuthenticationError{
                auth::AuthenticationErrorCode::provider_error,"Steam authentication failed"}));
        }
        // Locals are destroyed before done_ is published: native lifetime,
        // operation, provider and its DLL all have the same worker owner.
    }
    SteamAuthenticationProviderConfig config_;
    SteamAuthenticationTraceCallback trace_;
    std::function<void()> pump_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::vector<SteamAuthenticationTraceEvent> traces_;
    std::optional<auth::AuthenticationRequestContext> context_;
    std::optional<goldsrc::AuthenticationMaterial> material_;
    std::optional<auth::AuthenticationError> error_;
    bool begun_{},poll_{},cancelled_{},released_{},stop_{},done_{},terminal_{};
};
class SessionLifetime final:public auth::IAuthenticationSessionLifetime {
public:
    explicit SessionLifetime(std::shared_ptr<Worker> worker):worker_(std::move(worker)){}
    ~SessionLifetime()override{worker_->release();}
private:std::shared_ptr<Worker> worker_;
};
class Operation final:public auth::IAuthenticationOperation {
public:
    explicit Operation(std::shared_ptr<Worker> worker):worker_(std::move(worker)){}
    ~Operation()override{if(!complete_)cancel();}
    Result update()override{
        if(complete_)return Result::failed({auth::AuthenticationErrorCode::provider_error,
            "Steam authentication operation is already complete"});
        auto result=worker_->poll();
        if(result.status==Status::pending)return Result::pending();
        complete_=true;
        if(result.material)return Result::succeeded(auth::AuthenticationSession{
            std::move(*result.material),std::make_unique<SessionLifetime>(worker_)});
        return Result::failed(result.error.value_or(cancelled_error()));
    }
    void cancel()noexcept override{if(!complete_)worker_->cancel();}
private:std::shared_ptr<Worker> worker_;bool complete_{};
};
class Provider final:public auth::IAuthenticationProvider {
public:
    Provider(SteamAuthenticationProviderConfig config,std::function<void()> pump)
        :worker_(std::make_shared<Worker>(std::move(config),std::move(pump))){}
    auth::AuthenticationBeginResult begin(const auth::AuthenticationRequestContext& context)override{
        if(!worker_->begin(context))return auth::AuthenticationBeginResult::failed({
            auth::AuthenticationErrorCode::provider_error,"Steam authentication attempt already started"});
        return auth::AuthenticationBeginResult::started(std::make_unique<Operation>(worker_));
    }
private:std::shared_ptr<Worker> worker_;
};
}
std::unique_ptr<auth::IAuthenticationProvider> make_console_steam_authentication_provider(
    SteamAuthenticationProviderConfig config,std::function<void()> pump) {
    return std::make_unique<Provider>(std::move(config),std::move(pump));
}
}
