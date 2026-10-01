# E9 — committed remote-player sound playback

Primary result: `remote_player_movement_audio_integrated_offline_verified`.
Existing E1 functionality was verified/hardened, not reimplemented.
New build `manual=not_run`; no game, HLDS,
stock client, Steam initialization, WFP activation or capture was launched.
No staging/commit/push. Worktree is HLC-steamcfg-5e48b7c1, branch
codex/stock-runtime-campaign-5e48b7c1, HEAD ea738a80e5a66a500c245f95b96eed32198e2d66.
The dirty pre-edit source snapshot (1153 verified regular files) and baseline
logs are in ignored manual-artifacts/task-records/e9-remote-player-audio-20260930.
Baseline Release SHA: 00F8036B894BC71027451C3C4818C42424490437A37D1C75552B92095BF6EFE2.

## Reused production path and actual fixes

| Link | Existing owner | E9 evidence/change |
| --- | --- | --- |
| delivered svc_sound/stopsound/spawnstaticsound | goldsrc/runtime_control_decoder.cpp | existing bounded grammar; unchanged |
| complete record transaction | goldsrc/runtime_replay_session.cpp | existing atomic world/module commit, then owning CommittedSoundQueue |
| exact sparse sound binding | goldsrc/sound_assets.cpp | approved immutable manifest, existing bounded worker/cache; open/decode failures now distinct |
| entity/channel/event identity | goldsrc/server_audio.cpp | existing legitimate replacement, stop/change and ordinal/cursor dedup; no sample-name heuristic |
| origin/listener | ServerAudio::present, normal main.cpp | defect: one-shots followed later entity snapshots; now fixed event origin, loops retain attachment |
| voice and spatial PCM | audio/mixer.cpp | existing 128 voices/64 static, units/pan/attenuation unchanged; isolated PCM tests |
| SDL output | audio/sdl_playback.cpp | same backend; device listening remains opt-in/unverified |

Actual generic targets remain hlclient_goldsrc_audio, hlclient_goldsrc_sound_assets,
hlclient_audio and hlclient_audio_sdl. No lower target acquires HL1 linkage.
GameClientHost/HL module owns local cues, not server framing or mixer rules.
Normal main drains the committed queue, computes one listener from presented
camera and committed serverinfo client_slot+1 (not fixed slot1), then uses the
same output for server and local namespaces.

Short sounds retain the validated server coordinate independently of render
visibility. Loops may follow finite sources; absent snapshots retain last known
position for 2s, then event origin. A committed updateuserinfo slot notification
conservatively invalidates old attachment and returns it to event origin. It
does not infer a disconnect from PVS/model absence or expose private user IDs.
An old voice cannot be dragged by a reused slot after that boundary; subsequent
starts establish fresh attachment. Unsupported empty-userinfo disconnect
grammar is not invented; without a delivered slot boundary exact occupant
continuity is not claimed. Map/session generation reset clears pending/voices;
local life/presentation changes do not reset other players' server audio.

Entity/channel keys distinguish two players on CHAN_BODY. Starts at different
committed ordinal/cursors remain legitimate; repeat delivery/replay is rejected.
Local presentation voices retain the high-bit namespace. An identical pl_step
sample may represent a local material impact and remote server sound without
deduplication by name, position or time. svc_sound has no established command-ID
association: local server sounds are retained, not blanket-suppressed. Exact
owner delivery/local echo and stock receiver filtering remain unverified.
The actual SDK working HEAD and gitlink were rechecked at
`b1b5cf5892918535619b2937bb927e46cb097ba1`. Its PM_PlayStepSound CHAN_BODY/sample
callbacks are behavioral reference only, not a wire grammar/delivery proof.
Remote playback never depends on listener speed, MoveVars, material or reference
prediction. No event means no fabricated step; no wall-occlusion/reverb added.

Pending one-shots retain the existing 250ms late-play deadline and 10s maximum
pending bookkeeping; no late backlog burst. Existing loader is 512 entries,
32MiB PCM. Diagnostics distinguish pending, missing, authorization, open/decode,
budget and output queue rejection. `submitted` is not mixer acceptance/listening.
--net-trace logs at most 32 receipt/transition lines per visual session, using
only safe virtual resource names, generation/record/cursor, source/channel and
server origin; mixer starts/PCM frames/output status are separate evidence.

## Optional same-host manual composition (not launched)

