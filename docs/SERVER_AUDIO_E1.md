# E1 server sound playback

## Additive user evidence

2026-09-28, `attribution=user_report`: lift ascent is now smooth; the user
also confirmed improvement on slopes, uneven terrain and walking on the lift.
No run identity, measurement or universal moving-platform support is inferred.
Historical D4 reports and artifacts remain unchanged. New audible validation
of E1 is **not_run**.

## Ownership and production path

`RuntimeControlDecoder -> RuntimeReplaySession commit -> CommittedSoundQueue
-> ServerAudio -> ApprovedSoundAssets -> WavImporter -> Mixer -> Playback`.

* `hlclient_audio`: neutral owning PCM, WAV importer and deterministic mixer.
* `hlclient_audio_sdl`: private SDL playback implementation, no protocol/game.
* `hlclient_goldsrc_audio`: exact committed identity, flags/channel translation,
  pending starts, entity/channel/sample matching and sound-index requests.
* `hlclient_goldsrc_sound_assets`: existing manifest capability/approved opener
  and importer registry, single cancellable resource worker.
* Normal `hlclient` composes one audio owner inside its existing visual loop.
  No module, transport, renderer, input loop or movement kernel is duplicated.
  GameClientAPI and HalfLifeClientModule acquire no device/network handles.

The same sole SDL runtime owns video lifetime; playback initializes/releases
only its own audio subsystem. SDL is still pinned to 3.4.14 revision
`147a8ee32dbf9ac02f3794964490687b6bbda1bc`. Output uses float stereo 48 kHz,
`SDL_OpenAudioDeviceStream` on **default playback**, explicitly resumed from
its paused initial state. No recording device is opened. The audio worker
mixes fixed 10 ms blocks against queued sample count (target <20 ms), not render
FPS. No file I/O, packets, GL or game callbacks run there. Main command submission
uses a fixed SPSC ring (listener updates use a nonblocking try-lock); critical
overflow clears voices and queued PCM rather than losing
a stop and retaining a loop. Device failure is nonfatal `audio_unavailable`.

Interactive keyboard/mouse enables playback at 35% application volume.
`--audio-volume 0..100` is optional; zero mutes. Focus loss outputs silence
while the sample clock advances, preventing a delayed burst. Scripted,
headless/replay/checker modes never open playback. `-CheckOnly` is unchanged
and invokes neither the client nor audio. System volume is never changed.

## Wire reference and transaction

