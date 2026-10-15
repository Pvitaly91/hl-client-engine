# E13 — remote player death, server corpse and respawn

Task `HLC-E13-REMOTE-PLAYER-DEATH-CORPSE-RESPAWN`, 2026-10-02.
Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, starting HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Dirty E11/E12 preserved.
Pre-edit source snapshot: 1,284 hash-verified regular files under
`manual-artifacts/task-records/e10-e13-20261002/source-manifest.json`.
Pre-edit Release baseline: 72 passed, 3 opt-in skips, 8,780 assertions.

## Actual pre-E13 audit

| Field | Observed source behavior before edits |
| --- | --- |
| remote_death_sequence_current | E10 used exact current server sequence/frame/animtime/framerate, bounded 0.2 s transition and non-loop terminal clamp; no local death sequence selection |
| remote_weapon_after_death_current | E11 attachment chose sampled weaponmodel; authoritative zero could retain a previous p-model for one interpolation pair |
| remote_gait_after_death_current | Current server gait zero removed the gait layer; numeric phase remained retained through zero as for jumping |
| corpse_entity_current | Generic packet entity reconstruction retained raw renderfx/renderamt, but semantic observation omitted both |
| corpse_render_current | Ordinary Studio evaluation used corpse model slot without DeadPlayer/reference semantics; no selected-module proxy path |
| respawn_interpolation_current | Player EF_NOINTERP/gap/teleport/slot boundary reset already existed; no committed visual inactivity/re-arm boundary across skipped render records |
| remote_visual_lifecycle_current | E12 had lifetime/weapon/socket checks, but missing pose could fall back after an intervening life boundary; no post-commit entity life guard |

The normal source path, not a guessed manual death outcome, establishes this
audit. Existing `DeathMsg` parsing in `games/halflife/server_messages.cpp` keeps
bounded killer/victim/inert weapon text and feeds the existing **local** lifecycle.
The selected module retains at most 32 owning numeric notice-source markers
for optional diagnostics (generation/slot reset clears them). A marker is not
proof that the current rendered sequence is a death sequence. E13 does not use
it to create a corpse, decide remote death, choose a sequence,
reset network state or introduce kill-feed UI.

## Pinned reference and wire boundary

Official Valve SDK stays at `b1b5cf5892918535619b2937bb927e46cb097ba1`.
Reviewed `dlls/world.cpp::CopyToBodyQue`, `dlls/player.cpp` death/activity and
respawn paths, `dlls/client.cpp` full-pack projection, `common/const.h`,
`common/entity_state.h`, `network/delta.lst`, and
`cl_dll/StudioModelRenderer.cpp::StudioDrawModel/StudioDrawPlayer`.
No SDK implementation body is copied. Exact public facts used:

- `kRenderFxDeadPlayer=17`, `renderamt` is 1-based player index, `EF_NOINTERP=32`.
- Body queue copies model/index, origin/angles, frame/sequence/animtime and
  uses the original player's index; it does not create a new client player.
- The SDK dead-player draw selects the referenced player's model, suppresses
  weapon/gait and interpolation, and uses the corpse current entity for Studio
  sequence/frame/transform. Its non-gait player path centers controllers.
- Death activities clear server gait; server inventory death clears weaponmodel.
  Respawn/non-loop restart can carry EF_NOINTERP. The bit alone is not a death ID.
  `PlayerDeathThink` can set the original player modelindex to zero **after**
  copying its body. That is model visibility, not loss of the same player slot's
  validated model selection.

`runtime_replay_session.cpp::project_visual_fields` now projects optional neutral
`render_effect/render_amount` only from exact unsigned DT_INTEGER descriptors
named renderfx/renderamt in ordinary/player schemas. No ABI offsets, opcode
scan, fixed numeric message IDs or schema replacement. Valid values are 0..255;
absent is not zero. Decoder reconstruction remains the owner of baseline/delta
retention. Malformed suffix/duplicate records publish neither observation nor
the new notification. Historical canonical/I/v2 hashes are unchanged;
explicit `runtime_observation_visual_hash_v3` covers the two new fields.