Unchanged default, one client/crossfire:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Read-only E9 dry-run (no runner/game/Steam/WFP/sockets):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -CheckOnly
```

Optional user launch:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer
```

The launcher forwards actual RemoteAudioPeer to the existing FunctionalSmoke
runner and native --remote-audio-peer. Native launch creates one owned HLDS and
two instances of the same verified normal executable in the same constrained
Job. Both have the same approved root, auth provider, server endpoint and input
profile; no connect-only external process or second launcher invocation.
HLC_E9_A title is listener: explicit --audio-on-focus-loss retains server audio
while B has focus (local pending input/cues remain focus-gated). HLC_E9_B title
is mover: --audio-volume 0 disables its output device, not command transmission.
Default audio focus policy and output volume stay unchanged. Either reference
or off is supported; no health50/external-map/scripted profile combination.
Both use the existing 45s duration and bounded startup/cleanup deadlines.
Closing/completing A terminates only owned B, waits its exit and requires the
whole Job empty before isolation release. Early returns retain existing exact
owned-job cleanup barriers. Native summary records peer PID/runtime publication/
exit separately; these are not listening or two-player stock parity.
The A/B profile is explicitly not campaign-evidence-eligible even with Strict
validation. Native and wrapper summaries preserve that boundary.

Both clients still perform normal authentication. Acceptance of two clients
under the current Steam session is a manual prerequisite, not an offline claim;
no bypass/plugins/bots were added. If B cannot enter, inspect the peer runtime/
exit fields; do not call A's own footsteps remote proof. Closing B may itself
end peer coverage. Roles are fixed A-listener/B-mover; no role-swap toggle was added.

Keep A stationary; select B to move nearby on A's left/right, farther away,
stop/start/jump and change supported material. Return focus to A to rotate its
camera. Listen only to A. Then move A and regress local slopes/A-D, ladder,
landing, Glock/crowbar and MuteGlockFireSound. Quiet movement or missing delivered
events are not synthesized. Two windows alone do not prove remote sound.

## Verification

Pre-edit audio baseline: 50 pass, 4 optional skips, 4371 assertions.
Final verification results and Release SHA are appended after the last code edit.
Project-owned fixtures are mandatory; private assets/device listening optional.
ASan prior runtime exits 0xC0000135 before help/tests; compilation alone is not
a runtime pass and E9 does not repair loader infrastructure.

## Verified checks on 2026-09-30

- VS2022 Win32/v143, warnings-as-errors: affected normal Debug/Release targets
  built successfully; final orchestrator evidence-boundary edit rebuilt in both.
- Audio/E9/E8: 55 pass + 4 explicit optional skips, 4725 assertions. New E9 tests
  have no private data/device prerequisites and no E9 case was skipped.
- Debug focused game/audio/movement/prediction/runtime replay: 332 pass + 6
  optional skips, 282672 assertions. Expected injected decoder/module failure
  logs are fixture outputs, not unexpected test failures.
- Core-only: all engine libraries built with HLCLIENT_BUILD_GAME_HALFLIFE=OFF;
  41/41 registered tests passed. API executable: 125 cases/2579 assertions;
  boundary guard verifies eight allowed/direct/alias/transitive/generator/source/
  object/include cases. 168 non-guard generated projects inspected, zero concrete
  HL1 source/include/link coupling; CMake checks the actual recursive graph.
- Actual OpenGL/Null: 49 pass + 2 optional installed-asset skips, 62246 assertions.
- Existing no-stock launcher harness passes including new A/B reference/off argv
  forwarding, default unchanged, incompatible profile rejection, exact report,
  stdout/stderr and error/cleanup paths. Two owned fake children prove normal and
  early Job-close cleanup; no actual server/client auth is implied.
- Native argument guard probe: actual reference/off wrapper arguments parse;
  missing wrapper capability fails closed with processes-started=0. Wrong map,
  scripted input, health50 and duplicate-peer flag rejected before launch.
- Default and RemoteAudioPeer actual -CheckOnly pass. Native peer planner dry-run
  returns passed/process-launches=0. Launch resolution is the normal Release path.
- Actual CI prediction acceptance: all 14 Debug/Release synthetic scenarios,
  1000 commands each, equal final-state and history-replay hashes (including wall,
  jump, duck, stale duplicate, teleport, hard reset and deterministic route).
