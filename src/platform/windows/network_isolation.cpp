#include <hlclient/platform/windows/network_isolation.hpp>
#include <hlclient/platform/windows/process_orchestrator.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fwpmu.h>
#include <iphlpapi.h>
#include <rpc.h>

namespace hlclient::platform::windows {
namespace {

class UniqueSocket final {
public:
    UniqueSocket() noexcept = default;
    explicit UniqueSocket(const SOCKET socket) noexcept : socket_{socket} {}
    ~UniqueSocket()
    {
        if (socket_ != INVALID_SOCKET) {
            static_cast<void>(::closesocket(socket_));
        }
    }
    UniqueSocket(UniqueSocket&& other) noexcept
        : socket_{std::exchange(other.socket_, INVALID_SOCKET)}
    {
    }
    UniqueSocket& operator=(UniqueSocket&& other) noexcept
    {
        if (this != &other) {
            if (socket_ != INVALID_SOCKET) {
                static_cast<void>(::closesocket(socket_));
            }
            socket_ = std::exchange(other.socket_, INVALID_SOCKET);
        }
        return *this;
    }
    UniqueSocket(const UniqueSocket&) = delete;
    UniqueSocket& operator=(const UniqueSocket&) = delete;
    [[nodiscard]] SOCKET get() const noexcept { return socket_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return socket_ != INVALID_SOCKET;
    }

private:
    SOCKET socket_{INVALID_SOCKET};
};

class WinsockRuntime final {
public:
    WinsockRuntime() noexcept
    {
        WSADATA data{};
        started_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockRuntime()
    {
        if (started_) {
            static_cast<void>(::WSACleanup());
        }
    }
    [[nodiscard]] explicit operator bool() const noexcept { return started_; }

private:
    bool started_{false};
};

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept
{
    try {
        const auto a = left.wstring();
        const auto b = right.wstring();
        if (a.size() != b.size() ||
            a.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            return false;
        }
        return ::CompareStringOrdinal(
                   a.data(), static_cast<int>(a.size()),
                   b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool is_access_denied(const DWORD error) noexcept
{
    return error == ERROR_ACCESS_DENIED;
}

[[nodiscard]] NetworkIsolationStartResult start_error(
    const NetworkIsolationErrorCode code,
    const DWORD native) noexcept
{
    return NetworkIsolationStartResult{code, native, std::nullopt};
}

[[nodiscard]] bool add_filter(
    const HANDLE engine,
    const GUID& provider,
    const GUID& sublayer,
    const GUID& layer,
    const std::vector<std::byte>& app_id,
    const bool permit,
    const bool ipv6,
    const bool loopback_only,
    UINT64& filter_id,
    DWORD& error) noexcept
{
    if (app_id.empty() ||
        app_id.size() > static_cast<std::size_t>((std::numeric_limits<UINT32>::max)())) {
        error = ERROR_INVALID_PARAMETER;
        return false;
    }
    FWP_BYTE_BLOB application_blob{
        static_cast<UINT32>(app_id.size()),
        reinterpret_cast<UINT8*>(const_cast<std::byte*>(app_id.data()))};
    std::array<FWPM_FILTER_CONDITION0, 2U> conditions{};
    conditions[0U].fieldKey = FWPM_CONDITION_ALE_APP_ID;
    conditions[0U].matchType = FWP_MATCH_EQUAL;
    conditions[0U].conditionValue.type = FWP_BYTE_BLOB_TYPE;
    conditions[0U].conditionValue.byteBlob = &application_blob;

    FWP_V4_ADDR_AND_MASK v4_loopback{0x7f000000U, 0xff000000U};
    FWP_V6_ADDR_AND_MASK v6_loopback{};
    v6_loopback.addr[15U] = 1U;
    v6_loopback.prefixLength = 128U;
    std::size_t condition_count = 1U;
    if (loopback_only) {
        conditions[1U].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
        conditions[1U].matchType = FWP_MATCH_EQUAL;
        if (ipv6) {
            conditions[1U].conditionValue.type = FWP_V6_ADDR_MASK;
            conditions[1U].conditionValue.v6AddrMask = &v6_loopback;
        } else {
            conditions[1U].conditionValue.type = FWP_V4_ADDR_MASK;
            conditions[1U].conditionValue.v4AddrMask = &v4_loopback;
        }
        condition_count = 2U;
    }

    UINT64 weight = permit ? 15U : 5U;
    FWPM_FILTER0 filter{};
    filter.displayData.name = const_cast<wchar_t*>(
        permit ? L"HLClient temporary loopback permit"
               : L"HLClient temporary non-loopback block");
    filter.displayData.description = const_cast<wchar_t*>(
        L"Research-only dynamic filter; removed with its WFP session");
    filter.providerKey = const_cast<GUID*>(&provider);
    filter.layerKey = layer;
    filter.subLayerKey = sublayer;
    filter.weight.type = FWP_UINT64;
    filter.weight.uint64 = &weight;
    filter.numFilterConditions = static_cast<UINT32>(condition_count);
    filter.filterCondition = conditions.data();
    filter.action.type = permit ? FWP_ACTION_PERMIT : FWP_ACTION_BLOCK;
    filter.flags = 0U; // In particular, FWPM_FILTER_FLAG_PERSISTENT is absent.
    error = ::FwpmFilterAdd0(engine, &filter, nullptr, &filter_id);
    return error == ERROR_SUCCESS;
}

[[nodiscard]] bool run_probe_process(
    KillOnCloseProcessJob& job,
    const WindowsBinaryIdentity& executable,
    const std::wstring_view mode,
    const std::wstring_view host,
    const std::uint16_t port,
    const std::chrono::milliseconds timeout,
    DWORD& exit_code,
    std::size_t* const processes_started = nullptr) noexcept
{
    try {
        OwnedProcessLaunchSpec spec;
        spec.executable = executable.canonical_path;
        spec.arguments = {
            L"--mode", std::wstring{mode}, L"--host", std::wstring{host},
            L"--port", std::to_wstring(port)};
        spec.working_directory = executable.canonical_path.parent_path();
        spec.expected_identity = executable;
        spec.prohibit_child_processes = true;
        auto [process, launched] = job.launch(spec);
        if (!launched) {
            exit_code = launched.native_error;
            return false;
        }
        if (processes_started != nullptr) {
            ++*processes_started;
        }
        const auto result = process.wait(timeout);
        if (!result) {
            job.terminate(125U);
            exit_code = WAIT_TIMEOUT;
            return false;
        }
        exit_code = *result;
        return true;
    } catch (...) {
        exit_code = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
}

struct Listener final {
    UniqueSocket socket;
    std::wstring host;
    std::uint16_t port{0U};
};

[[nodiscard]] std::optional<Listener> make_listener_v4(
    const IN_ADDR& address) noexcept
{
    UniqueSocket socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    if (!socket) {
        return std::nullopt;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr = address;
    local.sin_port = 0U;
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&local),
               sizeof(local)) == SOCKET_ERROR ||
        ::listen(socket.get(), 8) == SOCKET_ERROR) {
        return std::nullopt;
    }
    int size = sizeof(local);
    if (::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&local), &size) ==
        SOCKET_ERROR) {
        return std::nullopt;
    }
    std::array<wchar_t, INET_ADDRSTRLEN> host{};
    if (::InetNtopW(AF_INET, &local.sin_addr, host.data(), host.size()) == nullptr) {
        return std::nullopt;
    }
    return Listener{std::move(socket), host.data(), ntohs(local.sin_port)};
}

[[nodiscard]] std::optional<Listener> make_listener_v6_loopback() noexcept
{
    UniqueSocket socket{::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)};
    if (!socket) {
        return std::nullopt;
    }
    DWORD v6_only = 1U;
    static_cast<void>(::setsockopt(socket.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                                  reinterpret_cast<const char*>(&v6_only),
                                  sizeof(v6_only)));
    sockaddr_in6 local{};
    local.sin6_family = AF_INET6;
    local.sin6_addr = in6addr_loopback;
    local.sin6_port = 0U;
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&local),
               sizeof(local)) == SOCKET_ERROR ||
        ::listen(socket.get(), 8) == SOCKET_ERROR) {
        return std::nullopt;
    }
    int size = sizeof(local);
    if (::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&local), &size) ==
        SOCKET_ERROR) {
        return std::nullopt;
    }
    return Listener{std::move(socket), L"::1", ntohs(local.sin6_port)};
}

