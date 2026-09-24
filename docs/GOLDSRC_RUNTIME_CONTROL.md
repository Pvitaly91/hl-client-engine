# Reference-driven GoldSrc runtime control

## Status and profile

M4.7.1.2A introduced the executable
`public_goldsrc48_runtime_control_v1` profile. Later shared-runtime slices
extended that same decoder; M4.7.2E does not create a second parser. It decodes
byte-aligned server service messages from an existing `OwnedServicePayload`.

| Message | Opcode | Body in this profile | State effect |
|---|---:|---|---|
| `svc_nop` | 1 | none | typed event only |
| `svc_setview` | 5 | one little-endian signed 16-bit entity reference | replaces the view-entity observation |
| `svc_time` | 7 | one little-endian IEEE-754 binary32 server-time value | replaces the server-time observation |
| `svc_signonnum` | 25 | one unsigned byte; this first profile accepts the referenced value `1` | replaces the signon/control observation |

The shared dispatcher also has typed, source-bounded support for `svc_setangle`,
`svc_lightstyle`, `svc_updateuserinfo`, the selected `TE_BSPDECAL` temporary
entity, `svc_choke`, registered user messages and `svc_voiceinit`. M4.7.2E adds
the normal post-spawn controls needed to keep the same live decoder running:

- `svc_sound` (6), including its LSB-first 9-bit mask, conditional
  volume/attenuation/pitch, channel/entity/sound references and bit-coordinate
  origin;
- NUL-terminated `svc_print` (8), `svc_stufftext` (9), `svc_centerprint` (26),
  `svc_finale` (32) and `svc_cutscene` (34), which are retained as inert typed
  metadata and are never executed;
- exact fixed-size `svc_stopsound` (16), `svc_particle` (18), `svc_setpause`
  (24), `svc_spawnstaticsound` (29), `svc_weaponanim` (35), `svc_roomtype`
  (37), `svc_addangle` (38), `svc_crosshairangle` (47) and `svc_soundfade`
  (48).

These controls have no new world-state or readiness effect. Their exact body
is consumed only so a later canonical clientdata/entity message in the same
stock payload is not lost.

The implementation profile is `public_protocol_reference`; its stock
interoperability status is always
`not_verified_against_stock_runtime_payload`. It does not promote or alias the
separate `stock_protocol_48_build_10210_evidence_pending` catalog. The strict
catalog's reserved runtime profile continues to fail closed without its own
accepted corpus.

## Public specification record

The independently authored decoder was written from the following field-level
facts. No third-party function body is compiled, copied, or translated into
the project.

