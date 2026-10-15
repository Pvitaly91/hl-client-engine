# E30 — original blood decals and blood-sprite scale

Task: `HLC-E30-ORIGINAL-BLOOD-DECALS-AND-SPRITE-SCALE`.
Status: offline completed; manual acceptance subsequently reported passed in E31.
Result: `original_blood_decals_and_sprite_scale_integrated`.

## User evidence and preservation

E31 follow-up acceptance: `manual=passed`, `attribution=user_report`,
`scope=reported blood decals/sprites and regression checklist` (blood decals,
spray/drops scale, Snark pop, HUD/kill notices and normal close). The original
offline handoff and `manual=not_run` entries below remain historical. E30's
actual report and pre-E31 file both identify SHA-256
`D38E036659AF3B9EB2E03B2671FB882FED363D65282766B6044DD5651817DC8A`;
this identity is not an independent attestation of the user's manual process.

E29 `manual=passed`, `attribution=user_report`,
`scope=HUD/inventory/crosshairs and reported working presentation`.
The E30 request supplies “HUD готовий, все працює”. This is acceptance, not a
new automatic test run or proof of every optional stock-renderer scenario.
E29.2's historical full-attempt timeout and isolated retry are not relabelled.
E27.1's previously accepted Snark pop remains accepted in its historical scope.

New complaint, `attribution=user_report`:
`blood_decals_on_surfaces=missing_or_not_observed`,
`blood_sprite_size_manual=failed`,
`reason=too_large_compared_with_original_half_life`.
New E30 `manual=not_run`; stock visual comparison remains `not_verified`.

Authoritative worktree `D:/DEV/CPP/HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`, origin
`https://github.com/Pvitaly91/hl-client-engine.git`.
Pre-edit source1,333 and Release rollback34 files were copied and SHA-verified
under `manual-artifacts/task-records/e30-original-blood-20261010` using the
existing preservation mechanism. No reset/clean/restore/stash, branch change,
staging/commit/push, foreign-process termination or other-worktree edit.
Pre-E30 normal executable SHA:
`C264160E03298B5F36408C2DDB19BC83D84092C140E649E80C620C827094FC83`.
That disk identity is not retroactively assigned to an unattested manual run.

## Established missing boundary and authority

All four requested decal TE grammars already existed: unsigned byte/HIGH index,
world without entity field and entity-decal with its own field order. Gunshot
and BSP formats remain distinct. `RuntimeReplaySession::stage_temporary_effects`
previously ignored `RuntimeControlDecal` and `RuntimeControlBspDecal`: parsed but
not presented. New neutral committed observations retain exact provenance,
position/index/target/subtype and optional BSP model reference after the existing
atomic commit. No new opcode or wire field is invented.

The existing ResourceList type3 mapping supplies actual server index/name.
The HL layer selects one of twelve compiled named dependencies from that mapping;
SDK enum, WAD directory ordinal and GPU handle are not network indices. Unknown
index/name does not select a guessed mark. Permuted-index fixtures passed.
The map-scoped effect is separate from blood sprites, Health, DeathMsg, shot
sound and Snark sound fallback. None of those alone creates a surface decal.

Pinned official SDK `b1b5cf5892918535619b2937bb927e46cb097ba1` establishes
TraceAttack -> TraceBleed -> UTIL_BloodDecalTrace -> UTIL_DecalTrace, runtime
DECAL_INDEX(name), world-coordinate position, explicit brush target, blood
visibility gates and TE_BLOODSPRITE fields. Damage/hit/violence settings remain
server-owned. No server/cfg/map/asset change or new live session is made.

## Approved original WAD evidence and material contract

One bounded `decals.wad` source is opened through the selected world's exact root,
verified locator and existing source opener. Fixed16 entries,4MiB source and
8MiB decoded cache bounds; one startup preparation, no frame/contact callback IO.
Metadata-only server names do not grant filesystem access. Case-insensitive
WAD identity uses the existing ambiguity-rejecting catalog contract.