[[nodiscard]] std::optional<IN_ADDR> find_non_loopback_local_v4() noexcept
{
    ULONG bytes = 16U * 1'024U;
    std::vector<std::byte> buffer(bytes);
    ULONG result = ::GetAdaptersAddresses(
        AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                     GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &bytes);
    if (result == ERROR_BUFFER_OVERFLOW && bytes <= 4U * 1'024U * 1'024U) {
        buffer.resize(bytes);
        result = ::GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                         GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &bytes);
    }
    if (result != NO_ERROR) {
        return std::nullopt;
    }
    for (auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
         adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp ||
            adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
             unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr ||
                unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            const auto address = reinterpret_cast<const sockaddr_in*>(
                unicast->Address.lpSockaddr)->sin_addr;
            const auto host = ntohl(address.S_un.S_addr);
            if ((host >> 24U) != 127U && host != 0U && host != 0xffffffffU) {
                return address;
            }
        }
    }
    return std::nullopt;
}

} // namespace

struct DynamicNetworkIsolationSession::Impl final {
    HANDLE engine{nullptr};
    NetworkIsolationAttestation attestation{};

    ~Impl()
    {
        if (engine != nullptr) {
            static_cast<void>(::FwpmEngineClose0(engine));
        }
    }
};

NetworkIsolationHeartbeatReadDisposition
classify_network_isolation_heartbeat_read(
    const bool read_succeeded,
    const std::uint32_t byte_count,
    const std::uint32_t native_error) noexcept
{
    if (read_succeeded) {
        return byte_count == 0U
            ? NetworkIsolationHeartbeatReadDisposition::clean_eof
            : NetworkIsolationHeartbeatReadDisposition::continue_waiting;
    }
    return native_error == ERROR_BROKEN_PIPE
        ? NetworkIsolationHeartbeatReadDisposition::clean_eof
        : NetworkIsolationHeartbeatReadDisposition::error;
}

