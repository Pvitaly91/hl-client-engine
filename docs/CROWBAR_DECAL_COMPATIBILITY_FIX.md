# Crowbar impact temporary-entity compatibility — 2026-09-28

## Evidence and scope

This is an additive correction after E1, not a rerun of G1 or a new gameplay
feature. Historical task and session reports remain unchanged.

User reports (`attribution=user_report`): pickup sounds were audible in run
`569cf18d757b4d76ab459477586d9644`; a crowbar wall-hit sound was audible before
the client closed in run `5fe89fbd7cef4f9ea8210fbd39141b1a`.

The latter run's staged report identifies server PID 42356, client PID 18868,
crossfire, reference prediction and requested start health 50. Its terminal
diagnostic identifies the primary failure as `runtime_record_failed` ->
`decoder_failed` -> `runtime_control_failed` ->
`unsupported_temporary_entity_type`, opcode 23, record 122, sequence 137.
The message begins at byte 166 of a 175-byte payload; failure is at byte 168,
after the opcode and subtype. `audio_backend=playback_ready`,
`audio_error=none`, `audio_started=1`, `gl_errors=0`. Exit 2 is a protocol
rejection, not evidence of an audio-device crash or a scripted coverage failure.

The old diagnostic did **not** retain the subtype byte or raw packet. The
nine-byte tail and SDK crowbar path are consistent with WORLDDECAL/HIGH, but
the exact historical subtype cannot be proved. Offline fixtures below are
project-owned reference-format tests, not a replay of private captured bytes.

## Wire contract and implementation

Primary reference is the existing Valve SDK checkout at
`b1b5cf5892918535619b2937bb927e46cb097ba1`:

- `dlls/crowbar.cpp`: `Smack` -> `DecalGunshot(BULLET_PLAYER_CROWBAR)`.
- `dlls/weapons.cpp`: crowbar branch -> `UTIL_DecalTrace`.
- `dlls/util.cpp`: `UTIL_DecalTrace`, `UTIL_GunshotDecalTrace`.
- `common/const.h`: temporary-entity identifiers and body comments.

All coordinates below are three signed little-endian Protocol 48 shorts,
retained in eighths. Sizes include svc opcode and temporary-entity subtype.

| Subtype | Wire body after subtype | Total bytes |
| --- | --- | --- |
| 104 TE_DECAL | coordinates, decal byte, entity short | 11 |
| 109 TE_GUNSHOTDECAL | coordinates, entity short, decal byte | 11 |
| 116 TE_WORLDDECAL | coordinates, decal byte | 9 |
| 117 TE_WORLDDECALHIGH | coordinates, decal byte | 9 |
| 118 TE_DECALHIGH | coordinates, decal byte, entity short | 11 |

`RuntimeControlDecoder` now produces an owning `RuntimeControlDecal` value.
HIGH formats resolve the byte index by adding 256; world formats carry an
implicit entity zero. The distinct existing `RuntimeControlBspDecal` (type 13,
short decal index and conditional model short) is unchanged. The new variant
is appended, preserving the existing alternative ordering.

This is generic service framing in `src/goldsrc/runtime_control_decoder.cpp`,
not HL1 inventory/weapon policy. There are no game-module, renderer, input,
prediction, scheduling, canonical hash, SDK ABI or launcher changes. No decal
rendering, client-side hit authority or locally synthesized ricochet sound is
added. These effects are decoded but not presented. Sound remains the existing
committed server-audio path. Unknown subtypes still fail closed (with the
numeric subtype included in decoder error context); no scan/resync or guessed
skip is introduced. This does not add a new terminal-summary schema field.

The existing record commit boundary remains the only publication point.
Malformed suffixes cannot publish earlier clientdata, entity changes or sound.
Output values own their data and outlive RX buffers. Duplicate records do not
replay sounds, and later normal clientdata/entity records continue to decode.

## Verification

Prechange snapshot and logs:
`manual-artifacts/task-records/crowbar-decal-fix-20260928/`.
Only the four changed source/test files were copied and SHA-256 verified;
all earlier dirty work remains in place. Branch/HEAD remain
`codex/stock-runtime-campaign-5e48b7c1` /
`ea738a80e5a66a500c245f95b96eed32198e2d66`.

