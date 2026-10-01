# E2 local weapon audio

Primary result: `local_weapon_audio_integrated_offline_verified`.
Manual/live E2: `not_run`.
No new live HLDS, Steam, WFP, capture/ETW or managed session. Offline fixture
checks only. No staging/commit/push.

## Additive manual evidence

`attribution=user_report`: Fast launch works; health/armor stations, pickups,
crowbar wall impact and lift are audible. No run ID, audio counters, spatial
coverage or startup timing was supplied/inferred. Historical artifacts and
earlier manual-validation statuses remain unchanged.

## Source and ownership

Reference SDK revision: `b1b5cf5892918535619b2937bb927e46cb097ba1`.
Both `projects/vs2019/hl_cdll.vcxproj` and `hldll.vcxproj` compile
`dlls/wpn_shared/hl_wpn_glock.cpp`, not the OLD_WEAPONS alternative.
Both define `CLIENT_WEAPONS` in Debug/Release.
`cl_dll/hl/hl_events.cpp` registers glock1/crowbar script event names;
`cl_dll/ev_hldm.cpp` supplies sample/volume/pitch/channel semantics.
`cl_dll/entity.cpp` handles 5004 options as a sample at attachment 0;
`cl_dll/hl/hl_weapons.cpp`/`com_weapons.cpp` gate effects on first prediction.
No SDK implementation functions/tests were copied.

| Cue | Owner/source | Due time | Policy |
|---|---|---|---|
| Glock primary | accepted existing B1 fire action | action start | `weapons/pl_gun3.wav`; SDK weapon-channel role; volume .92–1, pitch 98–101, deterministic per command identity, not stock RNG parity |
| Crowbar swing | accepted existing B1 swing action | action start | `weapons/cbar_miss1.wav`, volume/pitch 1, local weapon channel; no impact prediction |
| Glock reload/deploy, crowbar deploy | actual imported 5004 marker in supported sequence | animation start + frame/fps | options token, volume/pitch 1, independent automatic presentation channel; no invented sound when a model has no marker |
| Crowbar/world impact | committed server E1 event | existing E1 policy | unchanged, independent of local swing |

Read-only inspection through `GoldSrcStudioModelImporter` of this installation:

| Model/sequence | Actual records | Relative sound time |
|---|---|---|
| Glock 5 `reload`, 6 `reload_noshot` | 42 frames at 18 FPS; event 5004/type 0, frame 4 `items/9mmclip2.wav`, frame 23 `items/9mmclip1.wav` | 4/18 = 0.222222 s; 23/18 = 1.277778 s |
| Glock 3/4 fire | frame 0 event 5001, options `11` | not a sound; ignored by E2 |
| Glock 7 `draw` | 16 frames at 16 FPS, zero events | no deploy sound added |
| Crowbar 1 `draw` | 13 frames at 24 FPS, zero events | no deploy sound added |
| Other crowbar sequences | zero events in this model | swing comes from B1/EV_Crowbar, not a fabricated marker |

The actual two clip WAVs plus `weapons/pl_gun3.wav` and
`weapons/cbar_miss1.wav` decoded successfully through the existing WAV importer.
Production-path fixtures additionally exercise frame-zero deploy markers, but
this does **not** claim such markers exist in these installed draw sequences.
Private MDL/WAV bytes were not copied into the repository or made test/build
dependencies. The installed-asset check is opt-in and read-only.

`games/halflife/weapon_audio.*` is a companion to the **existing** B1
controller, not another action-eligibility/cooldown controller. `start()`
emits exactly once after B1 checks; confirmation does not emit. HUD ammo,
damage and hit authority are unchanged. Unknown client event codes and
server events are not interpreted as local sounds. Fire/swing sequences do
not additionally consume 5004; these effects have the B1 owner.

`game_api/audio.hpp` supplies fixed owning output/preload batches;
`GameClientHost::drain_audio()` uses the selected module (including the test
module). The application only wires this to `goldsrc::LocalAudio`, the same
`ApprovedSoundAssets` worker/cache and E1 output/mixer/SDL. Renderer emits no
audio. Generic importer already owns frame/event/type/options; only its
metadata projection is extended. `assets/animation_markers.hpp` knows no
5004 or weapon rules. Core-only has no concrete module dependency.

## Identity, clock and cancellation

Host reset serial, module cancellation scope, B1 command identity and visual
restart are separate. Marker cues retain sequence, ordinal and loop occurrence;
monotonic serials survive drain. Traversal is `(previous,current]`, with `-1`
admitting frame zero on a new restart. Repeated timestamps/rewinds do not emit.
Crossed markers are ordered by time/loop/ordinal, including equal samples.
At most 256 records, eight recent loops and 32 emitted cues per update;
events older than 250 ms are consumed/dropped, never replayed as a backlog.
Correction of the same action retains ownership, cancels pending old cues and
retains its elapsed high-water and consumed ordinal/loop set across the supported
reload variants. It cannot replay a consumed marker even after a sequence change.
Matching confirmation does not restart it. Unconsumed future corrected markers
remain eligible. The set is bounded to 64 occurrences; excess is dropped.

