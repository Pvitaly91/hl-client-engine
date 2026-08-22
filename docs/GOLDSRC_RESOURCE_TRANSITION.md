# GoldSrc resource-transition boundary

M3.1.1 implements the exact client transition request, its persistent reliable
lifecycle, the bounded second service transfer, strict opcode-45 control, and
an exact neutral stop at numeric opcode 43. It deliberately does not parse or
name the opcode-43 body as a resource list.

Status: completed for the bounded transition and neutral opcode-43 boundary
scope. The resource-list semantic/body gate is not passed and remains M3.1.2.

## Evidence and compatibility profile

Primary evidence is a private, byte-preserving IPv4-loopback relay between
signed Valve `hl.exe` (`VERSIONINFO 1.1.1.1`, Steam App 70 build 15961492) and
signed Valve HLDS (`VERSIONINFO 4.1.1.1`, observed engine profile 1.1.2.2,
Protocol 48/build 10210). It used one bounded upstream endpoint and kept raw
packets, decompressed service bodies, private user IDs, opaque fields,
authentication bytes, and game data under ignored
`manual-artifacts/resource-transition-captures/`.

The accepted evidence set includes:

- six clean single-client baselines;
- two first-batch sessions each for `maxplayers 1` and `maxplayers 8`;
- two independently RCON-correlated same-HLDS reconnect/overlap sessions;
- two drop-first-transition-request runs;
- two drop-first-covering-ACK runs;
- two duplicate-transition-datagram runs;
- at least two accepted transitions each for `boot_camp` and `crossfire`.

Rejected/incomplete user-info differentials are recorded in
[GoldSrc user info](GOLDSRC_USERINFO.md) and do not count. Stock evidence and
deterministic project-to-fake-HLDS tests are reported separately; a production
Steam authentication provider and live project-client-to-stock-HLDS sign-on
remain unavailable.

The tracked verifier projects only bounded metadata into the ignored evidence
directory, then validates that exact projection root:

```powershell
pwsh -File .\scripts\verify_stock_resource_transition.ps1 -ProjectEvidenceSet
pwsh -File .\scripts\verify_stock_resource_transition.ps1 `
  -ValidateMetadataSetRoot `
  .\manual-artifacts\resource-transition-captures\projections
```

The frozen sanitized summaries are:

```text
projection-valid accepted=22 transitions=18 rejected-controls=11 incomplete=2 opcode13=user-info-update request=sendres opcode45=8B opcode43=neutral-unconsumed
metadata-valid accepted=22 transitions=18 rejected-controls=11 incomplete=2 opcode13=user-info-update request=sendres opcode45=8B opcode43=neutral-unconsumed
```

Both parameter sets passed under PowerShell 7 and Windows PowerShell 5.1. The
frozen tracked verifier SHA-256 is
`E1D3E0A4139A56185CF1AC5CA680D88FE80A257D6BA4A309AFF38EF3766F3423`.

The 22 accepted sources include four first-batch-only maxplayers sessions and
18 complete transitions. Eleven transport-complete control attempts are
explicitly rejected because their requested values were not observed, and two
runs are marked incomplete. Neither the projection nor the verifier output
contains raw user-info values, user IDs, opaque fields, or service bodies.

## Exact client request

The independently captured semantic request is exactly:

```text
03 73 65 6E 64 72 65 73 00
```

Its grammar is:

```text
u8       client string-command opcode = 3
char[7]  lowercase ASCII "sendres"
u8       one NUL terminator
```

It is 9 bytes, has no padding, and has SHA-256
`1A9D246FF6AA7E401524881AEC9414F76A91AA6166BEF43D67DA0C0F9DFBC035`.
Stock places this semantic prefix at offset zero of a 37-byte reliable body;
the following 28 contemporaneous bytes are separate messages, not request
padding or an undocumented request tail.

`ResourceTransitionRequestBuilder` therefore has no command/string parameter
and can build only this owning fixed request. Its parser consumes exactly nine
bytes from an exact stream cursor and leaves any following reliable-message
bytes untouched. A duplicate NUL or other trailing byte is not absorbed as
padding. No generic arbitrary `stringcmd` API or `--sendres` CLI exists.