## Normal composition and ownership

`RuntimeReplaySession atomic commit -> GameClientHost::remote_entities -> HL
numeric visual life guards`; separately, `present_entities -> world_entity ->
approved model binding -> remote_player -> StudioPoseEvaluator -> existing
EntityRenderFrame/ClientWorldState/RenderScene -> normal OpenGL/Null`.

API/header `game_api/remote_player.hpp` adds synchronous bounded immutable
`WorldEntityPresentationContext` and numeric intent. `world_entity` is a pure
selected-module policy; the host contains no Half-Life constant, filename,
downcast or fallback factory. The default/alternate module is ordinary/silent.
The selected HL module recognizes only ordinary entities with exact renderfx 17
and a valid 1..maxclients renderamt. It requires a **current committed player
schema** reference with a current nonzero model slot, or that same still-present
identity's committed selection when the pinned inactive death path explicitly
sets modelindex/weaponmodel/gaitsequence to zero. That bounded 32-slot numeric
selection is cleared on disappearance, committed userinfo/overflow boundary,
map/session reset or missing descriptors. Invalid/missing reference/model is
typed unavailable, not a previous-map or hardcoded player.mdl fallback.

The existing imported binding/library/Studio package is reused. A proxy retains
its own entity number, pose fields, position, angles, bounds and floor-light
query. It gets no weapon, gait, transition, E12 socket sample or remote audio.
Receiving player's first-person body stays excluded; its distinct server corpse
can render. Corpses are stateless numeric samples of the latest committed
ordinary entity, already stepped by the pre-existing ordinary path. EF_NOINTERP
and body-queue reuse therefore cannot blend an old corpse into a new one, even
when the same number references another player or appears at another position.
No client corpse pool, DeathMsg synthesis, timeout, physics or GPU reimport.

Living players retain E10 main sequence authority and non-loop clamp. Current
weaponmodel zero overrides a retained sampled weapon immediately. A selected
HL visual-inactive (weaponmodel=0 and gaitsequence=0) -> authoritative re-arm
boundary steps player position and resets gait/yaw/transition. Post-commit
notifications preserve that reset even if RX crosses the boundary between
render samples. This is a local **visual epoch**, not server deadflag/life ACK,
and may conservatively reset presentation after a non-death inactive interval.
Player disappearance/model/lifetime changes also reset numeric presentation.
Neither these operations nor corpse sampling reset sockets/Netchan, command
history, prediction kernel, imported model/GPU caches or local C lifecycle.

E12 pending events retain an independent fixed-memory visual epoch at commit.
Current missing/hidden/unarmed emitter, epoch change or pre-boundary ordinal
rejects pending Glock/crowbar work before any new flash/light/shell/impact/audio.
Existing world shells/impacts/decals/voices continue their own bounded lifetime;
already published shell actions still accept real collision audio. Live records
remain atomic and source ordinals, not opaque record IDs, supply ordering.

Bounds: 32 player numeric states, 32 event-life guards, existing 32 pending
events/128 dedup keys/64 world actions, existing validated packet entity limit,
existing approved assets and scene/voice limits. API retains no borrowed arrays.
The normal opt-in net trace keeps its existing 64-row budget, with numeric
generation/lifetime/visual epoch, role/reference, classification, pose, effects,
nointerp and render status. No paths or raw bytes are logged. A corpse has no
fabricated server life ID; its own entity/committed frame and stateless pose
replace a mutable client corpse lifecycle. Reference changes are diagnosed as
body-queue reuse; same-player reuse is rendered immediately without guessing an
unobservable wire lifetime.

## Limits and manual handoff