NetworkIsolationValidation validate_network_isolation_policy(
    const NetworkIsolationPolicy& policy) noexcept
{
    if (policy.applications.empty()) {
        return {NetworkIsolationErrorCode::empty_application_set, 0U};
    }
    if (policy.applications.size() > kMaximumIsolatedApplications) {
        return {NetworkIsolationErrorCode::too_many_applications, 0U};
    }
    if (!policy.allow_ipv4_loopback || !policy.allow_ipv6_loopback ||
        !policy.block_non_loopback_outbound ||
        !policy.block_non_loopback_inbound_accept ||
        !policy.dynamic_session_required || policy.persistent_filters_allowed) {
        return {NetworkIsolationErrorCode::policy_not_fail_closed, 0U};
    }
    for (std::size_t index = 0U; index < policy.applications.size(); ++index) {
        const auto& application = policy.applications[index];
        if (application.identity.canonical_path.empty() ||
            application.identity.snapshot.size == 0U ||
            application.wfp_application_id.empty() ||
            application.wfp_application_id.size() > 64U * 1'024U) {
            return {NetworkIsolationErrorCode::invalid_application_id, index};
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (same_path(application.identity.canonical_path,
                          policy.applications[prior].identity.canonical_path) ||
                application.identity.snapshot.identity ==
                    policy.applications[prior].identity.snapshot.identity ||
                application.wfp_application_id ==
                    policy.applications[prior].wfp_application_id) {
                return {NetworkIsolationErrorCode::duplicate_application, index};
            }
        }
    }
    return {};
}