Focus cancel, model/switch/life/death reset invalidate pending cues; exact
local ownership never resets world voices. Already-heard short sounds cannot
be undone by rejection. Muted/no-device updates consume identities. Preloads
can finish into the bounded cache, but cancelled cues have no callback capable
of starting a voice. Map/session destruction stops the existing worker.

Short local voices use a disjoint high-bit voice namespace: one replacing local
weapon channel and independent automatic marker slots. They cannot replace
E1 CHAN_ITEM impacts, ambient or another
entity's channels. This is a documented presentation-channel adaptation, not
complete engine channel emulation. Own viewmodel sounds are centered at the
listener, **not** placed at camera-local model coordinates/world zero. SDK
5004's world attachment positioning is not reproduced for these first-person
sounds; positional remote weapon/event rendering remains out of scope.

No sample-name/time suppression is used. The pinned CLIENT_WEAPONS Glock
event and crowbar first-swing event use FEV_NOTHOST; this profile supplies
their local owner. Ordinary svc_sound has no command ID and is not guessed to
be a duplicate. Other players and server impact/pickup/station/lift sounds
remain untouched. A non-stock server redundantly emitting local fire via
svc_sound is an unresolved profile case, not universally deduplicated.

## Assets, timing and errors

Typed local references never receive a synthetic precache index. Up to 32
ASCII `weapons/*.wav` / `items/*.wav` tokens enter the existing loader. Absolute paths,
traversal, doubled prefixes, commands and marker syntax are rejected.
Resolution is constrained to the selected world's approved root, with no
fallback installation; the existing locator/reopen/identity checks and
bounded source reader precede the E1 WAV importer. Same local sample shares
PCM across profile/model references. Cache remains 32 MiB/512 total entries;
loading is on the existing single cancellable worker, never a shot/render/
audio-thread filesystem read. Missing/unsupported/late assets are optional
diagnostics, not gameplay exit 2.

`Command::scheduled_seconds` retains logical due time. The session submits
crossed events on the first update on/after that time; it does not promise
sample-exact onset independent of render update latency. SDL's existing
10 ms blocks / bounded software queue add separate device latency. No audible
zero-latency or stock-exact random sequence is claimed.
Delivery checks a fresh host-clock sample after asset/projection work, so an
in-frame stall cannot make old cues appear fresh. Pending requests preserve
owning serial order until ready or expired, without sample/time matching.
Dropping an optional local one-shot from a full SDL queue does not invoke the
critical world-stop overflow reset policy.

`weapon_audio_*` bounded summary distinguishes actions, cue kinds, markers,
exact marker duplicates, repeated delivery serials, timeline corrections,
cancelled/late/missing/invalid/muted cues and
accepted output submissions. Existing B1 confirmed/rejected/corrected metrics
and E1 actual mixer starts/backend metrics remain separate. Output submission
is not necessarily a voice start (the mixer may reject capacity);
`weapon_audio_started` counts actual presentation mixer starts. Missing and
unsupported/limited assets are distinguished (`missing` vs `invalid`). No sound is
proof of damage, hit, reload ammo or live correctness.
`weapon_audio_duplicates` is the sum of marker/delivery duplicates only;
`weapon_audio_marker_duplicates`, `weapon_audio_delivery_duplicates` and
`weapon_audio_timeline_corrections` retain the separate reasons through the
client/native/PowerShell report. A correction alone is not a suppressed sound.

## Verification / handoff

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`; branch
`codex/stock-runtime-campaign-5e48b7c1`; unchanged Git HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`. The initial worktree already
contained 275 changed/untracked entries. Staged files remain zero.

Records below are under
`manual-artifacts/task-records/local-weapon-audio-e2-20260928/`.
The pre-edit source snapshot contains 1,110 hash-verified files; none of those
sources were removed. Baseline Release: 58 passed / 1,949 assertions, with two
explicit opt-in audio skips. Existing B1/A1/H3/H4/D4 expectations were not
mass-updated to accommodate audio.