Pinned ReHLDS `6266cd23faee4a6e9cf3974f9605b2cadd86f0a4`:
[SV_BuildSoundMsg/SV_StartSound](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_main.cpp),
[ambient writer](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/pr_cmds.cpp),
[coordinate readers/writers](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/common.cpp)
and engine/net.h. Valve SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`
common/const.h, dlls/util.h, healthkit.cpp, items.cpp, plats.cpp and doors.cpp
establish channel constants and actual start/stop/change call sites. No
third-party implementation or tests are copied.

GoldSrc-specific consumer cross-check: Xash
`7500a6b3647e71d9b21691671957a0e06731019e`
[cl_parse_gs.c](https://github.com/FWGS/xash3d-fwgs/blob/7500a6b3647e71d9b21691671957a0e06731019e/engine/client/parse/cl_parse_gs.c),
engine/client/sound/s_main.c and engine/client/soundlib/snd_wav.c.
Its native protocol is **not** used.

| Message | Layout / effect |
| --- | --- |
| svc_sound 6 | Existing LSB decoder: mask9, optional volume8/attenuation8, channel3, entity11, index8/16, three presence bits and signed integer12/fraction3 coordinates, optional pitch8, byte padding |
| svc_stopsound 16 | LE16 entity<<3/channel; stops first matching entity/channel independent of sample |
| svc_spawnstaticsound 29 | Three signed LE16 /8 coords, sound LE16, volume8, attenuation8, entity LE16, pitch8, flags8; 14 bytes |
| flags | 1 volume, 2 attenuation, 4 large index, 8 pitch, 16 sentence (unsupported), 32 stop, 64 change-volume, 128 change-pitch, 256 spawning |

Defaults are volume255, attenuation1 (wire64), pitch100. The coordinate integer
has no invented +1. Existing parser now retains owning stop/static values;
there is no second parser. Fixed-control kind and historical hashes remain
unchanged. Truncated sound reports the first failed bit-reader checkpoint
through the existing typed runtime error/PowerShell path. No resynchronization.

Outbox publication occurs after decoder, module and world validation, at the
existing record commit. It owns values, never RX spans. Generation, record
ordinal and exact end cursor form a bounded ordered high-water; record identity
and message ordinal are retained. Replay rejects duplicate/conflicting records
first. Same values in distinct records remain distinct events. Pre-baseline
sounds use the existing bounded retained-control handoff and are not drained
while the live owner has a protocol error. Audio is never submitted by movement
replay or retained render state.

## Resources, mixing and bounds

Existing `RuntimeReplayLocalAssets` retains its prepared manifest/environment;
the audio worker uses `manifest.find(ResourceType::sound,index)`, builds an
exact approval plan, reopens/validates the same rooted locator, and dispatches
through the audio registry. No dense index assumption, model/ordinal confusion,
raw-path fallback or doubled sound/ prefix. The existing mapper rejects unsafe
paths; the audio binding rejects special markers and already-prefixed names
before opening the approved locator. Sentence flags never reach file lookup. weapons/
and player/ server WAVs are not filtered.

RIFF length/chunks/padding/alignment/rate/byte-rate/decoded size are validated.
PCM unsigned8 and signedLE16, mono/stereo, 4–192 kHz, at most 120 s and 16 MiB
decoded PCM per asset. Unknown bounded chunks are skipped. Cue sample offset
plus LIST/adtl/ltxt `mark` sample length selects an end-exclusive loop; cue-only
loops to data end. Unsupported/malformed cue layouts fail that asset only.
No cue means one-shot even on CHAN_STATIC. Stereo retains its two source
channels and applies independent left/right balance, without mono summation.
Linear interpolation resamples at source_rate*pitch/48000; output is finite
and clipped to [-1,1]. This is basic stereo, not HRTF or full engine parity.

128 voices, at most 64 static; 512 committed events, 512 output commands,
512 cached exact bindings, 32 MiB PCM cache. Limits drop new work deterministically;
cache does not evict active PCM. Missing/unsupported resources are cached as
terminal failures. A pending start waits at most 10 s; after load, nonloops
older than 250 ms are discarded. Stops and replacements remove pending starts,
so a late worker result cannot resurrect them. Active loop PCM is shared-owned.
CHAN_AUTO allocates independently; named dynamic channels replace matching
entity/channel, static channels key additionally by sample. Changes preserve
position and cursor. A missing change target is typed `change_without_voice`,
not a guessed start (intentional narrower policy than Xash's fallback).
CHAN_STREAM does not loop. Voice channel 7 and sentences remain unsupported.

Listener is the actual final RenderScene camera. Entity sources ignore render
visibility/effects/culling. Brush sources use transformed geometric bounds
centers and the same D4 sampled vertical translation as the presented camera
and brush. Ordinary entities use current observed origin. Missing entities
retain last-known position for 2 s, then explicit event origin; no origin-zero
or deletion inference from PVS. Distance uses a 1000-unit base range and
attenuation once; right is the Z-up camera right vector. Receiving-player sounds
are centered. Non-vertical entity interpolation remains the existing current
observation boundary, not fabricated historical motion.

Map-generation reset clears voices/pending commands. Death/respawn does not
reset this world audio owner. Teardown requests worker stop, cancels between
bounded source-read updates, joins owned workers, then destroys stream/device
before SDL lifetime ends. There are no detached threads. Source operations have
a 2 s cooperative deadline; synchronous verified local-disk OS calls retain
the existing filesystem layer's blocking-call limitation.

## Verification and handoff

Pre-edit snapshot: `manual-artifacts/task-records/e1-audio-prechange-20260928`,
287 ordinary source files verified by SHA-256. Baseline: 51 tests, 17733
assertions passed. Final results are appended after actual verification.

Existing crossfire/Map, reference/off, damage-respawn-check and optional 50 HP
profile remain unchanged. No audio success requirement is added to manual
outcome or prediction coverage. Bounded terminal diagnostics are retained by
the native/PowerShell application-outcome allowlist. Underruns are explicitly
unmeasured rather than fabricated zero.

Manual command (not run by the agent):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```

Listen for server pickup/charger start/loop/stop and transmitted lift/door sounds,
turn and move away, check no repetition/stuck loops and retained movement
smoothness. No local predicted firing/footsteps, .sc, full sentence/VOX/HEV,
music, voice/microphone, occlusion, DSP or reverb is claimed. A server may use
unimplemented client events instead of svc_sound: absence is not replaced by a
test tone. Device-open/PCM tests do not establish that the user heard correct
game sound. New live/HLDS/Steam/WFP/capture/ETW=0; staging/commit/push=none.