- ReHLDS revision
  [`6266cd23faee4a6e9cf3974f9605b2cadd86f0a4`](https://github.com/rehlds/ReHLDS/commit/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4),
  [`rehlds/engine/net.h`](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/net.h)
  confirms the ordered GoldSrc service-command numeric table: `nop=1`,
  `setview=5`, `time=7`, and `signonnum=25`.
- At that revision,
  [`rehlds/engine/sv_main.cpp`](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_main.cpp)
  emits `svc_setview` followed by `MSG_WriteShort`, `svc_time` followed by
  `MSG_WriteFloat`, and `svc_signonnum` followed by `MSG_WriteByte(..., 1)`.
  [`rehlds/engine/pr_cmds.cpp`](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/pr_cmds.cpp)
  independently shows the dynamic set-view path writing an edict number with
  `MSG_WriteShort`.
- At that revision,
  [`rehlds/engine/common.cpp`](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/common.cpp)
  defines byte, short, and float message serialization; floats are normalized
  with the little-endian conversion and occupy four bytes.
- At that revision,
  [`rehlds/HLTV/Core/src/Server.cpp`](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/HLTV/Core/src/Server.cpp)
  confirms the consuming side: `ParseNop` has no body operation,
  `ParseSetView` consumes one short, `ParseTime` consumes one float, and
  `ParseSignonNum` consumes one byte.
- The pinned public Valve SDK revision
  [`b1b5cf5892918535619b2937bb927e46cb097ba1`](https://github.com/ValveSoftware/halflife/commit/b1b5cf5892918535619b2937bb927e46cb097ba1),
  [`engine/eiface.h`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/engine/eiface.h)
  is only a supplemental API cross-check for distinct byte/short/entity write
  operations. It does not supply this decoder's numeric opcode table.
- The M4.7.2E post-spawn extension was checked against ReHLDS revision
  [`550f2d62f13f4ebeb029c1d9d1c212133202611d`](https://github.com/rehlds/ReHLDS/commit/550f2d62f13f4ebeb029c1d9d1c212133202611d).
  Its [`SV_BuildSoundMsg`](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_main.cpp)
  supplies the conditional sound field order, while
  [`common.cpp`](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/common.cpp)
  supplies LSB-first bit-coordinate widths and byte rounding. The consuming
  [`HLTV Server.cpp`](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/HLTV/Core/src/Server.cpp)
  independently confirms sound parsing and the fixed/string body boundaries.
- Xash3D FWGS revision
  [`7500a6b3647e71d9b21691671957a0e06731019e`](https://github.com/FWGS/xash3d-fwgs/commit/7500a6b3647e71d9b21691671957a0e06731019e),
  [`engine/common/net_buffer.c`](https://github.com/FWGS/xash3d-fwgs/blob/7500a6b3647e71d9b21691671957a0e06731019e/engine/common/net_buffer.c),
  is a GoldSrc-compatible cross-check for the ordered service-message names
  and LSB-first bit-buffer operations. No Xash-only Protocol 49 field or
  behavior is accepted.

These references describe the selected GoldSrc-compatible Protocol 48 subset.
No Xash Protocol 49 constants or layouts are part of this profile. Differences
in proprietary stock client/server builds, unusual proxies, demos, and signon
values other than `1` remain unverified rather than silently accepted.

## Input and transaction boundary

`RuntimeControlDecoder::decode_and_apply` accepts an owning, decompressed,
server-to-client service payload plus an exact `StockRuntimeSourceCursor`,
source generation, and payload ordinal. It never accepts or searches an
arbitrary UDP datagram. Version 1 requires byte alignment and walks forward
from the supplied cursor without scanning or resynchronizing.

Every event owns its decoded value and copies generation, sequence,
acknowledgement, reliability, payload ordinal, message ordinal, and exact
start/end cursors. `svc_setview` remains only a view-entity reference; it is
not promoted to local-player, Steam, log-userid, or prediction identity.
`svc_time` remains server time, not wall clock, acknowledgement, or command
number. `svc_signonnum` is only the decoded signon/control observation and is
not renderer, entity-snapshot, authentication, or stable-session readiness.

Publication is atomic per input suffix. The decoder builds events and a next
state privately, then commits both counters and state only after every message
reaches the exact payload end. A truncated body, non-finite time, unsupported
opcode/alignment, generation mismatch, limit failure, or allocation failure
publishes no batch and leaves the caller's state unchanged. On an unsupported
opcode the typed error reports that opcode and its exact start cursor; no body
length is guessed.

An explicit source-generation reset clears time, view, signon, and counters.
No monotonic-time relation is inferred across generations.

## Offline verification

The checker has no SDL, Steam, WFP, network, or filesystem prerequisite:

```powershell
cmake --build build --config Release --target hlclient_runtime_control_check
& .\build\bin\Release\hlclient_runtime_control_check.exe
```

Its project-owned literal byte fixture contains all four messages. Unit tests
also cover each message separately, multi-message atomic publication,
sequential payloads, generation reset, every supported body truncation,
non-finite numbers, unsupported opcodes/alignment, exact nonzero cursors,
bounds, transactional failure, and output lifetime after input destruction.

## Deliberate next boundary

This slice does not decode `svc_spawnbaseline`, `svc_packetentities`,
`svc_deltapacketentities`, or `svc_clientdata`. The next M4.7.1.2 part must bind
the already-published delta-schema registry to baseline/entity bit layouts,
define entity-history/base references and generation reset behavior, and then
add client-local data fields. M4.7.2 separately owns stock usercmd
transmission. None of those claims follow from this control decoder.