std::optional<NetworkIsolationFilterPlan> build_network_isolation_filter_plan(
    const NetworkIsolationPolicy& policy) noexcept
{
    const auto validation = validate_network_isolation_policy(policy);
    if (!validation || policy.applications.size() >
            (std::numeric_limits<std::size_t>::max)() / 4U) {
        return std::nullopt;
    }
    NetworkIsolationFilterPlan plan;
    plan.ipv4_loopback_network_host_order = 0x7f000000U;
    plan.ipv4_loopback_mask_host_order = 0xff000000U;
    plan.ipv6_loopback_address[15U] = std::byte{1U};
    plan.ipv6_loopback_prefix_length = 128U;
    plan.permit_filter_count = policy.applications.size() * 4U;
    plan.block_filter_count = policy.applications.size() * 4U;
    plan.dynamic_only = true;
    plan.persistent_filter_count = 0U;
    return plan;
}

std::optional<NetworkIsolationApplication> observe_network_isolation_application(
    const std::filesystem::path& executable,
    WindowsBinaryIdentityErrorCode& binary_error,
    NetworkIsolationErrorCode& isolation_error) noexcept
{
    binary_error = WindowsBinaryIdentityErrorCode::none;
    isolation_error = NetworkIsolationErrorCode::none;
    try {
        const WindowsBinaryObservationPolicy project_policy{
            AuthenticodePolicy::not_required_for_project_owned_binary, false};
        auto identity = observe_windows_binary_identity(
            executable, kMaximumObservedExecutableBytes, project_policy);
        if (!identity) {
            binary_error = identity.code;
            return std::nullopt;
        }
        FWP_BYTE_BLOB* app_id = nullptr;
        const DWORD result = ::FwpmGetAppIdFromFileName0(
            identity.identity->canonical_path.c_str(), &app_id);
        if (result != ERROR_SUCCESS || app_id == nullptr || app_id->data == nullptr ||
            app_id->size == 0U || app_id->size > 64U * 1'024U) {
            if (app_id != nullptr) {
                ::FwpmFreeMemory0(reinterpret_cast<void**>(&app_id));
            }
            isolation_error = NetworkIsolationErrorCode::invalid_application_id;
            return std::nullopt;
        }
        std::vector<std::byte> bytes(app_id->size);
        std::copy_n(reinterpret_cast<const std::byte*>(app_id->data),
                    app_id->size, bytes.begin());
        ::FwpmFreeMemory0(reinterpret_cast<void**>(&app_id));
        return NetworkIsolationApplication{std::move(*identity.identity),
                                           std::move(bytes)};
    } catch (...) {
        isolation_error = NetworkIsolationErrorCode::invalid_application_id;
        return std::nullopt;
    }
}

DynamicNetworkIsolationSession::DynamicNetworkIsolationSession() noexcept = default;
DynamicNetworkIsolationSession::~DynamicNetworkIsolationSession() = default;
DynamicNetworkIsolationSession::DynamicNetworkIsolationSession(
    DynamicNetworkIsolationSession&&) noexcept = default;
DynamicNetworkIsolationSession& DynamicNetworkIsolationSession::operator=(
    DynamicNetworkIsolationSession&&) noexcept = default;
DynamicNetworkIsolationSession::DynamicNetworkIsolationSession(
    std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)}
{
}

