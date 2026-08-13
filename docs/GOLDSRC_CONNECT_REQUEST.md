# GoldSrc Protocol 48 connect request

## M2.1 scope

M2.1 builds the exact captured stock Half-Life `connect` datagram from the M1
challenge and sends it once on the same UDP transport. Its own stop point still
ends immediately after that send. M2.2 optionally keeps the same transport open
for one bounded connectionless `ACCEPT` or `REJECT`; see
[Connect response](GOLDSRC_CONNECT_RESPONSE.md). Neither mode generates Steam
authentication, creates a netchan, sends sequenced traffic, or enters sign-on.

The compatibility reference is a signed Valve `hl.exe` 1.1.1.1 (Steam App 70,
build ID 15961492) communicating through a transparent loopback relay with a
stock Valve HLDS Protocol 48, executable 1.1.2.2, build 10210. The relay used
one client-facing socket and one unchanged server-facing socket for the whole
exchange. Both `getchallenge` and `connect` therefore used one upstream source
endpoint. It forwarded bytes unchanged, capped packet count/size/runtime, and
stopped after the first post-connect connectionless reply.

Raw captures and logs containing authentication or identity material remain
only below ignored `manual-artifacts/connect-captures/`. They are not source,
test data, documentation, CI artifacts, or Git objects.

## Confirmed wire layout

The exact redacted layout is:

```text
FF FF FF FF
connect SP 48 SP <signed-decimal-challenge> SP
"<protocol-info>" SP "<user-info>"<AUTH_BINARY_REDACTED:length=213>
```

There are exactly four ASCII spaces in fixed syntax. Only the two info strings
are quoted. The binary authentication suffix starts immediately after the
user-info closing quote: there is no separating space or length field. There
is no packet terminator at all—no LF, NUL, or hidden trailing byte. The final
authentication byte is the final datagram byte.

Observed requests were 488, 489, or 490 bytes as the decimal challenge width
changed. The observed fixed-size relation for the captured 66-byte protocol
info, 179-byte user info, and 213-byte binary suffix is:

```text
total = 479 + signed-decimal challenge length
```

The project uses a conservative 1,400-byte hard datagram bound. That is a
project safety limit, not a claim that stock GoldSrc accepts every size up to
it. The largest observed stock request was 490 bytes.

HLDS transmits the M1 token as canonical unsigned decimal `0..UINT32_MAX`.
Stock `hl.exe` preserves those 32 bits but formats them as signed `int32`
decimal in `connect`; values above `INT32_MAX` therefore appear negative. The
codec uses an explicit bit-preserving conversion and tests both sides of that
boundary.

## Captured info profiles

Both info strings use this grammar:

```text
info-string = *( "\\" key "\\" value )
```

They have a leading separator and no trailing separator. Ordered vectors are
the canonical representation, so serialization is stable. The production
builder rejects exact and ASCII-case-folded duplicate keys. That stricter
case-collision rule is a project security policy; one normal stock capture
does not establish server-side case semantics.

The 66-byte protocol-info order is:

```text
prot=3
unique=-1
raw=steam
cdkey=<AUTH_PROTECTED_REDACTED:length=32>
```

The captured user-info was 179 bytes with a redacted four-byte identity. Its
field order and the deterministic project defaults used by the current stock
profile are:

```text
bottomcolor=6
cl_autowepswitch=1
cl_dlmax=1024
cl_lc=1
cl_lw=1
cl_updaterate=102
hud_classautokill=1
model=ivan
name=Player
topcolor=30
esevcmmx=0
_gm=3154
_vgui_menus=0
rate=25000
```

`esevcmmx` and `_gm` remain opaque captured profile field names. No semantics
are inferred. All 14 entries are required, in this exact order, by the current
captured stock profile. M2.1 defines no optional user-info entries; additional
or reordered entries are rejected and remain future compatibility work.
Defaults reproduce the safe non-identity values seen in the capture, while
`name` defaults to `Player`; `--name` and `--model` provide only the two
identity overrides. Each override is limited to 31 printable ASCII bytes and
passes the same structural-character validation as every info value. The
attempted stock command-line override probe did not alter those slots, so that
test did not establish stock CLI semantics; the project options are its own
composition inputs.

The reusable info-string codec has absolute implementation ceilings of 127
bytes per key, 511 bytes per value, 64 entries, and 1,023 serialized bytes.
Those are allocation-safety ceilings, not the limits selected for a connect
request. The connect profile limits each key to 63 bytes, each value to 127
bytes, and each serialized protocol/user info string to 255 bytes; protocol
info permits at most 16 entries and user info at most 32. The 255-byte user-info
bound is supported by the official pinned HLSDK `MAX_INFO_STRING` declaration
(256-byte storage including terminator). The other key/value/count caps are
conservative project safety bounds, not inferred stock acceptance maxima.