- ASan Debug client/tests compiled. Runtime tests --help, [e9], and client --help
  each exit -1073741515 / 0xC0000135 with zero output items before testing.
  Runtime remains unavailable; the exact missing loader dependency is unresolved.
  No ASan pass or user listening/device output pass is claimed.
- git diff --check passes; index remains empty. Snapshot comparison shows only
  19 intended existing files changed, plus this record and the ordinary
  include/hlclient/core/remote_audio_peer_plan.hpp header. Other local work retained.

Release SHA-256:
`E3F038BE6D28B15F34358913C490A075D13857769693EC44E9C638DB331D999C`.
New build manual=not_run. Existing E8 acceptance remains attribution=user_report.

Reproduce on the already-configured trees without cleaning:

```powershell
cmake --build build --config Release --target hlclient hlclient_tests hlclient_core_api_tests hlclient_stock_runtime_orchestrator --parallel 1
ctest --test-dir build -C Release --output-on-failure -j 1
cmake --build build-core-g1 --config Release --target hlclient_engine_core hlclient_core_api_tests --parallel 1
ctest --test-dir build-core-g1 -C Release --output-on-failure -j 1
```

All live/capability opt-ins remain unset; HLCLIENT_RUN_WFP_CAPABILITY_TEST=0.
Final full Release run after the last production edit is recorded below.

Final Release suite: **2547 passed + 35 explicit skips / 2582, zero failures**,
357.06s. Exact retained log:
manual-artifacts/task-records/e9-remote-player-audio-20260930/release-final-verified.txt.
An intermediate rerun failed only the source-policy assertion that required the
old eligibility predicate. It now requires every old clause plus A/B exclusion
in both native and wrapper; no old runtime assertion was relaxed. Final full run
includes the stronger guard and passed after the last production change.

Explicit skipped cases (optional installed resources/device, WFP disabled,
interactive SDL focus/capture or filesystem/link/network capability unavailable):

- ADS and file symlinks remain fail closed
- Active-guard canary uses the supplied campaign job and exact count
- Actual dynamic WFP canary is explicitly capability gated
- Captured SDL focus loss clears tracker state and restores the cursor
- Contained and escaped directory symlinks are classified by physical target
- Corpus rejects path and manifest mutations without partial state
- E2 opt in original MDL event records and referenced WAV decode read only
- E4 opt-in installed Glock marker attachment and shell model are inspectable read-only
- E4.1 opt-in installed shell reaches production OpenGL world pixels
- E5 opt-in installed Valve decal passes the approved exact-root importer
- E7 opt-in installed material samples decode as nonempty PCM
- E7 opt-in installed materials table is a read-only parser control
- E8 opt in Crossfire ramp support resolves original compiled hulls read only
- E8 opt in original step samples decode read only with distinct families
- Escaped junction and link cycles are classified without traversal
- External target review supports a regular-file link when capability exists
- Game paths reject a game directory link that escapes basedir
- Local provider preparation failures emit zero UDP packets
- Local resource file open rejects non-files and reparses
- Local resource roots reject a final directory reparse point
- Mount points unsupported tags and excessive link depth fail closed
- No-follow reachability distinguishes directory symlink outcomes
- Opt in local WAV decoding uses original read only assets
- Opt in short moderate SDL playback exercises actual output without game
- Opt-in crossfire inspected brushes traverse the live provider and produce pixels
- Opt-in local Valve viewmodel resolves through the production Studio binding
- Precache asset dispatch stops before opening an unavailable world source
- Prepared local provider fails closed without fallback material
- Redundant WFP owner remains effective after primary guard loss
- RootedFileSystem rejects a symlink escape when links are available
- SdlWindow capture lifecycle is typed and Escape restores the cursor
- SdlWindow native overflow releases operational capture
- SdlWindow retains ordered capture transitions until they are polled
- UNC expressions are classified without a remote open
- Verified locator reopen preserves reparse rejection

No new remote animation, event codec, physics, weapon or UI milestone was added.
Remaining limits: server delivery/receiver filtering and exact local echo are
not stock-verified; loop occupant continuity without a committed slot boundary
is not guessed. Installed assets and device listening controls were not enabled.
Two-player authentication and A-only listening remain manual prerequisites.
New live sessions/stock/HLDS/Steam/WFP/capture=0; staging/commit/push=none.

## E9.1 corrective: remote fire framing (2026-09-30)