std::pair<DynamicNetworkIsolationSession, NetworkIsolationStartResult>
DynamicNetworkIsolationSession::start(const NetworkIsolationPolicy& policy) noexcept
{
    try {
    const auto validation = validate_network_isolation_policy(policy);
    if (!validation) {
        return {DynamicNetworkIsolationSession{},
                start_error(validation.code, ERROR_INVALID_PARAMETER)};
    }
    for (const auto& application : policy.applications) {
        const WindowsBinaryObservationPolicy observation_policy{
            application.identity.authenticode_valid
                ? AuthenticodePolicy::required
                : AuthenticodePolicy::not_required_for_project_owned_binary,
            application.identity.file_version.has_value()};
        const auto observed = observe_windows_binary_identity(
            application.identity.canonical_path,
            kMaximumObservedExecutableBytes, observation_policy);
        if (!observed ||
            !same_windows_file_identity(*observed.identity, application.identity)) {
            return {DynamicNetworkIsolationSession{},
                    start_error(NetworkIsolationErrorCode::stale_application_identity,
                                ERROR_FILE_INVALID)};
        }
    }

    auto impl = std::make_unique<Impl>();
    FWPM_SESSION0 session{};
    session.displayData.name = const_cast<wchar_t*>(
        L"HLClient stock runtime dynamic network isolation");
    session.displayData.description = const_cast<wchar_t*>(
        L"Temporary research-only loopback policy");
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    DWORD error = ::FwpmEngineOpen0(
        nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &impl->engine);
    if (error != ERROR_SUCCESS) {
        return {DynamicNetworkIsolationSession{},
                start_error(is_access_denied(error)
                                ? NetworkIsolationErrorCode::privilege_required
                                : NetworkIsolationErrorCode::wfp_unavailable,
                            error)};
    }

    GUID provider_key{};
    GUID sublayer_key{};
    const auto usable_local_uuid = [](const RPC_STATUS status) noexcept {
        return status == RPC_S_OK || status == RPC_S_UUID_LOCAL_ONLY;
    };
    if (!usable_local_uuid(::UuidCreate(&provider_key)) ||
        !usable_local_uuid(::UuidCreate(&sublayer_key))) {
        return {DynamicNetworkIsolationSession{},
                start_error(NetworkIsolationErrorCode::provider_failed,
                            RPC_S_UUID_LOCAL_ONLY)};
    }
    error = ::FwpmTransactionBegin0(impl->engine, 0U);
    if (error != ERROR_SUCCESS) {
        return {DynamicNetworkIsolationSession{},
                start_error(NetworkIsolationErrorCode::transaction_failed, error)};
    }
    bool transaction_open = true;
    const auto abort = [&]() noexcept {
        if (transaction_open) {
            static_cast<void>(::FwpmTransactionAbort0(impl->engine));
            transaction_open = false;
        }
    };

    FWPM_PROVIDER0 provider{};
    provider.providerKey = provider_key;
    provider.displayData.name = const_cast<wchar_t*>(
        L"HLClient temporary stock-runtime provider");
    provider.displayData.description = const_cast<wchar_t*>(
        L"Dynamic provider; never persisted");
    provider.flags = 0U;
    error = ::FwpmProviderAdd0(impl->engine, &provider, nullptr);
    if (error != ERROR_SUCCESS) {
        abort();
        return {DynamicNetworkIsolationSession{},
                start_error(NetworkIsolationErrorCode::provider_failed, error)};
    }

    FWPM_SUBLAYER0 sublayer{};
    sublayer.subLayerKey = sublayer_key;
    sublayer.displayData.name = const_cast<wchar_t*>(
        L"HLClient temporary stock-runtime sublayer");
    sublayer.displayData.description = const_cast<wchar_t*>(
        L"Dynamic fail-closed exact-application policy");
    sublayer.providerKey = &provider_key;
    sublayer.flags = 0U;
    sublayer.weight = 0x6f10U;
    error = ::FwpmSubLayerAdd0(impl->engine, &sublayer, nullptr);
    if (error != ERROR_SUCCESS) {
        abort();
        return {DynamicNetworkIsolationSession{},
                start_error(NetworkIsolationErrorCode::sublayer_failed, error)};
    }

    const std::array<std::pair<GUID, bool>, 4U> layers{{
        {FWPM_LAYER_ALE_AUTH_CONNECT_V4, false},
        {FWPM_LAYER_ALE_AUTH_CONNECT_V6, true},
        {FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, false},
        {FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, true},
    }};
    std::size_t permits = 0U;
    std::size_t blocks = 0U;
    for (const auto& application : policy.applications) {
        for (const auto& [layer, ipv6] : layers) {
            UINT64 filter_id = 0U;
            if (!add_filter(impl->engine, provider_key, sublayer_key, layer,
                            application.wfp_application_id, true, ipv6, true,
                            filter_id, error)) {
                abort();
                return {DynamicNetworkIsolationSession{},
                        start_error(NetworkIsolationErrorCode::filter_failed, error)};
            }
            ++permits;
            if (!add_filter(impl->engine, provider_key, sublayer_key, layer,
                            application.wfp_application_id, false, ipv6, false,
                            filter_id, error)) {
                abort();
                return {DynamicNetworkIsolationSession{},
                        start_error(NetworkIsolationErrorCode::filter_failed, error)};
            }
            ++blocks;
        }
    }
    error = ::FwpmTransactionCommit0(impl->engine);
    transaction_open = false;
    if (error != ERROR_SUCCESS) {
        return {DynamicNetworkIsolationSession{},
                start_error(NetworkIsolationErrorCode::commit_failed, error)};
    }
    impl->attestation = NetworkIsolationAttestation{
        true, true, true, true, true, policy.applications.size(),
        permits, blocks, 0U};
    auto attestation = impl->attestation;
    return {DynamicNetworkIsolationSession{std::move(impl)},
            NetworkIsolationStartResult{NetworkIsolationErrorCode::none, 0U,
                                        attestation}};
    } catch (...) {
        return {DynamicNetworkIsolationSession{},
                start_error(NetworkIsolationErrorCode::dynamic_session_failed,
                            ERROR_NOT_ENOUGH_MEMORY)};
    }
}

