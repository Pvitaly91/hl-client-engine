# GoldSrc opcode-13 user-info continuation

M3.1.1 continues from the exact opcode-13 `PostMoveVarsBoundary` retained by
M2.4.4. It decodes the complete initial sequence into immutable owning
metadata and proves the exact end of the first service batch. The public
`--stop-after user-info` route stops there and sends no resource-transition
request.

This is a bounded Protocol 48/build 10210 profile. It is not a runtime player
object, scoreboard, authentication record, filesystem input, or command
surface.

## Evidence and status

Primary evidence comes from private IPv4-loopback captures of signed Valve
`hl.exe` (`VERSIONINFO 1.1.1.1`, Steam App 70 build 15961492) against signed
Valve HLDS (`VERSIONINFO 4.1.1.1`, observed engine profile 1.1.2.2,
Protocol 48/build 10210). The relay preserved bytes, used one bounded upstream
socket, validated exact endpoints, and stored raw research artifacts only
under ignored `manual-artifacts/resource-transition-captures/`. Tracked code,
tests, logs, and documentation contain no real player values, user IDs, opaque
suffix bytes, authentication material, or raw opcode-13 bodies.

The accepted evidence includes six clean single-client baselines, two
`maxplayers 1` and two `maxplayers 8` first-batch sessions, and two
independently RCON-correlated same-HLDS reconnect/overlap sessions. The
single-client baselines contain one opcode-13 message. The overlap sessions
contain two consecutive messages with client indexes zero and one; the private
wire IDs advance by one and match the literal HLDS `status` user-ID column.
That correlation confirms the field category and width, but the raw values
remain private.

Attempts to control name, model, and color values did not make the requested
values appear and are not counted as differential evidence: six name attempts,
one model attempt, two top-color attempts, and two bottom-color attempts were
transport-complete. Two source key-order profiles were observed, so the corpus
does not establish one globally stable order; the decoder preserves the order
of each source message. A concurrent live second-client
startup failed before `CONNECT`, and one reconnect attempt was incomplete.
These rejected/incomplete attempts do not support semantic value claims.
Consequently the production API exposes presence and length metadata for
selected keys, never their values.

The pinned public Valve headers provide the independent field/category
cross-check used by `UserInfoUpdateEvidenceProfile::
stock_capture_and_public_valve_header`. They are not treated as a packed wire
layout; production decoding uses explicit byte reads.

## Exact opcode-13 grammar

One message has this exact profile:

```text
u8       opcode = 13                         message offset 0
u8       zero-based client index             body offset 0 / message offset 1
u32le    private positive user ID            body offset 1 / message offset 2
byte[]   ordered user-info byte string       body offset 5 / message offset 6
u8       NUL terminator                      after the user-info bytes
byte[16] private opaque binary suffix        immediately after the NUL
```

The message ends after the 16 opaque bytes. There is no padding. The exact
next cursor is either another opcode 13 or the end of the owning service
payload; the decoder never scans forward for a plausible opcode.

The common baseline starts at service-payload offset 7,273 and consumes 202
bytes including the opcode: 201 body bytes, a 179-byte info string excluding
its NUL, and the opaque suffix at message offset 186. The accepted overlap
profile contains messages at offsets 7,273 and 7,475 with sizes 202 and 189;
their info strings are 179 and 166 bytes. Both profiles end exactly at the end
of the first payload with no trailing byte or padding. These absolute offsets
are evidence observations, not parser constants.

The client index is an exposed `u8` routing value in the confirmed zero-based
range 0 through 31. The user ID is read as little-endian `u32`, restricted by
the supported profile to positive `int32` values, stored privately, and
represented publicly only by `has_private_user_id()`. The 16-byte field is
stored privately as opaque binary data. No digest, certificate, hash
algorithm, or authentication meaning is claimed; only
`opaque_suffix_size()` is public.

## Dedicated info-string profile

The opcode-13 string is decoded by a dedicated bounded profile rather than by
silently relaxing the connection-request `InfoString` rules:

