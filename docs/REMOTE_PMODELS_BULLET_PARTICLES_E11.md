# E11 — remote p-models and bullet impact particles

Task `HLC-E11-REMOTE-PMODELS-AND-BULLET-IMPACT-PARTICLES`, 2026-10-01.
Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, starting HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. No staging/commit/push.
Pre-edit source inventory and Release baseline are preserved under
`manual-artifacts/task-records/e10-e11-20261001/`; old loose diagnostics and
other worktrees are untouched. No clean/reset/branch switch or old-build cleanup.

## Manual attribution

New user report: “Зробив ручний тест 2-х клієнтів все працює”. E10
`manual=passed`, `attribution=user_report`, scoped to the reported two-client
body/animation and post-E10 behavior. This is not numerical gait/interpolation
evidence and does not identify the launched binary hash. Historical E10
offline `manual=not_run` reports remain unchanged. New E11 build:
`manual=not_run`. New stock/HLDS/managed/live/Steam/WFP sessions: zero.

## Before → after, actual production paths

| Before | E11 integration / owner |
| --- | --- |
| Committed `weapon_model_index` existed, but only the body was drawn | Half-Life `remote_player.cpp` emits the exact nonzero **sampled** slot through `RemotePlayerPresentationIntent::attachment_model_index` |
| Approved Studio models were already imported and retained | `RuntimeReplayLocalAssets::project_entities` resolves that type-local slot in the same library/package; no filename guessing or per-frame I/O/import |
| Frame allowed only one Studio instance per entity | Neutral `subinstance_ordinal`: 0 primary, 1 attached; exactly one of each, original server entity number |
| No weapon bone merge | Shared `StudioBoneMergeCache::prepare` and `StudioPoseEvaluator::merge`; final composed player palette → child palette |
| Glock impacts already traced, clipped, sounded | HL local `WeaponVisuals` and committed `RemoteWeaponEffects` add the same numeric profile to existing impact requests |
| No short impact particles | Existing `WorldImpactPresentation` feeds one neutral `ImpactParticlePool` after supported static BSP hit, before decal readiness check |
| GL drew decals/flashes/shells | `RenderScene::world_particles` carries neutral world-space billboards; normal OpenGL world pass draws them before first-person depth reset |

No second entity, network history, renderer, physics engine, resource pipeline
or gameplay authority is introduced. `--RemoteAudioPeer`, reference/off,
crossfire default, `-DurationSeconds`, `-NoTimeLimit`, external maps and existing
damage-respawn scenario remain unchanged. There is no E11 feature flag.

## Reference boundary

Pinned official Valve SDK remains
`b1b5cf5892918535619b2937bb927e46cb097ba1`, without submodule update or copied
implementation. `dlls/client.cpp` publishes the server's weapon model index;
`common/entity_state.h` defines it. `cl_dll/StudioModelRenderer.cpp` uses
case-insensitive bone names, copies matched parent transforms, and evaluates
unmatched child sequence/controller local transforms through the child hierarchy.
This independent implementation preserves those mechanisms and validates
ambiguous names/hierarchy instead of accepting malformed input.

`cl_dll/ev_hldm.cpp`, `EV_HLDM_GunshotDecalTrace`, requests
`R_BulletImpactParticles` before optional ricochet/decal. That API does **not**
provide the proprietary stock engine's particle implementation. E11 particles
are explicitly **local-compatible**, not stock-exact simulation or screenshots.

## Attachment ownership, bounds and failure semantics

The game selects a slot; the host receives a numeric optional intent. The shared
materializer operates only on retained approved Studio data and the existing
final player pose (main/transition/gait/pitch/controllers/blending). Same entity
transform and sampled static light are reused. The local receiving player remains
excluded from third-person attachments; its camera-local viewmodel is unchanged.

Mapping owns numeric indices and immutable asset identities/fingerprints, not
RX/native pointers. First selection prepares a pair; subsequent frames use the
64-entry FIFO pair cache, keyed by both identities/revisions. Up to 128 bones
per model; names are bounded to 64 bytes and ASCII case-folded. Duplicate names,
cycles, invalid parents, no shared bones or incompatible retained mappings return
typed errors. Matched bones copy the parent's final model-space palette exactly.
Unmatched bones use evaluated child local animation/controller transforms,
concatenated through already merged child parents; unmatched roots use their
evaluated local root, never an invented identity bind-pose replacement.

Child sequence uses the player sequence if available, otherwise zero. Body/skin
default to zero, independently validated against the child. Frame uses the
existing wire frame, animtime and framerate with the child's FPS; ordinary
Studio looping/clamping still applies. This is a bounded presentation context,
not a claim to reproduce every proprietary child-animation interpolation detail.
Render bounds come from the merged skinned vertices, not body bounding boxes.