bool DynamicNetworkIsolationSession::active() const noexcept
{
    return impl_ != nullptr && impl_->engine != nullptr;
}

const NetworkIsolationAttestation&
DynamicNetworkIsolationSession::attestation() const noexcept
{
    static const NetworkIsolationAttestation empty{};
    return impl_ ? impl_->attestation : empty;
}

void DynamicNetworkIsolationSession::close() noexcept
{
    impl_.reset();
}

bool windows_process_is_elevated() noexcept
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0U;
    const BOOL result = ::GetTokenInformation(
        token, TokenElevation, &elevation, sizeof(elevation), &size);
    static_cast<void>(::CloseHandle(token));
    return result != FALSE && size == sizeof(elevation) &&
           elevation.TokenIsElevated != 0U;
}

NetworkIsolationCanaryResult run_network_isolation_canary(
    const std::filesystem::path& probe_executable,
    const NetworkIsolationPolicy& policy,
    const std::chrono::milliseconds timeout) noexcept
{
    try {
        WinsockRuntime winsock;
        if (!winsock || timeout <= std::chrono::milliseconds::zero()) {
            return {NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                    false, false, false,
                    static_cast<std::uint32_t>(::WSAGetLastError())};
        }
        IN_ADDR loopback{};
        loopback.S_un.S_addr = htonl(0x7f000001U);
        auto v4 = make_listener_v4(loopback);
        auto v6 = make_listener_v6_loopback();
        const auto local_address = find_non_loopback_local_v4();
        auto nonloopback = local_address
            ? make_listener_v4(*local_address)
            : std::optional<Listener>{};
        if (!v4 || !nonloopback) {
            return {NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                    false, false, false, 0U};
        }

        auto [session, started] = DynamicNetworkIsolationSession::start(policy);
        if (!started) {
            return {NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                    false, false, false, started.native_error};
        }
        const WindowsBinaryObservationPolicy project_policy{
            AuthenticodePolicy::not_required_for_project_owned_binary, false};
        auto probe_identity = observe_windows_binary_identity(
            probe_executable, kMaximumObservedExecutableBytes, project_policy);
        if (!probe_identity) {
            return {NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                    false, false, false, probe_identity.native_error};
        }
        bool probe_is_covered = false;
        for (const auto& application : policy.applications) {
            if (same_windows_file_identity(application.identity,
                                           *probe_identity.identity)) {
                probe_is_covered = true;
                break;
            }
        }
        if (!probe_is_covered) {
            return {NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                    false, false, false, ERROR_ACCESS_DENIED};
        }
        auto [job, job_result] = KillOnCloseProcessJob::create(1U);
        if (!job_result) {
            return {NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                    false, false, false, job_result.native_error};
        }
        DWORD exit_code = 0U;
        const bool v4_launched = run_probe_process(
            job, *probe_identity.identity, L"ipv4-loopback", v4->host, v4->port,
            timeout, exit_code);
        if (!v4_launched || exit_code != 0U) {
            return {NetworkIsolationCanaryStatus::loopback_failed,
                    false, false, false, exit_code};
        }
        bool ipv6_allowed = false;
        if (v6) {
            const bool v6_launched = run_probe_process(
                job, *probe_identity.identity, L"ipv6-loopback", v6->host, v6->port,
                timeout, exit_code);
            if (!v6_launched || exit_code != 0U) {
                return {NetworkIsolationCanaryStatus::loopback_failed,
                        true, false, false, exit_code};
            }
            ipv6_allowed = true;
        }
        const bool denied_launched = run_probe_process(
            job, *probe_identity.identity, L"nonloopback-denied", nonloopback->host,
            nonloopback->port, timeout, exit_code);
        if (!denied_launched || exit_code != 0U) {
            return {exit_code == 3U
                        ? NetworkIsolationCanaryStatus::denied_without_os_classification
                        : NetworkIsolationCanaryStatus::non_loopback_not_denied,
                    true, ipv6_allowed, false, exit_code};
        }
        session.close();

        const bool restored_launched = run_probe_process(
            job, *probe_identity.identity, L"restored-nonloopback", nonloopback->host,
            nonloopback->port, timeout, exit_code);
        if (!restored_launched || exit_code != 0U) {
            return {NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                    true, ipv6_allowed, true, exit_code};
        }
        return {NetworkIsolationCanaryStatus::success,
                true, ipv6_allowed, true, 0U};
    } catch (...) {
        return {NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                false, false, false, ERROR_NOT_ENOUGH_MEMORY};
    }
}