Read-only local control: crossfire's lift1–lift4 are func_door with movesnd=0,
stopsnd=6. Pinned doors.cpp maps these to common/null.wav while moving and
doors/doorstop6.wav on arrival. Continuous motor sound is therefore not promised
for these specific lifts. Local medcharge4 (22050 Hz unsigned8 mono) and
suitcharge1 (11025 Hz unsigned8 mono) contain single cue + adtl/ltxt mark loops;
gunpickup2 and medshot4 are 22050 Hz one-shots. No map/server asset was edited.

## Completed offline verification (2026-09-28)

Result: `server_sound_playback_integrated_offline_verified`.
Branch remains `codex/stock-runtime-campaign-5e48b7c1`, HEAD remains
`ea738a80e5a66a500c245f95b96eed32198e2d66`. Existing dirty changes were retained;
no staging, commit, push, reset, cleanup or dependency upgrade was performed.
All 287 pre-change snapshot copies still match their inventory hashes.

| Verification | Actual result |
| --- | --- |
| Normal Debug / Release | Incremental client, tests and affected helpers built |
| Existing ASan Debug | Affected libraries and tests built; focused run clean |
| Focused Release / Debug / ASan | Each: 566 passed, 3 opt-in skips, 310089 assertions passed |
| One final broad offline Release suite | 2131 passed, 24 skipped, 404248 assertions passed |
| Core-only Release, HLCLIENT_BUILD_GAME_HALFLIFE=OFF | Client/engine libraries built; 95 core/API/alternate-module tests, 1424 assertions passed |
| Core-only CTest gates | 2/2: core API and transitive dependency/source/alias architecture guard |
| Null application process replay | 8/8 selected CTests passed |
| Local read-only WAV + actual GL brush control | 2 tests, 82 assertions passed; four submodels changed pixels, GL error 0 |
| Short SDL playback capability | 1 test, 5 assertions passed; 150 ms low-level tone, no game/microphone/system-volume change |
| Native and PowerShell summary retention | Passed, original primary errors preserved, fixture restoration exact, zero stock processes |
| Fake launcher and 50 HP evidence regressions | Passed, including incomplete/foreign reports and reference/off/scenario forwarding |
| Launcher CheckOnly from System32 | reference, off + damage-respawn-check, reference + TestStartHealth 50 passed |
| git diff --check / staged changes | Passed / empty |

The focused opt-ins are local WAV, local brush and playback-device controls;
all three were subsequently run explicitly. Broad skips additionally include
unrequested local visual assets, unavailable Windows symlink/custom-reparse/
second-volume fixtures and hidden SDL relative-mouse/focus capability. No
failed test is reclassified as a skip. One stale mixed-build test binary fault
and old zero-audio-importer assertions occurred during development; consistent
incremental builds and the final runs above supersede those unsuccessful runs.
Only the three explicit built-in registry-count expectations changed from zero
to one; a project-owned WAV is also probed/imported through that registry.
Wire, movement, scheduler, camera, weapon and canonical-state expectations were
not loosened. The new audio fixtures remain in the normal test target; the
unchanged core-only test target proves its existing API/alternate-module seam.

Reproduction uses the existing configured build directories, never a clean:

```powershell
cmake --build build --config Release --target hlclient hlclient_tests
cmake --build build --config Debug --target hlclient hlclient_tests
cmake --build build-asan --config Debug --target hlclient_tests
cmake --build build-core-g1 --config Release --target hlclient hlclient_core_api_tests
ctest --test-dir build-core-g1 -C Release -R '^hlclient_(core_api_offline|game_boundary_guard)$' --output-on-failure
ctest --test-dir build -C Release -R '^hlclient_application_runtime_replay_' --output-on-failure
```

Logs, exact focused filters and deterministic seed `2532982175` are retained
under `manual-artifacts/task-records/e1-audio-prechange-20260928`. Required code
and tests are ordinary source files, not generated from those artifacts.

Ready normal `build/bin/Release/hlclient.exe` SHA-256:
`BA7C690D6292524D5B06F27D6CA5B1724FAF53A9BEC7418E103313D147B14087`.
Orchestrator SHA-256:
`35296CB591C60CB938045D273B7F76376FB01CEB24096BD14FA4037A7CC4E6D0`.
SDL3.dll SHA-256:
`2377B7A423D7681D513F7F3E2B809711EADA27D7EE6E564469CC02571395E0C8`.

Remaining limits: no device hotplug/reopen, no measured underrun counter,
cooperative rather than OS-guaranteed local-file cancellation, and no new
live changelevel support beyond the host's existing generation handling.
`audio_loads` counts successful ready-asset voice bindings, including cache
hits, not distinct disk reads. Basic panning and current-observation nonvertical
sources are not full stock acoustics/interpolation. All new managed/live audio
validation remains **not_run** until the user's next manual session. The device
test does not assert that the user heard correct server sounds.