Classic server-authoritative Studio presentation, not stock-exact engine parity,
ragdolls, gibs, blood, corpse collision, full deathcam or scoreboard. This slice
resolves models from the current approved player model slot; it does not add a
new userinfo skin/model replacement subsystem. Missing sequence/appearance/model
fields remain unavailable rather than locally forced death/idle values.
No live death wire capture is newly acquired. Installed model death indices
cannot be inferred from labels; optional such evidence is not available here.

E12 acceptance is separately appended in `REMOTE_GLOCK_PMODEL_FIRE_E12.md`.
E13 `manual=not_run`, `remote_death_animation_manual=not_run`,
`corpse_proxy_manual=not_run`, `respawn_nointerp_manual=not_run`,
`repeated_cycle_manual=not_run`.

Default single-client crossfire (unchanged):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Two clients, bounded:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -DurationSeconds 180
```

Two clients, user closes both:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Observe remote death: actual server sequence, no p-model/walking legs or stale
fire; corpse if server-present at its own location; respawn immediately at spawn
with fresh pose, old corpse independently retained only while server-present.
Repeat at least three cycles, swap receiving roles, walk around corpses and
verify standing/crouching weapon effects. Wait for exact owned cleanup after
closing windows. Automated fixtures are not user visual acceptance.

New live/stock client/HLDS/managed captures/Steam/WFP = **0**.
Staging/commit/push = **none**. Final build/test evidence follows below.

## Final offline evidence

Primary result: `remote_player_death_corpse_respawn_integrated`.
These are post-final-production-edit results, not reused E12 counts.

| Check | Result |
| --- | --- |
| VS2022/v143 Win32 normal Release all targets | passed |
| VS2022/v143 Win32 normal Debug all targets | passed |
| Focused Release E10–E13/player/effects/lifecycle/packet/API | 114 passed, 3 skipped, 16,358 assertions |
| Focused Debug E7–E13/prediction/lifecycle/packet/API | 254 passed, 6 skipped, 173,442 assertions |
| Additional Debug E3–E6 audio/weapon visuals/impacts/Studio pose tags | 85 passed, 7 skipped, 7,356 assertions |
| Final full Release offline CTest catalogue | 2,649 passed, 38 skipped, 0 failed; 2,684 main + 3 serialized fake-process entries |
| `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` build and all core/API tests | 41/41 passed |
| Actual core-only graph and generated projects | no concrete HL module sources/links in manifest or 168 project files |
| Architecture guard | allowed, direct, alias, transitive, generator-expression, source/object/include cases passed |
| Alternate module through same host/API | independent proxy/model/pose and reset/teardown checks passed, no HL fallback |
| Existing bounded ASan core/API build/runtime | passed: 16 tests, 1,048 assertions, matching existing x86 runtime on process-local PATH |
| Full application ASan runtime | not_verified; not newly launched or repaired |
| Launcher CheckOnly | single crossfire, two-client 180 seconds, two-client NoTimeLimit passed; process-launches=0 |
| Launcher no-stock fixture | reference/off, maps, duration, Scenario, parameter/report/error propagation passed |
| Source preservation | 1,284 original snapshot hashes valid, no existing file deleted, empty index, same branch/HEAD |
| `git diff --check` | passed |

The full offline catalogue includes actual Null/process-level fixtures,
canonical prediction/wire regressions and E10–E12 p-model/effect regressions.
WFP capability execution was disabled; local game root was not injected.
Skip categories are explicit opt-in installed assets/audio output/live isolation,
interactive SDL capture, and unavailable link/reparse privileges. Owned E13
OpenGL tests **ran**, not skipped. No test counts imply a live death capture.
Full CTest counts are case results; the assertion totals above are the exact
focused executable results, not guessed aggregate full-suite assertion counts.
Numeric `[e3]`–`[e6]`/`[studio-pose]` tags matched no tests; the additional
Debug run explicitly used their actual `[audio]`, `[weapon-visuals]`,
`[world-impacts]`, `[crowbar-impacts]`, `[goldsrc-studio][pose]` tags.
The ASan run is core/API only; no concrete-HL `[e13]` tag is present in that
target. The extended independent alternate-module proxy assertions ran there.

Production Studio/OpenGL owned fixture evidence:
`alive_pixels=578`, `terminal_pixels=200`, separate corpse/respawn halves
`210` and `578` changed pixels, `studio_asset_upload_count=1`.
Behind-corpse flash stays depth-occluded; foreground flash changes pixels;
repeated frame is stable. A separate approved-import normal materializer test
renders its actual corpse palette, own floor light/body/skin, immediate body
queue reference/position reuse through three cycles, no p-model/socket output
and no reupload. These fixtures do not infer installed MDL death indices.
Missing/zero/max render fields, retention/delta, bad descriptor types,
truncated/malformed/duplicate atomicity, invalid/missing corpse references,
model-zero death cache boundaries, terminal clamp, fresh gait/visual epoch,
queued old-fire rejection and already-published shell contact all passed.

Initial compiler resource exhaustion was resolved by serial CL per target
(`/p:MultiProcessorCompilation=false`, two MSBuild workers), without clean,
changed safety flags or killing processes. The new tests initially had a
const move-only frame, an absent schema dereference and edge-on owned geometry;
these fixture defects were corrected before the final runs. Earlier logs are
preserved and are not final-pass evidence.

E13 changes relative to the pre-edit E11/E12 snapshot: neutral observation and
versioned visual hash (`client/runtime_observation`); neutral host/API headers
and forwarding (`game_api`); exact descriptor projection and post-commit
notification (`goldsrc/runtime_replay_session`); selected HL classifier,
numeric model cache, visual epoch and pending-event guards
(`games/halflife/client_module`, `remote_player`, `remote_effects`); normal
approved materializer and bounded diagnostics (`app/runtime_replay_local_assets`,
`apps/hlclient/main.cpp`); five existing test files extended; architecture and
E12 acceptance append-only records. No renderer, transport grammar, gameplay
authority or launcher implementation changed in this E13 delta.

Release executable:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`
(`3,535,360` bytes), SHA-256:
`33F6EF4995637961CD92230EAEDB46905D514AF8C76189D65BD073C2299EEFC5`.
Unchanged launcher selects precisely `build\bin\Release\hlclient.exe` under its
own source root; launcher SHA-256 remains
`7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`.