Attribution=user_report: both clients entered Crossfire; firing after one
became visible to the other closed the session. Exact run
5bb63b54543e4183b47d541a636a9680: server35716, A12832, B22996. A exited2 with
runtime_record_failed/decoder_failed/unsupported_opcode3, record489,
sequence504, computed message156:0/failure157:0 in167 bytes. Server diagnostics
confirm two entered-game events; A failure terminated the same owned Job
containing B and HLDS, not proven independent crashes. Audio was ready without
error. Owned cleanup was exact; Fast restoration had zero managed entries,
not full-tree attestation. No raw failing packet was retained.

The missing generic codec now parses bounded svc_event3 and
svc_event_reliable21 through the normal RuntimeControlDecoder/mixed dispatcher.
This limited crash corrective was explicitly requested after E9; it does not
add remote weapon effects. Framing uses the independent parser author's
[Protocol tables](https://github.com/cgdangelo/talent/blob/main/libraries/parser-goldsrc/docs/demo-structure.md#svc_event-3)
and [reliable table](https://github.com/cgdangelo/talent/blob/main/libraries/parser-goldsrc/docs/demo-structure.md#svc_eventreliable-21),
not external engine implementation code. Pinned Valve SDK
b1b5cf5892918535619b2937bb927e46cb097ba1 network/delta.lst registers event_t.
The reliable documentation calls the native type event_args_t; the decoder
binds the transmitted event_t schema, not an invented alias or SDK ABI struct.
SDK Glock PlaybackEvent calls are behavioral reference, not wire proof.

Each argument delta uses a fresh schema-derived null base. Packet references
are packet-list indices, not entity IDs; delays remain unscaled wire ticks.
Queues align once after all entries. Decoded values are independently owned.
Bounds:31 queued entries per message,128 per complete mixed/control payload,
64 fields/4096 accounted bytes/256 string bytes per arguments object, plus
existing payload/message bounds. Typed missing-schema/delta/truncation/budget
errors remain fail closed; there is no body-length guess, scan or resync.
Existing raw fingerprints, canonical world hashes and whole-record atomic
publication remain unchanged. No .sc callback, remote animation/muzzle/audio/
damage, second scene or new loop is executed by these observations.

Both initialization paths pass schemas; mixed/standalone controls also pass
staged server time. Pre-baseline initialization has no server-time context:
an advertised event_t with time-window fields fails closed there. The pinned
event_t contains no such fields. Existing svc_sound/B1/E8 paths are unchanged.
Early A failure now retains B's exact handle through cleanup, samples its exit,
and saves bounded peer-diagnostic-redacted.log/peer-metadata.json through the
existing secure writer. A's original error remains primary.

Project-owned literals test framing, ownership, following-message boundaries,
atomicity and dedup. The167-byte/boundary156 fixture is synthetic geometry,
NOT recovered stock bytes. The new build manual=not_run; the original failure
is not a passed two-client test. Source snapshot1262 regular files copied and
hash-verified before edits: manual-artifacts/task-records/e9-remote-fire-framing-fix-20260930/source.
Verification and Release SHA follow after the last source edit.
New live/Steam/HLDS/WFP/capture launches=0; staging/commit/push=none.

### E9.1 final offline verification

These results use the final mixed-dispatch typed aggregate-budget error change,
not the historical E9 source/results above.

- VS2022/Win32/v143 affected normal Debug/Release targets built successfully:
  hlclient, tests/core API, runtime-control, clientdata, entity-snapshot,
  runtime-replay, prediction and stock-runtime orchestrator checkers.
- Final full Release offline CTest: **2559 passed + 35 explicit skips / 2594,
  zero failures**, 415.92s. Log:
  manual-artifacts/task-records/e9-remote-fire-framing-fix-20260930/release-full-final.txt.
  Skip categories remain those listed above: optional private assets/device,
  interactive SDL capture, link/network capabilities and disabled WFP. No live
  game/authentication/isolation capability opt-ins were enabled.
- Focused Release [e9-fix],[e9]: **17 cases / 1533 assertions passed**, including
  owned fake-child early-exit cleanup and retained peer diagnostic evidence.
- Debug control/replay/audio/movement/prediction: **295 passed + 4 optional
  skips / 299**, 280981 assertions. The literal [game] filter matched no cases;
  an additional actual [game-module],[weapon-audio],[world-impacts],[e8],
  [movement-audio] run passed **89 + 3 optional skips / 92**, 17660 assertions.
  Counts overlap and are not summed as unique coverage. Injected decoder/module
  failure reports are expected fixture evidence, not test failures.
- Core-only HLCLIENT_BUILD_GAME_HALFLIFE=OFF rebuilt the final packet decoder:
  **41/41 CTest passed**, API 125 cases / 2579 assertions. Architecture guard
  covers eight dependency fixtures. All 168 non-guard generated projects and
  179 compiler-read dependency logs had zero concrete Half-Life source/link/
  include coupling. Normal Release outputs were not replaced by this build.
- Current Release production OpenGL/Null: **49 passed + 2 optional local-asset
  skips / 51**, 62246 assertions. This is offline rendering evidence, not a
  new two-player game or listening confirmation.
- Default reference and peer reference/off launcher -CheckOnly pass, resolving
  normal build/bin/Release/hlclient.exe; process-launches=0. The guarded no-stock
  launcher harness and owned fake-process regressions are separate offline proof.
- ASan: current runtime-control and packet-decoder Debug libraries compile with
  /fsanitize=address. The old ASan test executable, not rebuilt for this fix,
  fails --help before output with 0xC0000135. Current-fix ASan runtime tests
  **not_run**; loader cause remains unresolved. Compilation is not a runtime pass.
- git diff --check passes; branch/HEAD/index unchanged and no staging. Snapshot
  comparison preserves all 1262 original files; only 11 intended existing files
  differ, plus ordinary new tests/event_test_fixture.hpp. Unrelated dirty work,
  historical reports and other worktrees remain intact.

Current normal Release SHA-256:
`76E660BED01681C175228057CE59FB0B60A67100651392AA182AB44E2CD406AB`.
This identifies the prepared corrective binary, not the historical manual run.
New build manual=not_run. Original raw bytes were unavailable, so this is a
verified missing-codec fix, not exact incident replay or stock-binary parity.
No new remote weapon effects were added; svc_sound/E9 movement audio remains
the existing path. New live/Steam/HLDS/WFP/capture launches=0; commit/push=none.

Next user test uses the unchanged optional two-client command above: select B,
move into A's view and fire; check that neither client/session closes, then
listen on A and regress movement/local weapons. Retain the run ID and both
redacted peer reports if another failure occurs.

## Subsequent user confirmation retained for E10 (2026-09-30)

`attribution=user_report`: «тест показав все наче працює».
Scope: successful two-client manual flow with movement/audio/local Glock scenario
and no obvious reported session closure. The report supplied no executable hash
and does not individually prove gait/pitch/transitions, material categories,
stock-binary parity or stress coverage. It applies to the preceding E9 build,
not the new E10 binary. Historical manual=not_run entries and logs above remain
unchanged; E10 still requires its own user visual validation.

## Two-client gameplay audio correction (2026-10-01)

The new `attribution=user_report` says the ordinary two-client test has footsteps
but lacks weapon sounds and visual effects. The original E9 manual composition
above was an A-only listening diagnostic: the peer argument plan explicitly
added `--audio-on-focus-loss` to A and `--audio-volume 0` to B. This made B silent
even when the user selected its window. The historical report of
`audio_backend=disabled` and `audio_error=output_queue_rejected` in B's retained
run `2414cd57d7ee4f0587970347ab4ea91d` is consistent with that intentional
disabled sink, not proof of a hardware/audio-device failure.

`RemoteAudioPeer` now launches both normal clients at the ordinary volume35
with ordinary focused-window audio. Neither client receives an audio override;
selecting A or B selects that client's listener, avoiding two simultaneous
listeners on the same desktop. The A/B names, same server, authentication,
input, reference/off prediction, duration/unlimited options and owned cleanup
remain unchanged. This supersedes the A-background/B-disabled launch policy
documented above without changing the historical E9 listening evidence.

The pure plan and native dry-run contract now verify both clients retain normal
audio defaults; explicit conflicting audio overrides remain rejected. The
launcher banner and `remote-audio-output-isolation=focused-client-output` report
the applied policy. This addresses the hidden permanent mute only; server-event
presentation and local action timing have independent implementation and
regression evidence in the current task record. No new listening confirmation
is inferred from the argument-plan test, and no new live session was launched.

### Correlated existing run evidence

All times below are Europe/Kyiv,2026-10-01, from retained wrapper publication
metadata. These are existing user sessions; the corrective task launched none.

| Run (under manual-artifacts/research-copy-smoke) | Retained result |
| --- | --- |
| `567b750b79b14a3c8674a2c1047221c5`,18:31:14,single client11080/server29780 |10 local fire actions,10 flash/shell/hit effects,zero late drops; backend ready; application completed,exit0 |
| `b0a8957bcf1246ae94e758ea8c85d84a`,18:32:47,A29732/B29364/server18548 |12 local fire actions but10 audio/visual late drops,only2 flashes/shells/hits; backend ready; A completed,exit0 |
| `2414cd57d7ee4f0587970347ab4ea91d`,15:51:30,A8196/B32692/server24536 |B intentionally disabled output;7 local fires,7 visual late drops; both clients later exit2 on unsupported svc23 temporary-entity subtype |

The newer two-client report's47 local audio submissions comprise36 movement,
2 fire,3 impact/ricochet and6 shell voices. They are not47 confirmed gunshots.
Its peer log retains clip changes but no terminal outcome, so peer output cannot
be inferred. Neither wrapper retains a client executable SHA; the retained
helper hash is `EEB543271B8EBA3F33CE6193EFCBC85A3A039E16197C9DC6D092AA53257ABD70`.
All three report exact owned cleanup, scoped_exact restoration and zero cleanup
errors; Fast mode did not attest the whole research tree. The newer two-client
wrapper labels the run client-connection-rejected despite A's completed/none/0
and server entered-game evidence. That outcome-classification discrepancy is
separate from the missing effects and is not rewritten by this task.

The original svc23 failure retains no subtype/raw bytes. The diagnostic
boundary is known, but a particular blood/particle subtype is not proved.
The later two reports contain no parser error. Historical metadata/logs remain
unaltered; aggregate counters demonstrate suppression, not pixel/listening proof.

### Retained remote-effect diagnostics

The normal application emits `[remote-effects-summary]` with16 fixed unsigned
numeric fields: received,accepted,unresolved,unsupported,local_echo,late,invalid,
fire,swing,audio_submitted,audio_late,audio_missing,audio_rejected,shells,
impact_hits,flash_submissions. The native existing redacted excerpt retains
only the newest valid row, fewer than512 bytes, before verbose terminal output,
within its unchanged64-line/16KiB bounds. Both client roles use this same path.
Unknown/missing/reordered/duplicate fields, nonnumeric/overflow values, private
suffixes, embedded marker lookalikes and oversized rows fail closed. Invalid
rows cannot escape via the general error/connect retention path. A synthetic
native-contract fixture tests crowding, latest-row choice, privacy and bounds;
these counters separately identify event delivery, eligibility and execution,
without claiming that submission is audible output or visible pixels.

### Local clock correction and remote production path

The user clarified `attribution=user_report`: effects were missing **in both
clients**, including the firing client. The newer two-client run retained27
discarded scheduler wall-time samples. The previous publication formula
`activation + command_sequence * 20 ms` was therefore540ms behind the sampled
wall clock. B1's stale-action clamp plus the existing effect/audio age guards
then suppressed subsequent otherwise valid actions. The single-client control
has no such discarded samples and all10 fires produced their local effects.
This establishes a concrete code defect consistent with the retained evidence;
it is not a reconstruction of unavailable original network bytes.

`live_runtime_stage.cpp` now retains the actual scheduler sample end in a
256-entry exact-command table and publishes it only for that command's fresh
TX receipt. Replay/backups do not emit another action. The wire history,
20ms physics cadence, bounds and all existing late-effect guards are unchanged.
The existing fake-peer life/RX/TX regression adds a601ms recovery and a fresh
post-recovery shot through the module, visuals and production audio mixer.

The previous E9 framing-only scripted-event path now passes committed owned
arguments to the selected GameClientAPI module. Advertised exact event slots
bind Glock1/Glock2 and crowbar. Half-Life owns samples, spread/profile rules,
one-shot identities and the bounded event/action queues. The normal application
executes remote requests through the same approved asset loader, shell pool,
static-world impact/decal owner, mixer and OpenGL renderer as local effects.
Remote automatic voice IDs have their own namespace; local weapon sounds,
remote fire, impact and real shell-contact audio cannot replace one another
merely because their action serials match. Listener focus gates playback, not
remote event receipt, shell simulation or world-effect visibility.

The engine draws up to32 world flash billboards with normal world/entity depth,
before the camera-local viewmodel depth reset. Existing one-light capacity is
explicit: a local shot takes priority; otherwise the closest active remote
light is selected. Existing32shell/64decal pools remain shared and bounded.
Remote shell collision callbacks retain emitter/action/contact identity and
survive a local weapon switch; generation reset clears pending effects.

Scope: existing Glock primary/static-world effects and remote crowbar swing;
server crowbar contact sounds remain ordinary committed `svc_sound`. There is
no new all-weapon implementation, player-hit decal/damage authority, exact
stock random trajectory or delayed scripted-event scheduler. Missing packet
context, unsupported event bindings/delays and unavailable resources are typed
skips, not invented effects. Mandatory fixtures own their geometry and PCM;
installed Half-Life and an audio device are not required for core verification.

### Final offline verification and manual handoff

Task record: `manual-artifacts/task-records/e10-remote-weapon-effects-20261001/`.
This is a new corrective build, not a repeated historical E9/E10 report.

- VS2022/v143/Win32 all-target Release build passed. Affected Debug application,
  test and orchestrator targets also built successfully.
- After the final production edit, all 2651 registered Release offline tests
  were covered: **2614 passed, 37 explicitly skipped, 0 failed**. The parallel
  partition covered 2650 tests in 159.84 seconds; the existing fake-HLDS fixture
  with a global process-absence assertion passed separately in 2.02 seconds.
  It was serialized to avoid racing other fake peers, not omitted or weakened.
- Focused Release remote-effects/post-stall checks passed 2591 assertions in
  12 cases. Focused Debug effects/game-module/replay/prediction checks passed
  162762 assertions in 256 cases, with 6 optional installed-asset cases skipped.
- The new actual-context OpenGL tests passed: world flash pixels appear from
  multiple camera directions, are occluded by walls and Studio geometry, expire
  back to baseline, and preserve subsequent render state. Isolated production
  mixer tests prove nonzero finite fire, impact and real shell-contact PCM;
  simultaneous local/remote voices retain distinct ownership. These are pixel
  and synthetic PCM proofs, not user listening confirmation.
- `build-core-g1` remains `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: all targets built,
  and 41/41 tests passed, including architecture checks and the independent
  alternate module using the same new event/reset/clock seam without HL fallback.
- Launcher `-CheckOnly` passed reference/peer/unlimited and off/peer/300 seconds.
  The no-stock launcher and native redacted-report/peer-plan checks also passed.
  No game, Steam API or WFP activation was required.

The 37 Release skips are explicit optional installed-asset/device, WFP,
reparse/link/provider capability and SDL input-capture checks. Required
project-owned OpenGL and audio-mix regressions passed. Real-device listening
was not tested here. ASan runtime was not run; its previously unconfirmed
runtime status is unchanged. No new live game/stock client/HLDS/managed capture
was launched. Staging, commit and push: none.

Pre-edit preservation contains 1272 hash-verified source files. Final source
verification found no missing original files, no snapshot hash errors, and an
empty index; branch/HEAD remain unchanged. Seven new source/header/test files
are ordinary project files, included in CMake rather than hidden in artifacts.
All earlier unrelated local changes remain in place. `git diff --check` passed.

Prepared normal Release `build/bin/Release/hlclient.exe`, 3489792 bytes, SHA-256:

```text
88FCB21677CFEA01B21D2920477728A787DB625F15ECE5FE30D8DA1D44F262C9
```

Updated orchestrator SHA-256:

```text
8F4BEB361E0DA6CC2052B4B8321DD1B4969F261AB4BFA709C92715FEDB9460DB
```

The existing launcher selects these Release outputs. Neither hash is attributed
to an earlier user run. **New build manual=not_run.** Run:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Select A, fire Glock at a wall, and verify its local flash/light, shell, decal,
gunshot, impact and later shell-contact sound. Select B and repeat; focus now
selects the audible listener instead of permanently muting B. Observe the other
player's brief world muzzle flash, shell trajectory and static-world impact.
Regress crowbar swing/server contact audio, reload, movement, camera-local
viewmodel and near-player visibility. Closing the clients ends the unlimited
manual test; allow owned cleanup/restoration to finish. Remote effects from
unimplemented weapon scripts and unsupported delayed/context-free events are
not claimed. Existing historical unknown svc23 subtype and wrapper outcome
classification limits above remain separate, unresolved observations.