Baseline Release `[runtime-control],[runtime-replay],[audio]`, seed
2532982175: 50 passed, 2 opt-in skips, 2176 assertions passed.
New literal regressions cover all five layouts, nonzero cursors, following
opcodes, ownership, every byte truncation, unknown suffixes and invalid typed
metadata. The production replay/audio regression includes real fixture
clientdata/entities, failed transaction retries, duplicate suppression and
subsequent records. Existing expected wire/canonical/weapon/movement values
were not changed.

Final verification (seed 2532982175, existing incremental build directories):

| Check | Result |
| --- | --- |
| Normal Release including helpers | Built |
| Normal Debug client and tests / existing ASan Debug tests | Built |
| Focused decoder/replay/audio Release | 54 passed, 2 opt-in skips, 2972 assertions |
| Affected Release / Debug / ASan Debug regressions | Each: 451 passed, 3 opt-in skips, 289594 assertions |
| One final broad offline Release suite | 2135 passed, 24 skipped, 405044 assertions |
| Core-only Release (`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`) | Client/engine and core API test binary built; 95 tests, 1424 assertions passed |
| Core-only CTest gates | 2/2 passed: core API/alternate module and dependency/source/alias architecture guard |
| Null/process-level offline replay | 8/8 passed |
| Launcher CheckOnly from System32 | reference + TestStartHealth 50; off + damage-respawn-check passed |
| Synthetic launcher / 50 HP fixtures | Passed; no managed/live runner |
| Research-copy read-only FunctionalPreflight | prepared_projection_content_verified; stock processes = 0 |
| git diff --check / index | Passed / no staged changes |

Affected regression filter (identical across configurations):

```text
[audio],[runtime-control],[runtime-replay],[d4],[movement],[collision],[scheduler],[live-runtime],[reference-transmission],[prediction],[game-module],[weapon-presentation],[live-visual],[input],[hud],[pickups],[test-start-health],[manual-outcome],[live-brush],[brush],[brush-submodels]
```

The affected runs include the actual OpenGL project-owned weapon-sequence/A1
framebuffer regression. Their three skips are optional local WAV assets,
local crossfire brush assets and audible SDL device playback, none requested
for this fix. The broad suite additionally skips unrequested local assets
and unavailable platform/file-system/input capabilities. Its exclusions are
`~[udp]~[network]~[isolation]~[orchestrator]~[steam]~[live]~[loss]~[security]`;
no blanket CTest/live-script execution was used. No failed test is treated as
a skip. Two intermediate compiler failures were corrected (explicit int16
optional initialization and field-wise snapshot checks in the new test).
No existing expected values were relaxed.

Normal Release `build/bin/Release/hlclient.exe` SHA-256:
`D4ABE861BBD6FF2C428D5DDDB49FE92D5024744E4BF8EDB52419DED7582F939C`.
Unchanged Release orchestrator SHA-256:
`35296CB591C60CB938045D273B7F76376FB01CEB24096BD14FA4037A7CC4E6D0`.

## Separate approved research-file repair

During this task a later existing run `2d179a50ee714f4cb74aaba5a33af3f2` was
found in the local artifacts. Its staged report says client exit 0, owned
process cleanup exact, but `restoration_status=wrapper_pending`. The current
research `valve/liblist.gam` still referenced that run's temporary test50 DLL.
The user separately approved restoring **only** this file. Its previous bytes
were retained in `manual-artifacts/repairs/liblist-2d179a50-20260928/` and the
verified saved original was restored. Steam was read-only; other research
files and historical run reports were not modified. This is not a retrospective
full-restoration attestation, nor a fix to the wrapper's restoration lifecycle.

## Manual handoff

New decoder-build manual validation: **not_run**. The exact historical subtype
remains unavailable, so the wall-hit scenario needs the user's fresh test.
The existing command is unchanged:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```

Wait for the console's final managed result and cleanup/restoration reporting;
do not treat only the native diagnostic lines as proof of wrapper completion.
New live/game/Steam/WFP launches = 0; capture/ETW = not_run;
staging/commit/push = none.

Result: `crowbar_decal_decoder_offline_verified_manual_pending`.