## Reliable lifecycle

`ResourceTransitionStage` starts only after `UserInfoSignonStage` proves exact
first-batch completion. It retains the same bound socket, exact local/remote
endpoints, `NetchanDriver`, session, and optional authentication lifetime. It
builds and queues the typed request exactly once. Retransmission, reliable
generation, sequence advancement, and covering-ACK recognition remain owned
by the existing M2.3.2/M2.3.3 driver; the stage has no second retry timer and
does not queue a second semantic request after loss.

The controlled stock perturbations established:

- when the first request datagram is dropped, the same 9-byte semantic prefix
  is later retransmitted by transport and triggers one second transfer;
- when the first covering ACK packet is dropped, the next covering ACK
  completes the request without a second semantic queue operation;
- forwarding the same transition datagram twice still produces exactly one
  second transfer.

The project stage observes the typed driver event
`reliable_payload_acknowledged` only after the request became in flight. A
single owning server payload may arrive before that event and is held until
the ACK; a second pre-ACK payload is a bounded protocol error. Stale/future
ACK behavior stays behind the driver's wrap-safe acknowledgement contract.

## Second service transfer

Every checked stock transition used six normal fragments and a `BZ2\0`
envelope followed by a standard BZip2 stream. The bounded envelope decoder
reassembles/decompresses entirely in memory and records only source metadata.
Representative accepted sizes were:

| Profile | Compressed bytes | Decompressed bytes |
| --- | ---: | ---: |
| fresh `boot_camp` | 5,225 | 10,713 |
| fresh `crossfire` | 6,008 | 12,169 |
| fresh `stalkyard`/night | 5,330 | 10,815 |
| same-process second map observation | 5,239 | 10,713 |

All begin with opcode 45. These are observed profile sizes, not universal
buffer constants. The stage validates the envelope against the configured
second-payload bound and never writes decompressed bytes to disk.

## Exact opcode-45 control

The confirmed fixed control is:

```text
u8     opcode = 45                       message offset 0
u32le  private opaque value              body offset 0 / message offset 1
u32le  required zero                     body offset 4 / message offset 5
u8     exact next opcode = 43            message offset 9, unconsumed
byte[] opcode-43 body                     wholly unconsumed
```

The control body is exactly 8 bytes and the parser consumes exactly 9 bytes
including opcode 45. The first `u32le` was 1 for fresh server/map starts and 2
for the accepted same-process second-map observations. That correlation does
not establish a safe public name, so `ResourceTransitionControlState` stores
the value privately and exposes no getter. Its configured upper bound is a
project safety policy, not a stock maximum. The second `u32le` was zero across
the earlier 28 transition projections and the new accepted runs; the strict
parser rejects any nonzero value.

The parser validates opcode 43 only at the exact byte immediately after the
fixed control. It requires the boundary to retain at least one body byte, but
does not consume that opcode, read a body length/count, or scan for a later
candidate. Success reports `bytes_consumed == 9` and a next cursor that still
points at 43.

## Why the boundary remains neutral

Stock behavior strongly places numeric opcode 43 after the exact transition
request, and its untouched body size varies with the map. Baseline,
`crossfire`, and night profiles retained respectively 10,703, 12,159, and
10,805 bytes after the boundary opcode. The pinned public Valve `custom.h`
contains resource-related structures/functions, but no numeric service-opcode
mapping that independently equates 43 with a resource-list message.

The project evidence rule requires that independent numeric/header mapping
before the public semantic name `ResourceListBoundary` is allowed. The gate
therefore is **not passed**. Production exposes only `Opcode43Boundary`,
containing the numeric opcode, exact offset, remaining byte count, source
payload size, and compatibility/evidence profile. `ResourceTransitionState`
and the terminal stage state likewise use
`neutral_opcode43_boundary_reached`.

The CLI spelling `--stop-after resource-list-boundary` selects this exact
historical/product stop point; it does not upgrade the typed boundary to a
resource-list semantic claim. At success, opcode 43 is validated at the exact
cursor and remains unconsumed; its body remains wholly unread and unparsed, no
resource count or entry exists, and the client sends no response.