Full logs and source preservation inventory:
`manual-artifacts/task-records/e10-e13-20261002/`.
Those are diagnostics only; all required implementation/tests/docs remain
ordinary project source files. E13 manual fields above remain **not_run**.

## E13 follow-up: player-hit transport compatibility fix

New user report, attribution=user_report: shooting the other player closes
both clients. This is a failed E13 manual attempt, not death/corpse acceptance.
Exact latest run: `93940832c5124a83ac853bff9c4f5914`, crossfire, peer enabled,
unlimited duration; server PID 40508, receiver PID 34904, peer PID 39880.
Both receivers rejected source sequence 445 / record 430 with exit 2:
`svc_temp_entity` opcode 23, `unsupported_temporary_entity_type`.
Main message starts at byte 241, failure at byte 243, payload length 255.
The decoder stops immediately after the unknown subtype, not at a render or
prediction failure. The orchestration cleanup was exact, scoped restoration
exact, cleanup errors zero. Full-tree restoration was not attested in Fast mode.

Pinned official SDK `CBasePlayer::TraceAttack -> SpawnBlood -> UTIL_BloodDrips`
emits TE_BLOODSPRITE 115. Public format is exactly three coordinate shorts,
two sprite shorts, palette-color byte, scale byte: 12 body bytes / 14 bytes
including opcode and subtype. The reported packet geometry matches that
message. **Raw failed payload and exact subtype were not retained**, so this
is reference-backed likely identification, not stock-wire confirmation.