Read-only actual audit: source924,840bytes, SHA-256
`17726E8174729011C7EA4E1E37B4CCFE7A9AA4ABB82C2AACA87872CBE0C106D0`.
Requested `{blood1..6` and `{yblood1..6` match uppercase source names, type67.
Red sizes48x48 except blood5=64x64; yellow48x48,32x32 and16x16; all four mip
levels are present. Palette0=(255,255,255),1=(254,254,254),127=(128,128,128),
254=(0,0,0). Palette255 contains each texture's blood tint (e.g. blood1=(22,0,0),
yblood1=(204,184,97)). Actual texels use0..254, never255; most backgrounds use0.
Ordinary index255 masking would therefore create opaque white/gray background
and lose tint. The initial exact-case audit failure is retained; the corrected
case-contract audit passed109 assertions before material conversion.

An explicit per-dependency `palette_tint_index_coverage` conversion validates
the inverse-gray palette, writes constant palette255 RGB and raw-index coverage
to straight-alpha RGBA at every mip. Ordinary masked/RGBA/map textures retain
their old interpretation; no global white key or installed-asset edit.
The inverse gray sequence is monotone with endpoints255/0, not the strict
`255-index` identity at every entry; audit reports these properties separately.
Constant transparent RGB avoids a white filter border. Lit straight-alpha uses
the selected surface's actual static lightmap values, not additive glow/fullbright
or the E5 white-neutral bullet modulation. Actual pixel verification passed
the72 wall/light combinations and288 exact transparent interior controls below.

## Surface geometry, ownership and limits

The existing WorldImpactPresentation64-entry FIFO also owns server marks;
there is no second blood decal or collision system. Exact received position and
explicit target select a uniquely validated nearby surface; shared triangle
clipping, asset dimensions, tangent basis and small bias materialize it without
a camera shot trace. Ambiguous unrelated planes fail with a typed reason rather
than wrapping through a thin wall. Static light comes from barycentrically
interpolated selected-triangle lightmap UV using the existing baseline layer.

Brush birth pose is frozen from the same committed record, not a later packet
or delayed rendered pose. Existing rigid-transform helpers project into exact
approved brush-model space; current render transform moves the attached mark.
Existing EntityFollow lifetime identity/birth floor prevents observed slot reuse
from inheriting an older placement. Render culling/NODRAW alone does not retire
an owner. Packet-list absence remains a conservative observed boundary; private
same-model reuse without any observed boundary cannot be distinguished.

World marks survive player death/respawn/leave and spray/kill-notice expiry.
They retire on map/resource generation, explicit target lifetime loss or bounded
eviction. Old local-shot20s expiry is unchanged. Renderer caches16 textures and
66 geometry buffers, with64 additional batch limit and typed rejections; stable
owning geometry does not force repeated uploads. No complete BSP rebuild per mark.
Dynamic lighting parity and exact proprietary corner-placement policy are not
claimed by this static baseline contract.

## Blood sprite cause, corrected field contract and Snark

Renderer audit found no double scale, half-extents, HUD/DPI scale, alpha-blood
minimum screen clamp or aggregate-bounds-as-frame defect. The legacy red/small
yellow profile instead assumed spray raw*.1 and one animated/fading drop raw*.05,
with guessed bright red tint(1,.05,.05). Large yellow used a different E27.1
profile. Yellow alone is not an emitter identity and no player hit is forced to16.

SDK proves server MP scaling already happens before `clamp(amount/10,3,16)`.
It does not implement the internal blood size-to-sprite multiplier. A separately
recorded compatible reference is
[FWGS R_BloodSprite, e9b63241616c390d4d1720ced80e88de2acf83a6](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/cl_tent.c),
blob`95c85f7c17cfb5f02f3088a544fab954ff2665e7`. Its blood-specific spray/drop
ranges inform independently authored bounded means raw/30 and raw/20. Density
is independently bounded min(16,raw+9), not raw equals drop count. These are
`compatible-reference-only`/local-compatible, not measured GoldSrc constants;
no implementation body was copied or retained.