Unknown/zero/missing/non-Studio/unsupported material/failed merge produces no
attachment and never retains the previous weapon or hides the owner. Map reset
clears the pair cache; life/model/slot/removal continuity still belongs to E10.
No model is imported or uploaded again just because it switches away and back.
The existing entity-package GPU cache owns immutable meshes/textures.

Frame uniqueness permits exactly one primary plus one attached Studio instance;
orphan/duplicate/other ordinals and collisions with sprite/unsupported entities
remain rejected. Ordering groups the owner and child: all opaque/masked primary
commands precede the child's commands. A precomputed numeric owner lookup avoids
repeated whole-frame searches during draw sorting. Existing resource/pose/draw
bounds and normal PVS/frustum/depth mechanisms remain active.

## Particle ownership and numeric profile

The values/bounds below record the original E11 implementation. The later
2026-10-03 [E15 glow/density tuning](MP5_PRIMARY_FIRE_E15.md) supersedes the
particle profile and per-burst cap, while retaining this eligibility/ownership.

Half-Life alone chooses Glock eligibility and profile: 8 particles, 0.28 s,
35–65 units/s outward hemisphere speed, 180 units/s² downward gravity,
0.55-unit radius, muted tan RGBA (0.62, 0.57, 0.46, 0.85), linear alpha fade.
This is a short dust/chip burst, not a persistent flame, tracer or modern
explosion. Crowbar receives no bullet profile. Miss, startsolid, sky/liquid,
unsupported/ambiguous/unmapped static face and nearer unsupported brush do not
create it. Existing trace/material/audio/decal positions are unchanged.

The shared pool does not interpret weapon IDs, model names, materials or sound
paths. Both local accepted command and committed remote event converge through
the **same** impact owner. Decal or audio absence does not veto valid particles;
particle failure does not veto those independent effects. No new asset dependency.

Fixed storage: 32 bursts × maximum 8 particles = 256 particles; 128 recent causal
identities; 32 diagnostic entries. Oldest burst is deterministically overwritten
on saturation (counted as eviction), never growing allocations. Seed uses causal
generation/resource/action/emitter metadata, not frame count/global RNG/wall clock.
Local and remote actions have independent emitter identities. Absolute-age
ballistics make repeated sampling and frame slicing deterministic. No extra
collision or authoritative trace is performed for particles.

Requests older than 250 ms, invalid finite/profile bounds and different active
generation are typed rejections. Map/life teardown clears active and pending
effects through the existing owner's resets. A first valid action anchors the
generation after reset; the host remains responsible for excluding stale records.
The bounded recent-action guard is not an unlimited journal of all historical
actions. Expired bursts are not regenerated by normal render/replay updates.

GL shares the existing owned billboard VAO/program, with a neutral radial-only
shape and straight-alpha blending for particles, not muzzle rays/additive light.
Each finite bounded world billboard retains world depth testing; depth writes,
culling/blend state are restored. Particles neither clear depth nor alter HUD,
Studio/chrome, first-person flashes, lights, decals or shell simulation.

## Diagnostics

Existing opt-in `--net-trace` remote visual budget adds exact attachment slot,
typed absent/resource/merge/pose/material/submitted/visible/culled status and
matched/unmatched bone counts. The impact owner retains a 32-entry numeric
journal; application output has a 64-line session budget, causal serial,
generation/command/emitter, local/remote source, hit point/surface, requested
count, spawned/duplicate/invalid/stale/expired status. Saturation is separately
counted as deterministic eviction. Existing world-hit rejection counts cover
invalid/unavailable/unsupported/miss results. No native paths or matrices are
logged. Submission/voice counters are not claimed as pixel/listening evidence.

## Verification and manual handoff

Required owned fixtures cover generic case-folded merge/unmatched hierarchy,
mapping ambiguity/cycles/revisions/reset/cache bounds, sixteen mixed-material
owner-child pairs, exact manifest slots W1/W2/zero/missing/unknown, normal host
replacement and unchanged network state, isolated OpenGL body/weapon A/B and
switching with upload reuse. Particle fixtures cover hit/no-resource/miss/crowbar,
duplicates, different emitters, finite trajectories, frame slicing, expiry,
capacity/eviction/reset/stale/invalid profiles and isolated depth-tested GL pixels
without decals. Existing local/remote shell/audio/movement regressions remain.
Core-only tests use owned numeric profiles and no HL library/assets/device.