NetworkIsolationCanaryResult
run_network_isolation_canary_under_existing_guard(
    KillOnCloseProcessJob& campaign_job,
    const std::filesystem::path& probe_executable,
    const std::chrono::milliseconds timeout) noexcept
{
    std::size_t processes_started = 0U;
    const auto result = [&processes_started](
        const NetworkIsolationCanaryStatus status,
        const bool ipv4_loopback_allowed,
        const bool ipv6_loopback_allowed,
        const bool non_loopback_os_denied,
        const std::uint32_t native_error) noexcept {
        return NetworkIsolationCanaryResult{
            status, ipv4_loopback_allowed, ipv6_loopback_allowed,
            non_loopback_os_denied, native_error, processes_started};
    };
    try {
        WinsockRuntime winsock;
        if (!winsock || timeout <= std::chrono::milliseconds::zero()) {
            return result(
                NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                false, false, false,
                static_cast<std::uint32_t>(::WSAGetLastError()));
        }
        IN_ADDR loopback{};
        loopback.S_un.S_addr = htonl(0x7f000001U);
        auto v4 = make_listener_v4(loopback);
        auto v6 = make_listener_v6_loopback();
        const auto local_address = find_non_loopback_local_v4();
        auto nonloopback = local_address
            ? make_listener_v4(*local_address)
            : std::optional<Listener>{};
        if (!v4 || !nonloopback) {
            return result(
                NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                false, false, false, 0U);
        }
        const WindowsBinaryObservationPolicy project_policy{
            AuthenticodePolicy::not_required_for_project_owned_binary, false};
        auto probe_identity = observe_windows_binary_identity(
            probe_executable, kMaximumObservedExecutableBytes, project_policy);
        if (!probe_identity) {
            return result(
                NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                false, false, false, probe_identity.native_error);
        }
        if (!campaign_job.valid()) {
            return result(
                NetworkIsolationCanaryStatus::isolation_canary_unavailable,
                false, false, false, ERROR_INVALID_HANDLE);
        }
        DWORD exit_code = 0U;
        if (!run_probe_process(
                campaign_job, *probe_identity.identity, L"ipv4-loopback",
                v4->host, v4->port, timeout, exit_code,
                &processes_started) || exit_code != 0U) {
            return result(NetworkIsolationCanaryStatus::loopback_failed,
                          false, false, false, exit_code);
        }
        bool ipv6_allowed = false;
        if (v6) {
            if (!run_probe_process(
                    campaign_job, *probe_identity.identity, L"ipv6-loopback",
                    v6->host, v6->port, timeout, exit_code,
                    &processes_started) || exit_code != 0U) {
                return result(NetworkIsolationCanaryStatus::loopback_failed,
                              true, false, false, exit_code);
            }
            ipv6_allowed = true;
        }
        if (!run_probe_process(
                campaign_job, *probe_identity.identity, L"nonloopback-denied",
                nonloopback->host, nonloopback->port, timeout, exit_code,
                &processes_started) ||
            exit_code != 0U) {
            return result(
                exit_code == 3U
                    ? NetworkIsolationCanaryStatus::denied_without_os_classification
                    : NetworkIsolationCanaryStatus::non_loopback_not_denied,
                true, ipv6_allowed, false, exit_code);
        }
        return result(NetworkIsolationCanaryStatus::success,
                      true, ipv6_allowed, true, 0U);
    } catch (...) {
        return result(
            NetworkIsolationCanaryStatus::isolation_canary_unavailable,
            false, false, false, ERROR_NOT_ENOUGH_MEMORY);
    }
}