The decoder now owns this exact typed wire observation and accepts it in the
same staged control transaction. No blood particles/sprites, game damage,
local death, audio or file access is produced. The following Health/DeathMsg
continues through the existing game module. Unknown types remain fatal; there
is no guessed size, scan/resync, universal skip or forced successful exit code.
Future failures retain numeric `temporary_entity_type` (0..255 or unavailable)
through owning decoder error, application summary, native orchestration and
PowerShell report. Existing metric indices are unchanged; the native metric is
appended. This removes the identified missing hit-message grammar; another
unsupported subtype would be separately diagnosed rather than silently skipped.

Independent owned literal tests cover exact fields and lifetime, byte-241
alignment/byte-255 next message, every truncation, forged opcode, zero bytes,
malformed suffix atomicity, selected-module and protocol-only hit/health/death
delivery and duplicate rejection. A separate owning-error fixture checks
numeric unsupported subtype survives rollback and the normal fake-child/native/
PowerShell report path. No captured/private bytes or game assets are added.

Pre-edit snapshot: 1,285 hash-verified files under
`manual-artifacts/task-records/e10-e13-shot-hit-fix-20261002/`.
Historical Release SHA/results above remain historical; follow-up final results
and executable SHA are recorded below. Corrected build manual=not_run.
New live game/HLDS/Steam/managed sessions=0, staging/commit/push=none.

Follow-up final verification (2026-10-02, VS2022 v143 Win32):

- Release application/helpers build and affected Debug application/test/native
  targets passed, serial CL with two MSBuild workers; no clean build.
- Final full Release offline set: **2,653 passed, 38 explicitly skipped,
  zero failures**. Main retry ran 2,687 cases (38 skipped); all four
  process-sensitive cases ran separately, serially, and passed. The initial
  parallel suite observed job active-process count 1 rather than 0 in the
  unchanged suspended-process identity test. Its original failure log is
  preserved; neither that assertion nor process implementation was changed.
- Final Release focused decoder/hit/E12/E13/lifecycle/diagnostic regressions:
  **64 passed, 1 installed-asset opt-in skip, 11,586 assertions**.
- Final Debug focused set, additionally prediction/pose/E10/E11:
  **229 passed, 3 opt-in installed-asset skips, 169,137 assertions**.
- Core-only build with Half-Life OFF: **41/41 CTest passed**, including
  architecture guard; generated core targets contain no concrete HL sources,
  headers or linkage. ASan core/API: **16 cases, 1,048 assertions passed**.
  This does not establish full application/concrete-HL ASan runtime support;
  that prior limitation remains unverified, with no infrastructure repair.
- Owned production OpenGL E13 fixture ran: alive 578 / terminal 200 changed
  pixels, corpse/respawn halves 210/578, one Studio asset upload. These are
  offline geometry results, not live death/corpse confirmation.
- Owning-error fixture: 28 assertions; actual native fake-child and PowerShell
  report roundtrip retained subtype 12, original fatal error/exit 2, exact
  cleanup/restoration and ineligible failed evidence. Missing metrics stay
  missing in the offline roundtrip rather than being fabricated as unavailable;
  ordinary production status printing is unchanged.
- Unchanged launcher peer/unlimited `-CheckOnly` passed with zero launches;
  no-stock fake-runner fixtures passed, including reference/off, map selection,
  damage-respawn-check, bounded failure reports and restoration contracts.
  Installed-asset, interactive-device and WFP capability controls were not
  enabled. Project-owned positive tests did run without installed Half-Life.

The initial new literal fixture's following opcode/cursor setup and offline
roundtrip optional-field/coverage metadata were corrected before the final
runs; no old production assertions or unknown-subtype rejection were weakened.
Final full Release logs are `release-offline-retry-final.txt` plus
`release-serial-final.txt`; earlier attempt logs remain in the same task folder.

Current Release executable: `build\bin\Release\hlclient.exe`, 3,535,872 bytes,
SHA-256 `3163EA1FAF44C7A9C082D3D1F4B3EFAA75148D8E32ABA17F97504937CB0331D1`.
Launcher remains byte-identical to the SHA recorded above and selects this
exact Release path; no hidden flag or alternate executable is needed.