- the first byte must be `\`;
- key and value segments alternate in captured order;
- at least one pair is required;
- keys and values must be nonempty;
- a final separator without a value is invalid;
- duplicate keys, including ASCII case collisions, are rejected;
- original byte order and case are preserved; there is no normalization or
  silent overwrite;
- the message-level NUL terminates the complete byte string and is not part of
  its reported length.

The clean baseline profile contains 13 ordered key/value entries. That count is
an observation, not a requirement imposed on other bounded messages.

Stock values were ASCII in the accepted corpus, and the maximum observed
info-string length was 179 bytes. That is an observation, not a character-set
or buffer maximum. The parser preserves non-NUL bytes and does not invent
quote, semicolon, locale, or UTF-8 restrictions not established by evidence.
Values remain inert metadata and never reach a command interpreter.

Exact lowercase keys `name`, `model`, `topcolor`, and `bottomcolor` produce
ordered `UserInfoSafeFieldMetadata` containing only the typed key category,
entry index, and value length. `player_name_length()` and
`player_model_length()` are optional length getters. The actual name, model,
color, protected/`*`-key, and unknown values are owning private bytes with no
raw getter. Key names and values are not logged by default.

## Typed state and exact first-batch completion

`UserInfoUpdateParser` parses exactly one complete message. Success returns an
owning `UserInfoUpdateState`, exact `bytes_consumed`, and the next byte offset;
failure publishes no candidate and reports zero consumed bytes. A suffix after
one exact message is rejected by this single-message API.

`UserInfoUpdateStreamDecoder` instead starts from the M2.4.4
`PostMoveVarsBoundary`, validates its opcode/offset/remaining-byte geometry,
and repeatedly consumes only consecutive opcode-13 messages. It preserves
message order, rejects a duplicate client index, enforces per-batch count and
total-byte bounds, and stops at exact payload end or before an exact non-13
opcode. A following opcode, when present in a synthetic/profile-extension
test, remains unconsumed.

`UserInfoFirstBatchCompletion` owns:

- the terminal condition (`exact_end_of_payload` or `following_opcode`);
- initial and final byte offsets;
- exact bytes consumed and remaining;
- an optional unconsumed following opcode.

The stock `UserInfoSignonStage` profile requires `exact_end_of_payload` with
zero remaining bytes. It preflights all message/completion events and then
publishes one immutable `UserInfoSignonState` containing the prior
`MovementEnvironmentSignonState`, ordered message state, completion metadata,
and retained source-payload metadata. No receive-buffer pointer, socket, raw
payload, auth bytes, player entity, renderer, or filesystem object enters the
state.

## Stage and CLI boundary

`UserInfoSignonStage` privately retains the exact socket, endpoint,
`NetchanDriver`, first decompressed payload, and optional authentication
lifetime from `MovementEnvironmentStage`. It does not reparse the envelope,
create a second driver, send a second `new`, or queue the transition request.
Success is `first_batch_complete`; timeout, cancellation, unsupported input,
secondary stream, backpressure, network failure, and protocol failure are
typed terminal outcomes with exact-once cleanup.

The explicit stop is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after user-info `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

Trace/event surfaces carry only counts, offsets, safe value lengths, endpoint,
and transmission metadata. They cannot access the raw info string, user ID,
opaque suffix, or private values. No request is sent at this stop.

## Project safety limits

These are project allocation/policy limits, not stock engine maxima:

| Limit | Default | Hard cap |
| --- | ---: | ---: |
| one user-info message | 2,048 bytes | 8,192 bytes |
| info string | 1,024 bytes | 4,096 bytes |
| key | 64 bytes | 256 bytes |
| value | 256 bytes | 4,096 bytes |
| entries per message | 64 | 256 |
| messages per batch | 32 | 256 |
| total user-info bytes per batch | 32,768 bytes | 262,144 bytes |
| stage events | 64 | 256 |

Configuration validation also requires nonzero component limits, key/value
limits no larger than the string bound, a message bound large enough for the
fixed fields, and a total-byte bound at least as large as one message. Exact
limit and limit-plus-one cases are tested.

## Negative tests and CI

Deterministic tests cover an independent exact fixture, every truncation
prefix, wrong opcode, missing/out-of-range index, zero/out-of-profile user ID,
unterminated/oversized info strings, malformed pair sequences, empty key/value,
ASCII case-collision duplicates, key/value/entry/message/count/total limits,
truncated or wrong-width opaque suffix, unexpected single-message trailing
bytes, source-buffer destruction, repeated-message order, duplicate indexes,
invalid boundary geometry, and no-scan failure. Failed parses publish neither
partial state nor an advanced cursor.

The composed fake-HLDS route also injects a wrong opcode at the exact
user-info cursor, a duplicate client index, an unterminated info string, an
over-limit info string, and a missing fixed opaque suffix. Each case fails
atomically before `sendres`, publishes no user-info/transition result, sends
no post-batch datagram, and releases the retained connection lifetime once.

The normal Win32 acceptance commands run these tests with the complete suite:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 `
  -DHLCLIENT_WARNINGS_AS_ERRORS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

A focused local Catch2 run is available after building:

```powershell
.\build\bin\Debug\hlclient_tests.exe "[userinfo]"
```

GitHub Actions uses the `vs2022-win32` configure preset, builds
`vs2022-win32-debug`, runs its CTest preset, then checks `--version`, `--help`,
and the null-renderer smoke path. CI needs no Steam client, stock binaries,
Internet during tests, GPU, authentication ticket, or Half-Life assets.

## Deliberate limitations

- controlled name/model/color value differentials remain unconfirmed;
- a normal concurrent stock second-client startup remains unavailable, though
  the accepted same-HLDS overlap profile confirms indexes zero and one;
- later runtime user-info update behavior is not generalized from this initial
  batch;
- the opaque 16-byte algorithm and contents remain unknown/private;
- no value reaches filesystem, assets, renderer, shell, URL, logger, player
  entity state, or authentication policy;
- no public CLI can inject raw user-info or mutate runtime user information.

The separate [resource-transition boundary](GOLDSRC_RESOURCE_TRANSITION.md)
continues only after exact first-batch completion. The `user-info` stop itself
retains zero transition requests and zero resource responses.