Original64x64 spray and16x16 drops retain current-frame origins and one scale
application. Raw3/6/10/16 produce spray6.4/12.8/21.33/34.13units and individual
drops2.4/4.8/8/12.8units, with12/15/16/16 drop limits. Source palette195/247 is
required for both colors; absent palette is typed skip, not a bright guess or
delayed replay.20FPS/.5s spray and existing seeded ballistic/gravity/lifetime
values remain explicitly local-compatible timing, not auxiliary physics copies.

Actual TE uses its own received fields. Only validated Snark blast fallback
supplies inferred yellow195/size16 under the existing entity/generation/dedup
rules and same blood pipeline. The previous accepted Snark spray102.4/drop8units
becomes34.13/drop12.8units; before/after is recorded separately rather than
preserving incorrect math in a hidden Snark branch. Exact TE/fallback replay
ownership remains distinct from surface decals; no particle contact creates
permanent marks. No fuse, damage, sound duplicate, fireball or light is invented.

## Production paths, settings and bounded diagnosis

`src/goldsrc/runtime_replay_session.cpp` stages the existing exact decoder's
decal/BSP payloads into `client/temporary_effect.hpp`; the candidate record also
freezes a complete brush pose before atomic publication. GameAPI's owning
`surface_decals.hpp`, host/module query and `temporary_effects.hpp` carry neutral
recipes and placements. Default/alternate modules do not inherit HL names.

Half-Life `blood_decal_profile.hpp`, `blood_profile.hpp`, `temporary_effects.cpp`
and `client_module.cpp` own the twelve names, server-index selection, palette,
size policy and independent sprite/decal/fallback dedup. `goldsrc/decal_assets`
executes the approved compiled recipe; indexed-texture/WAD decoders expose an
explicit interpretation without changing ordinary texture semantics.

`RuntimeReplayLocalAssets::materialize_server_decal` executes the exact world or
brush selection; `WorldImpactPresentation::submit_server/frame_batches` owns
placement/clipping/persistence. `EntityFollowPresentation` supplies an existing
lifetime with a record birth floor. The normal `apps/hlclient/main.cpp` prepares
resources at approved projection readiness, binds ResourceList names, consumes
committed intents and attaches bounded batches to the ordinary RenderScene.
No feature flag, demo path, HUD sprite or camera-facing decal substitutes it.
The generic OpenGL pass handles neutral lit alpha and rigid transforms; Null
validates bounded input without claiming rendered pixels. Each immutable server
placement now retains its own geometry revision: adding another server/local
mark does not invalidate older VBOs. Legacy aggregate local-shot revision stays.

SDK `UTIL_ShouldShowBlood`/TraceBleed gates remain server-owned. A received
actual TE is the server's presentation authorization; missing TE never creates
a guessed local mark. The existing optional client blood policy continues to
gate inferred Snark sound fallback; it does not set/override server human/alien
violence cvars. No new client cvar synchronization or knowledge of private
server settings is claimed. A suppressed/absent server mark may correctly leave
only a blood sprite, or neither effect.

Opt-in net-trace terminal `[surface-decals-summary]` distinguishes wire/forwarded,
unresolved index, unsupported name, target invalid, stale/capacity, asset states
and geometry states. Asset states are ready/pending/absent/not_authorized/
open_failed/decode_failed/material_rejected/capacity. Placement includes duplicate,
stale generation, material rejection, unavailable target, surface unmapped,
ambiguous surface, geometry/lighting rejection and submission. Persistent pool
active and GPU batch counts are separate; renderer counts include other decal
passes and are not per-blood pixel proof. No per-frame spam or native asset path
is exported. There is no bounded pending-name replay: an unresolved event skips
once; a prepared surface intent allows at most2s delivery age without rebirth.