Manual handoff, **corrected build manual=not_run**:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Shoot the other player's torso, observe server-derived HP, then continue to
server death and inspect corpse/respawn. Close both clients manually and wait
for runner cleanup. The known 115 grammar is now accepted offline; the old
run's exact subtype and live correction still need this user test. If another
unsupported type fails, its numeric subtype is now available for diagnosis.
No new live sessions, staging, commit or push were performed.

## E13 follow-up: delayed death animation and respawn normal-base fix

New manual feedback, attribution=user_report: player killed by a headshot;
death animation jerks; both clients close after the reported respawn. This
confirms the user saw death, not smooth death animation or successful E13
respawn acceptance. No client hitgroup/headshot authority or feature is added.
Exact latest run `55a4a84643274e14bcf2f84393f21699`: server PID 27152,
client 4008, peer 36360, crossfire/reference/peer/unlimited. Both exit 2 at
record 970/source sequence 984, opcode 40 `svc_packetentities`,
`schema_mismatch`. Main message cursor byte 194 bit 2; payload 294 bytes;
last publication 969, local life epoch 1/state dead. Audio unsupported-sentence
and prediction coverage were not the fatal cause. Native job cleanup exact,
wrapper scoped restoration exact, cleanup errors 0; Fast full-tree restoration
not_verified. The run did not retain failed raw bytes, entity number or schema
sub-boundary, so it is not an exact wire replay and does not establish which
of the old schema checks fired. No executable SHA is attributed to this run.

Two concrete production defects were independently reproduced and corrected:

1. Full-message intra-base selection wrongly treated normal player and ordinary
   delta schemas as incompatible entity types. A server body-queue record can
   inherit a preceding normal player state. `GoldSrcPacketEntityDecoder` now
   reconstructs the destination schema by shared negotiated member identity,
   retaining owning values and source entity provenance. It keeps null members
   only where the source descriptor does not advertise them. No packet skip,
   guessed body length, opcode scan, full-snapshot fallback, category change
   in an existing delta record or forced exit success is introduced. Custom
   crossings and conflicting types/storage still reject atomically. See the
   existing generic packet contract for reference and limitations. This fixes
   a supported respawn-shaped message, not a claim to have captured the old
   failed packet's exact branch.
2. `HalfLifeRemotePlayerPresentation` sampled the newest nonloop anchor while
   the engine transform used a delayed clock. When sample time precedes the
   new animtime, the pinned SDK-compatible forward clock clamps its time delta
   to zero, exposing the newest wire frame as a packet-rate step. The HL layer
   now blends evaluated same-sequence nonloop endpoint frames at that same
   delayed presentation time. Numeric server sequence/frame/animtime/rate remain
   authority; loop/gait policy and local prediction are unchanged. Gap, teleport,
   model/life replacement, sequence change, rewind/restart and EF_NOINTERP
   suppress this interpolation. Terminal clamping, no gait/p-model after death
   and stateless independent corpse handling remain intact. This is bounded
   local-compatible presentation, not gameplay prediction or stock-exact timing.

Before edits, 1,285 files were hash-snapshotted under
`manual-artifacts/task-records/e10-e13-respawn-animation-fix-20261002/`.
The corrected independent fixtures were built against the still-pre-fix Debug
libraries with project references disabled, without restoring source or keeping
a second production path: both failed, 11/62 assertions failed, and the packet
reported `intra-message baseline has another schema category`. Initial fixture
mistakes (optional view wrapper, Catch INFO parentheses, missing staged time,
coordinate descriptor multiplier) are retained in earlier logs; final assertions
were not loosened. The first-render animation guard was corrected so an unused
module can consume a valid previous observation without a packet step.