| Check | Recorded result |
|---|---|
| Final Release, Debug and ASan Debug affected targets | passed, incremental bounded builds; final `hlclient`, tests and native helper updated (`build-diagnostic-handoff-*.txt`) |
| Final Release, Debug and ASan Debug focused regressions | 276 passed / 163,501 assertions in each configuration; four opt-in skips (`focused-handoff-*.txt`) |
| ASan instrumentation | existing `HLCLIENT_ENABLE_ADDRESS_SANITIZER=ON` Debug configuration, focused/core/process/native checks passed; no sanitizer failures |
| Core/API + alternate test module | 97 passed / 1,437 assertions in Release/Debug/ASan; final dedicated HL1-OFF engine build and both API/architecture CTests passed (`core-only-handoff.txt`) |
| Actual core-only dependency audit | 146 generated project source/reference lists, actual test linker inputs and generated graph contain no concrete module; guard rejects direct, alias, transitive, generator, source, object and include violations |
| Null/process fixtures | four passed per final Release/Debug/ASan configuration: null smoke, basic replay, budget-one replay and visual-null replay (`process-handoff-*.txt`) |
| Actual OpenGL fixtures | three passed / 356 assertions, zero skips (`opengl-handoff.txt`) |
| Original MDL/WAV control | nine passed / 434 assertions with explicit read-only root (`local-mdl-wav-handoff-final.txt`); inventory above reflects importer output |
| One broad final offline suite | 2,276 passed / 753,619 assertions, 20 capability skips (`final-offline-release.txt`); run once after shared-code refactor, not repeated for final clock/diagnostic-only refinements |
| Client/native/PowerShell summary | production publication roundtrip passed, including distinct marker/delivery/correction fields (`summary-roundtrip-handoff.txt`); stock launch absent |
| Launcher | fixture suite passed; actual `-CheckOnly` from system32 passed for reference, off, damage-respawn-check, optional 50 HP and Strict (`checkonly-handoff-final.txt`) |
| Source hygiene | `git diff --check` and whitespace checks for new E2 files passed; no staging, commit, push, cleanup or history rewrite |

The focused suite covers E1/B1/E2, Studio metadata, input/focus, camera-local
viewmodel, prediction and D4/lift/pitch regressions. Frame-zero, repeated and
irregular times plus 30/60/144 FPS preserve logical marker identities/order;
offline sink checks include PCM blocks, starts, due timestamps, independent
crowbar impact, cancellation and 1/37/480-frame callback blocks. Movement
replay stays outside the sound emission path.

The four focused skips are explicit local-asset/device controls: original E2
models/WAVs were then checked separately, while actual-device playback remains
`not_run`. The broad suite's 20 skips concern Windows file/directory symlink,
custom reparse-tag/second writable volume capabilities and hidden SDL focus/
relative-mouse capabilities. Live/capability campaigns and audio hardware were
excluded, not silently counted as passed. No audible latency or live success
is inferred from offline PCM.

Release `build/bin/Release/hlclient.exe` SHA-256:
`4C0889C3FB3C99D1CFE620F94406EA1ACE44319A7A22EFE0EBF349B385313C80`.
Native orchestrator SHA-256:
`0043CCEDB5FBF1B4A18D9711A8CC079F9E8554AF8620D09025F3075193017F11`.

### Actual targets and reproducible offline commands

`hlclient_game_halflife` compiles the B1 companion `weapon_audio.cpp`.
`hlclient_game_api` owns the cue contract; `hlclient_game_client_host` depends
on that API, not the concrete module. `hlclient_goldsrc_audio` compiles the
neutral delivery owner; `hlclient_goldsrc_sound_assets` supplies the existing
approved worker/cache and `hlclient_audio` the mixer. The normal application
composes the concrete module with these shared targets. Nothing in the lower
audio/Studio/host graph links the Half-Life target.

From the existing worktree, using its already configured build directories:

```powershell
cmake --build build --config Release --target hlclient hlclient_tests hlclient_core_api_tests hlclient_stock_runtime_orchestrator --parallel 1 -- /p:UseMultiToolTask=true /p:CL_MPCount=3 /p:MultiProcMaxCount=3 /p:EnforceProcessCountAcrossBuilds=true /p:PreferredToolArchitecture=x64 /nodeReuse:false
build/bin/Release/hlclient_tests.exe "[weapon-audio],[audio-api]" --rng-seed 2531315663
cmake -S . -B build-core-g1 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF
cmake --build build-core-g1 --config Release --target hlclient_engine_core hlclient_core_api_tests hlclient_goldsrc_sound_assets --parallel 1 -- /p:UseMultiToolTask=true /p:CL_MPCount=3 /p:MultiProcMaxCount=3 /p:EnforceProcessCountAcrossBuilds=true /p:PreferredToolArchitecture=x64 /nodeReuse:false
ctest --test-dir build-core-g1 -C Release -R "^(hlclient_core_api_offline|hlclient_game_boundary_guard)$" --output-on-failure
```

Debug uses the same `build` tree; ASan uses the existing `build-asan` tree
and Debug configuration. ASan runtime lookup is scoped to the test process's
PATH; no system PATH or runtime installation is changed. No clean build or
build-tree deletion is needed. The core test fixtures require no installed
game, local MDL/WAV files or SDL playback device. Optional original-asset
inspection explicitly sets `HLCLIENT_LOCAL_GAME_ROOT`; ordinary CI does not.

Manual command remains:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Fast default, Strict explicit, reference/off, map/scenario and optional 50 HP
remain unchanged. E2 does not depend on 50 HP. Dry-fire, secondary attack,
other weapons, predicted footsteps/landing, damage/trace/decal/shells,
remote .sc events and complete GoldSrc event/client.dll APIs are deferred.