Evidence classification: server fields/authority and `DECAL_INDEX(name)` are
SDK-proven. The actual ResourceList type3/index-to-name transport is the existing
accepted adapter/corpus contract (`docs/GOLDSRC_RESOURCE_LIST.md`), exercised by
literal/permuted fixtures here; the SDK alone does not specify that transport.
WAD/SPR/palette properties are asset-proven; CPU/GPU fixtures prove this normal
pipeline's geometry/material/frame behavior. Scale distribution means, drop
budget, ballistic timing, asset-width world extent and static-light blend are
documented compatible approximations. Matching stock capture, proprietary
placement/lighting parity and real manual focus/brush/yellow scenarios remain
unverified. Scorch names remain typed unsupported; E30 does not silently add a
second non-blood map-effect milestone.

Local renderer readbacks (not stock captures) are under the task record's
`visuals/`: `player-hit-scale-before-after.png`, `original-decal-walls.png`,
`snark-scale-time-before-after.png`, plus isolated current/legacy spray/drop and
birth100/250/450/700/1000ms frames. Snark size16 is shown separately, not claimed
unchanged from the historical accepted image. No matched stock screenshots were
established for the requested resolution/FOV/distance/settings combination.

## Verification and delivery

The following results were obtained after the last production edit, using normal
production components and bounded offline fixtures. Manual and stock visual
comparison remain separate; ASan compilation is not sanitizer execution proof.

Completed after the final immutable-VBO production edit:

- Full VS2022/Win32 Release build succeeded (`release-final-build.log`).
- Mandatory E30:46 cases,43 passed/3 explicit optional-asset skips,
  11,507 assertions passed (`release-e30-final.log`), including Null, literal
  protocol, geometry/lifecycle/bounds/cache and source-owned GPU checks.
- Approved original-assets E30:3 cases/97,610 assertions passed, no skips
  (`release-actual-assets-final.log`). Twelve WAD variants,72 wall/light cases
  and288 transparent interior GPU controls; all four CPU mips and original SPR
  frame/origin/scale/FOV/time readbacks. This is not a stock capture.
- Release alternate module:3 cases/75 assertions, no HL dependency
  (`release-alternate-module-e30.log`). Core API/architecture CTest2/2 passed
  (`release-core-api-architecture.log`); the entire separate core configuration
  completed below, as did the full final Release.
- Requested launcher +`-CheckOnly` passed, native `process-launches=0`, no
  game/Steam API/WFP/socket session. Its unchanged ClientPath joins the normal
  Release executable; Debug is not a candidate.
- Source snapshot1,333 and rollback34 SHA entries reverified: missing0,
  unchanged preexisting source1,295, task-modified38. Prior versions remain
  intact (`preservation-current.json`); diff check clean, staging none.

First failures are retained, not relabelled as successful runs:
`release-compose-build.log`: incorrect test-factory/metadata/INFO expressions,
repaired using actual APIs. `release-e30-first.log`:144 supposed neutral points
were actually opaque ink, plus an obsolete entity-bound assertion. Corrected
controls prove transparent3x3 CPU neighborhoods and actual VP projection while
keeping exact GPU RGBA assertions. `release-e30-fixtures-corrected.log`: shared
host finish/teardown fixture executed an older binary; explicit teardown and
both ownership boundaries now pass. `release-actual-assets-first.log`: a forced
quadrant was not transparent in original16x16 yellow6; four distinct separated
real transparent patches now pass the same exact GPU checks. Independent review
found a genuine global-VBO-revision defect; stable per-placement revision and
A/B/local-shot upload regression fix it. `debug-final-build.log` records C1060
heap exhaustion during two large test compilations; the task-only serialized
compiler retry built the entire Debug test executable successfully, without
source/assertion weakening (`debug-serial-retry-build.log`). The first separate
core build was explicitly stopped only in its own descendant process tree when
system memory commit became scarce, not called a build pass; its final serialized
repeat hit C1002/pass2 and subsequent MSBuild out-of-memory/tlog failure
(`core-serial-final-build.log`). A quiescent repeat after all other jobs finished
passed (`core-quiescent-final-build.log`). No foreign process was stopped or
compiler/assertion settings relaxed.

