# Steam authentication for a GoldSrc connection

## Selected API family

The stock Protocol 48 route uses the legacy Steamworks game-connection
authentication family:

```text
ISteamUser::InitiateGameConnection
    -> opaque authentication bytes in the GoldSrc connect datagram
    -> server-side ISteamGameServer::SendUserConnectAndAuthenticate
ISteamUser::TerminateGameConnection when the connection lifetime ends
```

This is an endpoint-bound three-party flow. The client call receives the game
server SteamID, IPv4 address and port in host order, and the server's secure
flag. Valve requires an output buffer of at least 2,048 bytes and requires the
matching terminate call. The server-side API explicitly consumes material
created by `InitiateGameConnection` and may later report approval or denial.
Consequently a connectionless GoldSrc `ACCEPT` is not promoted to completed
Steam authentication.

`GetAuthSessionTicket`/`CancelAuthTicket` is a different API family paired
with `BeginAuthSession`; Web API tickets are different again. Neither is used
or mixed into this route. Primary API references are Valve's
[ISteamUser documentation](https://partner.steamgames.com/doc/api/ISteamUser#InitiateGameConnection),
[ISteamGameServer documentation](https://partner.steamgames.com/doc/api/ISteamGameServer#SendUserConnectAndAuthenticate),
and the official Valve Proton copy of the
[Steamworks SDK 1.53a flat ABI](https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamworks_sdk_153a/steam_api_flat.h).

The GoldSrc wire/profile cross-checks are source references, not imported
engine code. ReHLDS revision
`6266cd23faee4a6e9cf3974f9605b2cadd86f0a4` shows the server pairing and
callback distinction in
[`sv_steam3.cpp`](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_steam3.cpp).
The GoldSrc-compatible path in Xash3D revision
`e9b63241616c390d4d1720ced80e88de2acf83a6` independently shows the
`prot=3`, `unique=-1`, `raw=steam`, fresh decimal-nonce MD5 `cdkey`, and
actual-length ticket append in
[`cl_main.c`](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/cl_main.c).
No function or source file from either engine is copied into this repository.

## Optional runtime integration

`SteamAuthenticationProvider` implements the existing
`IAuthenticationProvider`/`IAuthenticationOperation` contract. It is composed
only for explicit `--auth-provider steam`; offline decoding, replay, and all
default modes neither load nor initialize Steam. The user must pass one
absolute `--steam-api-runtime` path. There is no registry/current-directory
search, file-provider fallback, original-`hl.exe` helper, credential input,
ticket file, or retained auth artifact.

On Windows the provider loads that explicit DLL with restricted dependency
search, resolves the exact flat exports for `ISteamUser` v021 and
`ISteamUtils` v010, initializes Steam, requires App ID 70 and a logged-on
session, and pumps callbacks on the application update thread. A new
connection begins a new operation after the fresh challenge supplies the
server SteamID and secure flag. The operation has a bounded deadline and
typed unavailable/configuration/provider/invalid/oversize/timeout/cancel
outcomes.

The Steam call writes into a 2,048-byte temporary buffer. The serializer uses
the returned length without padding or truncation and rejects empty material
or anything above the reviewed 1,023-byte GoldSrc profile bound. The overall
connect datagram still has its 1,400-byte project bound. The historical file
profile remains exactly 32 protected bytes plus 213 suffix bytes and is not a
Steam ticket template.

The successful operation returns an owning `AuthenticationSession`. After its
material is moved once into the request, its lifetime guard remains behind the
same coordinator, `NetchanDriver`, and sign-on stages. Destruction invokes the
matching `TerminateGameConnection` exactly once; shared provider teardown
invokes `SteamAPI_Shutdown` exactly once. Diagnostics contain only status,
API family, bounded error category, material size, and lifecycle status.

## Capability observed on the development host

On 2026-09-20 the available official x86 runtime was:

```text
D:\Steam\steamapps\common\Half-Life\steam_api.dll
file version: 06.91.21.57
size: 263080 bytes
SHA-256: 1AC139EBAD2A653ADFF5700347274CF9816256EB5D69AE6DC43C4CF9C8532AA7
Authenticode: Valid, Valve Corp.
```

Its exports include `SteamAPI_Init`, `SteamAPI_Shutdown`,
`SteamAPI_RunCallbacks`, `SteamAPI_SteamUser_v021`,
`SteamAPI_SteamUtils_v010`, `SteamAPI_ISteamUser_BLoggedOn`,
`SteamAPI_ISteamUtils_GetAppID`, and the matching deprecated flat
initiate/terminate calls. The provider is dynamically integrated; no
Steamworks header, import library, DLL, or other proprietary runtime file is
copied into the source tree or required at link time.

The local development `steam_appid.txt` contains `70` only beside the project
build executable. The primary Steam installation is not modified, App ID 480
is rejected, and restart-through-Steam is not invoked.

## Current live verification status

M4.7.2C live verification is
`fresh_project_client_stock_signon_verified`. Historical runs
`eb1e1ad86b01467eb6be2fffb2297767` and
`5a84bb9f5fe149d98228dc0443788079` remain launch-boundary results and are not
reclassified as Steam rejection.

The continuation used two newly authorized managed runs:

1. `a5b9e103be984adda786d4da6b6b5212` proved exact suspended image identity,
   resume, retained-handle wait, exit `0xC0000142`, bounded empty child output,
   exact owned cleanup, and exact restoration. Application entry and Steam were
   not reached. The concrete launch-spec defect was the incompatible pairing of
   `CREATE_NO_WINDOW` with the restricted-child process policy; the production
   spec no longer requests that combination and the common launch seam rejects
   it.
2. `bb2a8457b33c44538f4ec5951d7c44e6` launched owned stock HLDS and the exact
   project client on `127.0.0.1:27243`, `valve/boot_camp`. The project client
   exited normally at its selected delta-schema stop and both cleanup and
   wrapper restoration were exact.

The successful run recorded:

```text
application_entry_observed=succeeded
arguments_accepted=succeeded
provider_begin_observed=succeeded
steam_api_init_attempted=succeeded
steam_api_initialized=true
fresh_material_acquired=true
connect_sent=true
connection_accepted=true
serverinfo_received=true
schema_registry_received=true
authentication_status=pending_or_unknown
last_confirmed_stage=delta_schemas_ready
client_exit_code=0x00000000
```

ServerInfo reported Protocol 48, maximum clients 8, game `valve`, and map
`maps/boot_camp.bsp`. This fresh session completed a registry of 7 schemas and
219 fields; those counts are observations, not protocol constants. A
connectionless ACCEPT and completed initial sign-on still do not expose the
server's later approve/deny callback, so the authentication result remains
`pending_or_unknown` rather than guessed success.

The live client SHA-256 was
`31405E442E863C30FE99607C722DD1910578824A5C19320A0E4F8F59B5E5F3E2` and
the live orchestrator SHA-256 was
`00CE2416246F19B50F21DFAD9D66772C657C13052747FCBE860E03876879AEF9`.
No original `hl.exe`, captured authentication material, packet capture, ETW,
or strict campaign was used. Retained diagnostics redact authentication lines;
no ticket bytes, identifiers, hashes, or full connect datagram were persisted.

M4.7.2D kept that provider and lifetime unchanged. Its final authorized run
`f205835da9944e8bbab06c2750539772` again initialized the API, acquired fresh
material, sent a new connect and received ACCEPT. Authentication remains
`pending_or_unknown`, because no later server approve/deny callback was exposed
before initial sign-on stopped. The observed blocker was instead a strict
capture-envelope assumption: a legitimate wire-uncompressed normal-fragment
service payload was rejected before ServerInfo. The live-runtime composition
now accepts either bounded `BZ2\0` or bounded wire-uncompressed service bytes;
capture/replay and `delta-schemas` remain strict. That post-live fix is built
and regression-tested but was not run against stock HLDS because D's two-run
budget was exhausted.