## Owning state, events, and isolation

`ResourceTransitionState` owns:

- the complete preceding `UserInfoSignonState`;
- the exact typed request metadata/bytes;
- private-field `ResourceTransitionControlState`;
- neutral `Opcode43Boundary`;
- compressed/decompressed size, source sequence/ACK/reliability,
  reassembly/decompression, direction, acknowledgement-reliability, and
  receive-time metadata for the second payload.

It owns no raw payload, resource count, entry, filename, flag, checksum, path,
file, download, cache, precache handle, renderer object, or `AssetManager`.

The stage publishes bounded metadata-only events for queue, transmit, ACK,
second transfer, decoded control, and neutral boundary. Trace callbacks cannot
access the request command string, user-info values, private control field,
raw service body, authentication bytes, or resource bytes. All success/error,
timeout, cancellation, secondary-stream, backpressure, and destruction paths
release the retained driver/authentication lifetime exactly once.

The explicit route is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after resource-list-boundary `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

The earlier `user-info` stop never queues this request. All earlier M1–M2.4.4
stop points preserve their historical network behavior.

## Project safety limits

| Limit | Default | Hard cap |
| --- | ---: | ---: |
| resource-transition request message | 9 bytes | 9 bytes |
| opcode-45 control message (opcode plus body) | 9 bytes | 9 bytes |
| private opaque `u32` upper bound | 1,000,000 | 4,294,967,295 |
| decompressed second service payload | 65,536 bytes | 1,048,576 bytes |
| stage events | 64 | 256 |
| driver events consumed per update | 32 | 256 |

The fixed message limits must equal nine. The opaque limit must be positive
and no larger than `UINT32_MAX`; the parser accepts a value equal to the
configured bound and rejects bound plus one. These values are project safety
limits, not claims about stock engine capacities.

## Negative tests and CI

Request tests cover an independent exact fixture, opcode and lowercase command
bytes, one terminator, missing/early terminators, every truncation prefix,
fixed size-limit validation, ownership, exact cursor advancement, unconsumed
duplicate/trailing bytes, round-trip construction, and compile-time absence of
an arbitrary command/raw-string API.

Control tests cover every truncated complete-payload prefix, wrong opcode,
little-endian decoding, configured limit and limit plus one, hard-cap
configuration, nonzero reserved field, missing/wrong/truncated opcode-43
boundary, exact consumed/next offsets, no forward scanning, ownership, and
absence of public opaque getters. Stage/integration tests cover exact request
queue-once behavior, a deterministic reliable-queue rejection before
`sendres`, wrong endpoint, an unknown opcode before the boundary, truncated
opcode 45, and an inserted trailing byte before opcode 43 without partial
publication. The fake-HLDS path additionally proves malformed inbound
user-info fails atomically before the transition request. Deterministic 20/20
sets cover baseline, dropped-request retransmission, fragmented second
transfer, and repeated user-info messages.

The complete Win32 acceptance commands are:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 `
  -DHLCLIENT_WARNINGS_AS_ERRORS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Focused codec/stage tests can be run after building:

```powershell
.\build\bin\Debug\hlclient_tests.exe "[resource-transition]"
```

GitHub Actions configures `vs2022-win32`, builds/tests Debug with warnings as
errors, and runs version/help/null-renderer smoke checks. Automated tests need
no stock binaries, Steam, real authentication, public server, GPU, Internet,
or installed game assets.

## Explicitly absent and next milestone

M3.1.1 adds no opcode-43 body parser, resource count, entries, filenames,
flags, hashes, consistency response, filesystem lookup, VFS mount, download,
cache, precache, map load, asset load, renderer work, or graphics change. It
does not expose raw user-info or received resource-body bytes. The only public
request bytes are the fixed typed nine-byte message, and no CLI can inject an
arbitrary transition command.

The next milestone is M3.1.2: independently establish the opcode-43 body and
numeric semantic gate, then implement a bounded owning codec. M3.2 local
resolution and M3.3 safe download/cache remain later work.