Separate core-only configuration `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` rebuilt
the normal client and core API test target. Full core CTest42/42 passed,
0 skips/failures,20.69s (`core-full-final.log`); alternate module and architecture
guard included. Core Null client passed one frame with no game assets
(`core-null-final.log`). This is independent of the earlier HL-enabled build's
neutral test executable and confirms the no-HL transitive boundary.

Final focused Debug:266 cases,252 passed/14 explicit opt-in skips,107,836
assertions (`debug-focused-final.log`), covering E30, E27/E27.1, E29/E29.1/E29.2,
temporary effects, audio and impacts. Separate original-assets Debug E30:
3 cases/96,758 assertions passed, no skips (`debug-actual-assets-final.log`).
Release's extra852 assertions verify its enabled framebuffer artifact exports;
the underlying Debug CPU/GPU assertions and72/288 material controls are unchanged.

Pre-final relevant suite:296 cases,283 passed/13 explicit asset skips,790,510
assertions (`release-relevant-first.log`). This targeted result does not replace
the required final full Release suite.

Clean full final Release after the last production edit:
`release-full-final-first.log`, driver exit0. CTest3423 registered:3360 passed,
63 explicit skips,0 failed; total698.18s. No failed timeout/isolated retry is
substituted. The existing E28.2 final-close/verdict test passed116.67s within its
unchanged180s deadline. Full source driver additionally passed version/help,
Null, transport/log self-checks, summary fixtures, no-stock launcher/preservation
and source-only CheckOnly postchecks. Existing capability/symlink/private corpus
and local-asset opt-ins account for the63 skips; E30's3 actual-asset skips were
separately satisfied by its explicit approved-assets pass. Historical E29.2's
timeout/retry remains historical and is not rewritten into a clean prior pass.

ASan: the current normal `hlclient` target compiled/linked successfully in the
existing Win32 Release ASan configuration (`asan-client-final-build.log`), scope
normal-client target, not a claimed full sanitizer test-suite build. Its fresh
`--help` startup exited signed-1073741515 / `0xC0000135`, output lines0
(`asan-runtime-final.log`); it cannot enter the control command. E30 sanitizer
execution therefore remains unavailable/not verified. The earlier loader
limitation is not resolved, no sanitizer pass is claimed, and no DLL copying,
global PATH, loader or ASan-infrastructure repair was attempted. Normal Release,
Debug, core, CPU/GPU and Null passes above are independent of this limitation.

Current normal Release:4,147,712bytes, SHA-256
`D38E036659AF3B9EB2E03B2671FB882FED363D65282766B6044DD5651817DC8A`.
Identity is checked again at handoff, not assigned to a prior manual session.
Original SPR hashes: bloodspray
`86769469CF458DF8114A7239A9C1B5018ECC8CC2E443D679EB43BAD5B70EFB89`,
blood `42CABFBAC848496FDE138163BC36DE3E71B7E7B4E51926F6F4F9A2FF685F19C8`;
palette768bytes `036B7EA3C8C03BE214BB7D22B8D63AC27F63D547E42CA677FEF344DDC780961E`.
These files were read only, never edited or added to repository assets.

Ready executable:
`D:/DEV/CPP/HLC-steamcfg-5e48b7c1/build/bin/Release/hlclient.exe`.
Final machine-readable counts, skips, identity and ASan distinction:
`manual-artifacts/task-records/e30-original-blood-20261010/final-verification-summary.json`.

E30 manual=not_run; stock_visual_comparison=not_verified;
new live/Steam/HLDS/capture sessions0; staging/commit/push none.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Manual: B stands close to a normal wall; A hits B. When a server decal is sent,
check original blood texture/no rectangle, persistence after sprites and camera
movement, both recipients, smaller varied spray/drop footprints, Snark no
duplicates, no invented marks for a distant wall, respawn persistence, A->B->A
HUD/kill notices and normal B/A close. Yellow/moving-brush cases require an
actual server scenario; synthetic proof alone is not manual acceptance.