Empty info strings are representable by the generic codec, but empty keys and
values are rejected. Keys are printable ASCII without spaces. Values may
contain printable ASCII spaces. Both reject backslash, double quote,
semicolon, NUL, CR, LF, all other control bytes, DEL, and non-ASCII bytes. The
codec performs no escaping, locale conversion, normalization, or repair.

## Authentication boundary

`AuthenticationMaterial` owns two opaque regions:

- the captured protected info value (`cdkey` slot), observed as 32 printable
  ASCII-hex bytes;
- the immediately appended raw binary suffix, observed as 213 bytes.

The type is move-only, exposes sizes and boolean comparison only, has no
string/stream conversion, and is readable only by the connect codec. M2.2 adds
the provider boundary around acquisition:

```text
IAuthenticationProvider
    -> IAuthenticationOperation
    -> AuthenticationSession
    -> AuthenticationMaterial
    -> ConnectRequestBuilder
```

The only concrete implementation is an explicit user-file provider for
development/manual runs. It is not Steamworks: there is no ticket generation,
SteamID construction, discovery, cache, reuse policy, or bypass. The move-only
session can retain a provider-specific lifetime guard after material is
transferred into request preparation; the application keeps that guard through
the configured terminal handshake state. See
[Authentication provider](AUTHENTICATION_PROVIDER.md) for the async contract,
lifetime requirements, and sensitive-data limitations.

For explicit development runs, `--auth-material-file` reads one local 245-byte
file: the first 32 bytes feed the protected slot and the remaining 213 bytes
are the binary suffix. The path is not persisted, the file is not copied into
the build output, and contents never enter logs or error messages. The loader
copies the bounded record only into the owning in-memory authentication type.
Do not commit such a file.

The owning authentication type additionally enforces absolute implementation
ceilings of 127 protected bytes and 1,200 binary-suffix bytes. These permit
bounded synthetic/profile tests but do not relax the production stock profile:
that profile requires exactly 32 protected ASCII-hex bytes and exactly 213
binary suffix bytes. The complete encoded connect datagram is always capped at
1,400 bytes; 490 bytes remains the largest observed stock request.

Tests use only the explicit synthetic `TEST_AUTH_MATERIAL` marker (including
bounded repetitions where the captured profile requires a fixed length). Their
independent fixture reproduces the confirmed packet structure but contains no
captured authentication or identity material.

## State and one-shot policy

The M1 `ChallengeExchange` remains a separate completed component. A
`GoldSrcHandshakeCoordinator` advances it and then, only in explicit connect
mode, starts `ConnectRequestStage` on the same `IDatagramTransport`, local
endpoint, and remote endpoint:

```text
idle -> waiting_for_challenge -> challenge_received
       -> building_request -> request_ready -> sending_request -> request_sent
       -> waiting_for_connect_response -> accepted | rejected | timeout | error
```

Timeout, cancellation, configuration, network, and protocol errors are
terminal. Static user/auth configuration is prepared before any network send.
The request stage makes exactly one connect `send_to` call, with no retry.
At the M2.1 stop point, send success or error is terminal. At the explicit M2.2
stop point, send success starts the separate receive-only response stage. Later
updates cannot send a duplicate, netchan, sign-on, disconnect, or resource
packet.

`--stop-after challenge` is the default and preserves M1. Explicit
`--stop-after connect-request` requires `--auth-material-file`, sends once, and
reports only transmission—not acceptance or connection. Explicit
`--stop-after connect-response` uses the same provider and request path, then
waits for the immediate bounded result; it still does not enter netchan/sign-on.

`--net-trace` for connect is metadata-only: endpoint, size, protocol,
challenge, field counts, info lengths, and redacted authentication length. It
never holds or formats the raw datagram, field values, password, or
authentication bytes; the current logger emits counts rather than the validated
field names carried by the trace event.

## Verification status

The loopback fake-HLDS integration test uses the production UDP transport. It
receives the exact 23-byte M1 request, returns a synthetic challenge, validates
one strict connect request from the same source address/port, and proves that
no third datagram follows. Deterministic fake-transport tests cover terminal
states and one-shot behavior without sleeping.

During clean-room discovery unmodified stock clients transmitted the captured
connect request through the relay and stock HLDS returned connectionless class
`B` datagrams. Separate bounded rejection probes recorded class `9` responses.
M2.2 now interprets those immediate layouts, while any following sequenced
traffic remains the explicit M2.3 boundary. These observations establish stock
wire behavior; they are not a stock-server transmission or acceptance proof for
this project's `hlclient` executable. Therefore:

- stock `hl.exe` -> stock HLDS capture transmission: observed;
- project `hlclient` -> fake HLDS transmission: passed deterministically;
- stock-HLDS immediate accept/reject layouts: observed and sanitized;
- project `hlclient` -> fake HLDS accept/reject handling: passed
  deterministically;
- project `hlclient` -> stock HLDS transmission or acceptance: not performed or
  claimed.

M2.2 connectionless accept/reject and the authentication-provider boundary are
complete. M2.3 owns netchan sequencing/acknowledgements. M2.4 owns initial
sign-on.
