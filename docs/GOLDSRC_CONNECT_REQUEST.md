# GoldSrc Protocol 48 connect request

## M2.1 scope

M2.1 builds the exact captured stock Half-Life `connect` datagram from the M1
challenge and sends it once on the same UDP transport. It stops immediately
after that send. It does not generate Steam authentication, interpret the
server's answer, create a netchan, send sequenced traffic, or enter sign-on.

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
string/stream conversion, and is readable only by the connect codec. M2.1 does
not implement `IAuthenticationProvider`, Steamworks, ticket generation,
SteamID construction, reuse, or bypass. The future boundary is:

```text
IAuthenticationProvider -> AuthenticationMaterial -> ConnectRequestBuilder
```

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
```

Timeout, cancellation, configuration, network, and protocol errors are
terminal. Static user/auth configuration is prepared before any network send.
The stage makes exactly one connect `send_to` call; success or error is
terminal, with no retry and no receive/parser after it. Later updates cannot
send a duplicate, netchan, sign-on, disconnect, or resource packet.

`--stop-after challenge` is the default and preserves M1. Explicit
`--stop-after connect-request` requires `--auth-material-file`, sends once, and
reports only transmission—not acceptance or connection.

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

During clean-room discovery the unmodified stock `hl.exe` transmitted the
captured connect request through the relay and stock HLDS returned a
connectionless class `B` datagram. An earlier bounded observation then saw
sequenced traffic, but M2.1 does not interpret or reproduce either response.
That discovery exchange establishes the stock-client request layout; it is not
a transmission proof for this project's `hlclient` executable. Therefore:

- stock `hl.exe` -> stock HLDS capture transmission: observed;
- project `hlclient` -> fake HLDS transmission: passed deterministically;
- project `hlclient` -> stock HLDS transmission: pending because no production
  authentication provider or explicitly supplied legitimate material was used;
- stock-server rejection: not determined;
- stock-server acceptance: not determined;
- stock-server response interpretation: not implemented.

M2.2 owns connectionless accept/reject and the authentication-provider
boundary. M2.3 owns netchan sequencing/acknowledgements. M2.4 owns initial
sign-on.