Optional exact prepared-root `p_9mmhandgun`/`p_crowbar` controls use the normal
approved importer, host, merge and GL path, verify input write times unchanged,
and remove only the child draw for an independent pixel A/B. They are not server
capture or live stock parity evidence. No private models are copied into Git.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer
```

On crossfire: inspect the other player's Glock, switch Glock → crowbar → Glock,
walk/strafe/jump/crouch and inspect aim. Shoot supported walls/floors: short impact
particles must coexist with decals/material audio and disappear promptly. Check
both receiving roles, no third-person duplicate on the local viewmodel, no ghost
weapon after removal, and unchanged light/shell/audio/movement behavior.
Use existing `-NoTimeLimit` or `-DurationSeconds` only if desired. Single client
uses the same command without `-RemoteAudioPeer`. New E11 manual remains not run.

### Final verification of the E11 source

Normal VS2022/v143 **Win32** Debug and Release targets built, including the
Release client and existing helpers. The first parallel Release build exhausted
the 32-bit compiler host heap (`C1060`); retry used the existing x64 compiler host
and two workers, still targeting Win32, without changing source/compiler flags
or cleaning the build tree. Initial failure logs remain available.

| Final check | Result |
| --- | --- |
| Latest focused Debug: E11/E10/Studio/entity/remote effects/impacts/weapon visuals/audio/prediction | 284 passed, 11 opt-in skips; **254,795 assertions passed** |
| Latest focused Release E11 | 10 passed, 1 opt-in skip; **3,725 assertions passed** |
| Full final Release offline catalog, after the last production and test edits | **2,633 passed, 38 skipped, 0 failed** out of 2,671 |
| Core-only Debug, `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` | All engine/tool targets built; **41/41 CTest passed**, including core/API and transitive architecture guard |
| Optional approved installed p-model control, Release | **42 assertions passed**; exact Glock/crowbar slots, each 11 matched + 1 evaluated unmatched bone, isolated actual GL A/B and upload reuse |
| Existing bounded ASan core E11 check | **5 tests / 812 assertions passed** with the matching already-installed runtime DLL directory added to this process's PATH only |
| Unchanged launcher `-Prediction reference -RemoteAudioPeer -CheckOnly` | Passed; process launches/Steam/WFP/sockets = 0 |
| Existing no-stock launcher fixture | Passed, including reference/off, timing, external maps and scenario contracts |
| `git diff --check`, empty staging index and source snapshot preservation | Passed; original source files retained, no snapshot hash errors |

Full offline CTest was partitioned: 2,668 catalog entries ran with four workers;
the three fake/process-inventory-sensitive startup/profile fixtures ran serially
afterward, all passed. This covers the whole catalog, not permanent exclusions.
WFP capability execution was explicitly disabled. The 38 skips are reported
capability/opt-in resource/device checks, not passes; the installed E11 p-model
test skipped in the owned-fixture catalog was separately passed above. Actual
owned OpenGL attachment and particle pixel/depth/expiry tests passed. Null,
process-level application checks, audio and prediction regressions also passed.
No live game or server was started by these checks.

ASan's initial default launch still failed at DLL loading (`0xC0000135`, no test
execution). A process-local PATH to the matching installed MSVC runtime allowed
`--help` and the freshly built core E11 tests to run successfully. Nothing was
installed/copied and global PATH was not changed. This is **not** a full ASan
application, audio-device or full-suite result; historical runtime failures are
not rewritten. No device listening or E11 user manual result is claimed.

Evidence logs are retained under `manual-artifacts/task-records/e10-e11-20261001/`:
`focused-debug-final-latest.txt`, `e11-release-final-2.txt`,
`release-offline-final-main.txt`, `release-offline-final-isolated.txt`,
`core-offline-final-retry.txt`, `pmodels-release-readonly-final.txt`,
`asan-e11-final.txt`, `launcher-checkonly-final.txt`, `launcher-nostock-final.txt`
and `source-preservation-final.json`. The 1,279-file pre-edit snapshot and older
attempt logs are preserved; none is substituted for the final run. Earlier new
fixture expectation/clock-anchoring errors were corrected without weakening old
assertions or altering production E10 behavior.

Final normal executable: `build/bin/Release/hlclient.exe`, 3,512,832 bytes.
SHA-256:
`3299E100B955A4F330F058C6A7128D6626FF695B06F36662572BE37F1BB30503`.
The unchanged launcher derives this exact Release path from its own worktree.
This hash identifies the prepared E11 binary, not any earlier user launch.

Remaining limits: one selected child per remote player, public bounded child
animation context rather than all proprietary interpolation details, and the
documented static-BSP Glock local-compatible particle slice, not stock-exact
effects, other weapons, dynamic damage or a generic particle system. New E11
manual = **not_run**; new live/stock/HLDS/managed sessions = **0**;
staging/commit/push = **none**.

## Subsequent manual confirmation before E12

New user report: “ручний тест показав що все працює”. E11 `manual=passed`,
`attribution=user_report`, for remote server-selected p-model visibility,
weapon switching, movement attachment and the tested local/remote bullet-impact
particle scenario. No granular stock-exact result, run metadata or launched
binary hash is invented. Historical E11 offline/manual-not-run entries above
remain unchanged. Particles remain local-compatible, not stock-exact. E12 is
a separate build requiring a new manual check; see `REMOTE_GLOCK_PMODEL_FIRE_E12.md`.
