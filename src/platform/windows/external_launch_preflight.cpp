#include <hlclient/platform/windows/external_launch_preflight.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <winsock2.h>
#include <windows.h>
#include <fwpmu.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>

namespace hlclient::platform::windows {
bool is_research_application_block(std::wstring_view name, bool outbound_v4,
    bool blocking, std::span<const std::byte> app, std::span<const std::byte> expected) noexcept
{
    return name == L"HLClient temporary non-loopback block" && outbound_v4 && blocking &&
        !expected.empty() && std::equal(app.begin(), app.end(), expected.begin(), expected.end());
}

ResearchIsolationObservation observe_research_isolation_conflict(
    const std::filesystem::path& executable) noexcept
{
    struct Cleanup {
        HANDLE engine{}, enumeration{};
        FWP_BYTE_BLOB* app{};
        ~Cleanup() {
            if (enumeration) ::FwpmFilterDestroyEnumHandle0(engine, enumeration);
            if (engine) ::FwpmEngineClose0(engine);
            if (app) ::FwpmFreeMemory0(reinterpret_cast<void**>(&app));
        }
    } owned;
    if (!executable.is_absolute()) return {ResearchIsolationConflict::unavailable, ERROR_INVALID_NAME};
    DWORD error = ::FwpmGetAppIdFromFileName0(executable.c_str(), &owned.app);
    if (error != ERROR_SUCCESS || !owned.app || !owned.app->data || !owned.app->size)
        return {ResearchIsolationConflict::unavailable, error};
    error = ::FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &owned.engine);
    if (error != ERROR_SUCCESS) return {ResearchIsolationConflict::unavailable, error};
    FWPM_FILTER_ENUM_TEMPLATE0 query{};
    query.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    query.enumType = FWP_FILTER_ENUM_FULLY_CONTAINED;
    query.actionMask = FWP_ACTION_BLOCK;
    error = ::FwpmFilterCreateEnumHandle0(owned.engine, &query, &owned.enumeration);
    if (error != ERROR_SUCCESS) return {ResearchIsolationConflict::unavailable, error};
    constexpr UINT32 page_size = 128U;
    for (unsigned page = 0; page < 128U; ++page) {
        FWPM_FILTER0** filters{};
        UINT32 count{};
        error = ::FwpmFilterEnum0(owned.engine, owned.enumeration, page_size, &filters, &count);
        if (error != ERROR_SUCCESS) return {ResearchIsolationConflict::unavailable, error};
        bool conflict{};
        for (UINT32 n = 0; n < count && !conflict; ++n) {
            const auto* filter = filters[n];
            if (!filter || !filter->displayData.name || filter->numFilterConditions > 64U) continue;
            const auto name_size = wcsnlen_s(filter->displayData.name, 128U);
            if (name_size >= 128U) continue;
            for (UINT32 c = 0; c < filter->numFilterConditions; ++c) {
                const auto& condition = filter->filterCondition[c];
                if (condition.fieldKey != FWPM_CONDITION_ALE_APP_ID || condition.matchType != FWP_MATCH_EQUAL ||
                    condition.conditionValue.type != FWP_BYTE_BLOB_TYPE || !condition.conditionValue.byteBlob) continue;
                const auto& app = *condition.conditionValue.byteBlob;
                if (!app.data || app.size > 64U*1024U) continue;
                conflict = is_research_application_block({filter->displayData.name, name_size},
                    filter->layerKey == FWPM_LAYER_ALE_AUTH_CONNECT_V4, filter->action.type == FWP_ACTION_BLOCK,
                    {reinterpret_cast<const std::byte*>(app.data), app.size},
                    {reinterpret_cast<const std::byte*>(owned.app->data), owned.app->size});
                if (conflict) break;
            }
        }
        if (filters) ::FwpmFreeMemory0(reinterpret_cast<void**>(&filters));
        if (conflict) return {ResearchIsolationConflict::present, 0};
        if (count < page_size) return {ResearchIsolationConflict::absent, 0};
    }
    return {ResearchIsolationConflict::unavailable, ERROR_MORE_DATA};
}

OfficialSteamRuntimeObservation observe_official_steam_runtime(const std::filesystem::path& runtime) noexcept
{
    OfficialSteamRuntimeObservation result;
    result.binary = observe_windows_binary_identity(runtime);
    if (!result.binary || result.binary.identity->pe_machine != WindowsPeMachine::x86) return result;
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = result.binary.identity->canonical_path.c_str();
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    if (::WinVerifyTrust(nullptr, &policy, &trust) == ERROR_SUCCESS) {
        const auto* provider = ::WTHelperProvDataFromStateData(trust.hWVTStateData);
        auto* signer = provider ? ::WTHelperGetProvSignerFromChain(
            const_cast<CRYPT_PROVIDER_DATA*>(provider), 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain && signer->pasCertChain[0].pCert) {
            std::array<wchar_t, 256> name{};
            const auto size = ::CertGetNameStringW(signer->pasCertChain[0].pCert,
                CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name.data(), static_cast<DWORD>(name.size()));
            result.valve_signer = size > 1 && size < name.size() &&
                std::wstring_view{name.data()} == L"Valve Corp.";
        }
    }
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    ::WinVerifyTrust(nullptr, &policy, &trust);
    // Retain same-file identity after signer inspection; the observer validates
    // stable bytes itself. No network/auth call or loaded game DLL is involved.
    if (result.valve_signer) {
        const auto after = observe_windows_binary_identity(runtime);
        if (!after || !same_windows_file_identity(*result.binary.identity, *after.identity) ||
            result.binary.identity->sha256 != after.identity->sha256) result.valve_signer = false;
    }
    return result;
}
} // namespace hlclient::platform::windows