The packet fixture also traverses production protocol-only RuntimeReplaySession
to neutral observations and duplicate rejection, with no Half-Life fallback.
Wrong custom category/type/storage traverses rollback and owns the failed
entity/category diagnostic. Future terminal/native/PowerShell summaries retain
bounded `packet_entity` and `packet_schema_boundary`; fields are appended to
the metric table, preserving existing indices. No native paths or raw bytes
are logged. New diagnostics do not convert a rejected record into success.
The death test consumes frame outputs through Studio composition and normal
OpenGL: start/middle/end nonloop poses have distinct nonempty pixel results,
repeat draws are stable and the existing single-upload corpse/respawn test
still runs. Independent boundary tests keep respawn/nointerp unsmoothed.

Final build/test results and prepared Release SHA follow below. Corrected
build manual=not_run. New live sessions/Steam/HLDS/WFP activation=0;
staging/commit/push=none. All implementation remains ordinary project source.

Final verification after the last production edit:

- VS2022/v143 Win32 Release and affected Debug targets built successfully.
- Complete Release offline suite: 2,655 passed, 38 explicit skips, zero failures
  across 2,693 case results (2,689 main results and four separately serialized
  process-sensitive checks). Logs: `release-offline-final-2.txt` and
  `release-serial-final.txt` in the task directory above. The earlier full run
  exposed a stale normal-tree core test binary carrying the fixture's old
  coordinate multiplier. All Release targets were rebuilt and the entire final
  suite repeated; the failed attempt is preserved, not counted as acceptance.
- Focused Release: 94 passed, three opt-in installed-asset skips, 11,181
  assertions. Focused Debug: 209 passed, three equivalent skips, 165,291
  assertions. Project-owned production OpenGL checks ran, including delayed
  death poses and corpse/respawn rendering; installed-asset skips are not live
  evidence.
- Core-only (`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`): 41/41 tests passed, including
  generic replay/packet regressions and the architecture dependency guard.
- Focused ASan core/API runtime: 34 cases / 2,493 assertions passed. Full
  application/concrete Half-Life ASan runtime remains not_verified; this result
  does not imply full-client ASan acceptance.
- Launcher peer/unlimited/reference `-CheckOnly`, no-stock launcher fixtures,
  client/native/PowerShell failure-summary roundtrip and scoped restoration
  checks passed without live Steam, stock client, HLDS or WFP activation.
  The roundtrip fixture covers the existing temporary-entity failure; non-null
  packet entity/schema-boundary ownership is covered by the generic replay test.

Prepared normal Release: `build\bin\Release\hlclient.exe`, 3,539,968 bytes,
SHA-256 `310B067A6E19BBF2202929ECDCC0A7012FF6C17DA5C2E0FF80F84D2E676F59C4`.
The unchanged launcher selects that exact path; launcher SHA-256 remains
`7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`.
Neither hash is retrospectively assigned to the failed manual run.

Manual handoff, this build **manual=not_run**:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Repeat the headshot/death observation, inspect the smooth nonloop animation and
corpse, then respawn and verify movement and Glock use in the new life. Close
both clients manually and wait for runner cleanup. A new user run is still
required to confirm that the unretained old packet's failure is eliminated;
offline supported-message acceptance is not a captured live replay.

## New user confirmation and spawn-orientation follow-up (2026-10-02)

New attribution=user_report: death animation no longer jerks and clients no
longer exit. This is manual confirmation for the previous death/respawn fix,
not an instrumented prediction/stock-parity verdict. The user did not supply an
executable hash with this message; no historical report is rewritten.

The next user report is independent: players initially face a wall, apparently
180 degrees reversed. Investigation found lost server view-angle delivery,
not evidence that all model axes should be rotated. The exact supported paths
and pinned SDK spawn contract are in `FIRST_PERSON_CAMERA.md`. Three new
independent tests built against the unchanged pre-fix Debug libraries reproduced
pre-baseline loss, post-record loss, and source-ID reuse across generations:
three failures, five failed assertions out of 77. The old focused Release
baseline passed 19 cases / 740 assertions; it tested immediate correction
consumption, not delayed readiness.