std::string_view to_string(const NetworkIsolationErrorCode code) noexcept
{
    switch (code) {
    case NetworkIsolationErrorCode::none: return "none";
    case NetworkIsolationErrorCode::unsupported_platform: return "unsupported-platform";
    case NetworkIsolationErrorCode::empty_application_set: return "empty-application-set";
    case NetworkIsolationErrorCode::too_many_applications: return "too-many-applications";
    case NetworkIsolationErrorCode::duplicate_application: return "duplicate-application";
    case NetworkIsolationErrorCode::stale_application_identity: return "stale-application-identity";
    case NetworkIsolationErrorCode::invalid_application_id: return "invalid-application-id";
    case NetworkIsolationErrorCode::policy_not_fail_closed: return "policy-not-fail-closed";
    case NetworkIsolationErrorCode::privilege_required: return "network-isolation-privilege-required";
    case NetworkIsolationErrorCode::wfp_unavailable: return "wfp-unavailable";
    case NetworkIsolationErrorCode::dynamic_session_failed: return "dynamic-session-failed";
    case NetworkIsolationErrorCode::provider_failed: return "provider-failed";
    case NetworkIsolationErrorCode::sublayer_failed: return "sublayer-failed";
    case NetworkIsolationErrorCode::transaction_failed: return "transaction-failed";
    case NetworkIsolationErrorCode::filter_failed: return "filter-failed";
    case NetworkIsolationErrorCode::commit_failed: return "commit-failed";
    case NetworkIsolationErrorCode::canary_unavailable: return "isolation-canary-unavailable";
    case NetworkIsolationErrorCode::canary_loopback_failed: return "canary-loopback-failed";
    case NetworkIsolationErrorCode::canary_non_loopback_not_denied: return "canary-nonloopback-not-denied";
    case NetworkIsolationErrorCode::canary_denial_not_os_classified: return "canary-denial-not-os-classified";
    }
    return "unknown";
}

std::string_view to_string(const NetworkIsolationCanaryStatus status) noexcept
{
    switch (status) {
    case NetworkIsolationCanaryStatus::success: return "success";
    case NetworkIsolationCanaryStatus::ipv6_capability_unavailable: return "ipv6-capability-unavailable";
    case NetworkIsolationCanaryStatus::isolation_canary_unavailable: return "isolation-canary-unavailable";
    case NetworkIsolationCanaryStatus::loopback_failed: return "loopback-failed";
    case NetworkIsolationCanaryStatus::non_loopback_not_denied: return "nonloopback-not-denied";
    case NetworkIsolationCanaryStatus::denied_without_os_classification: return "denied-without-os-classification";
    }
    return "unknown";
}

} // namespace hlclient::platform::windows