Production changes retain the latest correction in the generic replay bridge,
route its pre-baseline control through the existing bounded handoff, and dedup
camera consumption by generation plus original source. The application counter
now counts corrections also when clientdata is retained. No local +180 offset,
server/map/assets/cfg edit, new loop, gameplay rule or prediction kernel change
was added. The existing immediate-record test's absence-after-nop expectation
was intentionally updated to the new retained-delivery contract; its exact
degrees, source provenance and unchanged canonical hash assertions remain.

Source snapshot: 1,285 verified files in
`manual-artifacts/task-records/e10-spawn-view-orientation-20261002/`.
Final verification and prepared executable hash are recorded below. This new
orientation build remains manual=not_run; live launches=0, staging/commit/push=none.

Final orientation-fix acceptance:

- VS2022/v143 Win32 affected Debug targets and all normal Release targets built.
  New Release spawn regressions: four cases / 285 assertions passed (pre-baseline
  reference/off, delayed RX, malformed suffix rollback, replacement, session
  boundary, cardinal directions, mouse continuation and new-life correction).
- Focused Debug: 200 passed, three opt-in local-asset skips, 164,276 assertions.
  Existing project-owned E10/E11/E12/E13 OpenGL and prediction regressions ran.
- Full final Release suite after the last production edit: 2,655 passed and
  38 explicit skips in the main 2,693 results; four serialized process-sensitive
  checks also passed. Total: 2,659 passed / 38 skipped / zero failures across
  2,697 results. Logs: `release-offline-final.txt` (186.43 s) and
  `release-serial-final.txt` (8.64 s) in the orientation task directory. Skips
  are explicit opt-in installed assets/audio, WFP and unavailable link/capture
  capabilities, not positive live evidence.
- Core-only Release without concrete Half-Life: 41/41 tests passed, including
  the real dependency guard and the new neutral generation-aware camera test.
- Focused ASan core/API: 28 cases / 1,624 assertions passed. The first invocation
  exited `0xC0000135` before tests because the ASan DLL directory was not in
  PATH. Repeating with the existing official VS 14.42 Hostx64/x86 runtime added
  only to the child process PATH passed; no DLL was installed/copied or source
  infrastructure changed. Full application/Half-Life ASan remains not_verified.
- Unchanged launcher `-CheckOnly` reference/peer/unlimited passed with zero
  process launches. No-stock launcher fixtures passed reference/off, scenario,
  duration, map and exact-report contracts. `git diff --check` passed; the
  1,285-file source snapshot is hash-valid with no existing file removed, index
  empty and branch/HEAD unchanged. All 78 unrelated runtime-diagnostics files
  remain byte-identical.

Prepared normal `build\bin\Release\hlclient.exe`: 3,539,968 bytes, SHA-256
`2C028E0B41D56E7C68417C06CB53031B6BBAF2675D9AF2EBD2BF9B9E0F39D32C`.
Launcher SHA remains `7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`
and selects this exact executable. Historical hashes and manual outcomes above
remain historical; this orientation build is not yet user-confirmed.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Check both clients' initial direction and direction after respawn against the
server-selected spawn, then turn with the mouse: subsequent RX/render updates
must not pull the view back. Keep the previously confirmed smooth death and
surviving respawn as regressions. Close both windows and wait for cleanup.

## Subsequent user acceptance recorded at E14

`manual=passed`, `attribution=user_report`: the E14 request explicitly confirms
correct spawn/respawn direction, authoritative Glock kills, smooth death
animation, surviving hit/death/respawn, corpse/respawn lifecycle, renewed movement
and continuing gameplay. No executable SHA was supplied for this manual report.
Earlier failures, their corrections and historical not-run statuses above remain
unchanged. E14 scoreboard/death notices still require their own new manual test.
