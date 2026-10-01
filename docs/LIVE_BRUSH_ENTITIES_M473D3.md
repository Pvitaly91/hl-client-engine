# D3: live brush instances (offline implementation record)

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, starting HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`. Existing uncommitted sources
were preserved with the established verified snapshot mechanism (168 files)
under `manual-artifacts/task-records/d3-brush-prechange-20260927`.
Pre-edit offline baseline: 70 cases / 2,643 assertions passed.

## Cause and boundary

The actual live/replay provider rejected every inline `*N` as
`inline_brush_unsupported`, without a render binding. It also rejected every
nonzero entity render mode. The existing brush library and GL brush pass were
available only to the inspected static-scene path. These are concrete source
defects, not evidence that every missing object was a brush.

Now the canonical BSP import attachment retains the parsed document from its
single parser invocation, including submodels and bounded texture-directory
metadata. Preparation builds the existing world, brush texture/lightmap library
and spatial package once through approved local asset capabilities. There is no
`*N` file open, per-frame BSP import, alternate parser, or metadata-spawned live
entity. Temporary import document ownership ends after preparation.
Live/replay explicitly requests `GoldSrcBspParseOptions{true}` through built-in
importer composition. Default world-only import retains `{false}` and its
historical malformed-submodel/error-boundary behavior; no second live BSP parse
or weakening of validation is used.

Sparse model precache slot -> validated positive `*N` -> current map's exact
submodel -> shared immutable geometry -> owning `RuntimeBrushRenderFrame`.
Runtime entity number, sparse slot and submodel index remain separate. World
model 0 is never a brush instance. Invalid syntax/range/overflow is rejected
by the existing exact brush-reference parser; a provider rejects foreign
generations. New map resources require a new provider, as before.

Committed origin/angles use `make_brush_submodel_transform`, not Studio's
negative-pitch adapter. The supported QCSG entity-relative coordinate profile
rejects nonzero source-model origins rather than guessing a pivot. Bounds use
all eight transformed corners. Instances are discrete server observations,
matching the existing live entity presentation policy; no platform clock,
motion simulator or new interpolation/prediction path is introduced.

The immutable map scene is separate from owning per-record brush instances.
Studio/Sprite and brush frames stage in a candidate ClientWorldState and publish
together. Failed observations leave both old frames intact. Decoder delta
reconstruction/transaction authority is unchanged; clientdata-only dispatch
retains entity frames. Removal/model changes cannot leave metadata copies.
Reset clears brush resources/instances without inventing server instances.

## Materials and visibility

Normal (0) and trans-alpha (4) use the existing depth-writing textured/lightmap
pass and texture-index-255 cutout. `renderamt=0` is not an invisibility test for
these modes. Other entity blend modes remain explicitly unsupported, not
silently converted to opaque. EF_NODRAW remains hidden. No renderer classnames,
HP, Use, inventory or station rules were added.

Nonzero server brush frame selects an imported `+0`/`+A` alternative texture
bank, case-insensitively; missing alternative means the original texture.
The existing texture import operation validates size/profile and loads the bank
through the same BSP/WAD capabilities, including textures without world faces.
No duplicate geometry/lightmap upload or Studio evaluator is involved. This is
two-bank state selection, not general timed multi-frame texture animation.

Reference facts, not imported implementation:
[Valve health charger](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/healthkit.cpp)
uses frame for station state;
[FWGS brush renderer](https://github.com/FWGS/xash3d-fwgs/blob/master/ref/gl/gl_rsurf.c)
selects an alternate texture bank for nonzero frame and uses alpha-test without
entity opacity blending for GoldSrc trans-alpha. Actual installed-stock binary
equivalence outside this bounded profile is not claimed.

Frustum uses current transformed bounds at rendering time. Bounded leaf
membership is computed from current instance bounds; usable camera PVS can
reject a fully invisible membership. Missing, special, out-of-range or
insufficient membership conservatively falls back to frustum. Camera movement
does not depend on receiving another entity record. No always-on-top/global
depth/culling bypass or live-loop GPU pixel probe was added.

## Read-only crossfire audit

Existing canonical inspector/provider: 72 models including world 0; 3,738
world faces and 647 brush faces; 71 brush models; spatial-scene validation
passed. Below are **BSP entity ordinals, not runtime entity numbers**.
All listed source-model origins are zero and initial entity-origin keys absent.

| BSP ordinal / class | Model | Source faces (first/count) | Material / mode |
| --- | --- | --- | --- |
| 3 / func_healthcharger | *1 | 3738 / 6 | medkitedge1, +0medkit, black; mode 0 |
| 8 / func_wall | *6 | 3781 / 8 | {ladder3a; mode 4, amount 255 |
| 15 / func_illusionary | *12 | 3837 / 22 | {rail1; mode 4, amount 255 |
| 28 / func_door | *21 | 3943 / 14 | crete2_flr03a/c, generic031b; mode 0, amount 0 |
| 105 / func_recharge | *38 | 4127 / 6 | rechargeedge1, +0recharge, black; mode 0 |
| 135 / func_healthcharger | *40 | 4139 / 6 | medkitedge1, +0medkit, black; mode 0 |

The inspected map has no func_plat/func_train class. Its lift-like brush motion
can be represented by func_door; no runtime identity or motion is inferred
from its BSP ordinal alone. Ground items use a separate Studio path; local
model checks do not prove the state/effects of an unidentified historical item.

## Diagnostics and constraints

Terminal application evidence carries brush candidates/resolved/prepared/hidden,
unsupported materials, GL submitted/culled instances, geometry/texture uploads
and one bounded first rejection (entity, slot, submodel, reason, publication
revision). Per-record counts and accumulated GL submission counts are distinct;
neither is a pixel-visibility assertion. Native and PowerShell retain these
inert typed tokens. No packet/auth/body/asset bytes are published.

G1 ownership is unchanged: binding, transforms, culling and rendering are
generic engine mechanisms. Use, healing, inventory and life/weapon policies
remain in HalfLifeClientModule. Collision, scheduler, movement replay and the
50HP addon are not changed. Moving-platform collision/prediction remains a
separate boundary. New manual visibility is `not_run`.

Verification results and final Release hash follow below.
New live/Steam/WFP/capture/ETW sessions=0; staging/commit/push=none.

## Source/target ownership

| Layer | Ordinary project source | Responsibility |
| --- | --- | --- |
| `hlclient_goldsrc_bsp` / built-in importers | `src/goldsrc/bsp`, `goldsrc_builtin_asset_importers.cpp` | One explicitly selected canonical import profile and owning attachment |
| `hlclient_goldsrc_brush_models` | `src/goldsrc/brush_models/goldsrc_brush_render_library.cpp` | Submodel-only faces, base/alternative textures, lightmaps |
| `hlclient_application_runtime_replay` | `src/app/runtime_replay_local_assets.cpp` | Exact sparse bindings, committed transforms, atomic owning frames |
| Client / world-scene contracts | `include/hlclient/world_scene_render/world_scene_render_types.hpp`, `src/client` | Neutral immutable library and bounded runtime instances |
| `hlclient_renderer_opengl` | `src/renderer/opengl/opengl_renderer.cpp` | Cached GPU resources, current-bounds PVS/frustum, depth-tested draw |
| Application / native / PowerShell | `apps/hlclient/main.cpp`, orchestrator, `scripts/capture_stock_runtime_state.ps1` | Bounded terminal metrics and preserved first rejection |

No game module dependency is introduced by the application-runtime-replay
target's new dependency on `hlclient::goldsrc_brush_models`. GameClientHost/API,
HL1 module sources, collision and H3/H4 simulation sources are unchanged.

Verification uses the existing `build` (Debug/Release), `build-asan` (Debug)
and `build-core-g1` (Release, `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`) directories.
The core-only directory does not replace the manual launcher's Release.
The opt-in `[local-brush-control]` test takes `HLCLIENT_LOCAL_GAME_ROOT` only
for read-only asset inspection; all other new fixtures are project-owned.

## Verification evidence

Seed for deterministic D3/regression runs: `2532982175`. Logs are retained
under `manual-artifacts/task-records/d3-brush-prechange-20260927` (not required
source/build inputs).

- The single broad offline run selected 2,134 cases: 2,110 passed, 22 skipped,
  and two compatibility failures. Both failures exposed the importer-default
  regression described above. Old expectations were preserved, the default
  was restored, and both cases passed in the final focused rerun. The broad
  suite was not repeated after every change (nor rerun wholesale after repair).
- Final focused Release: 362 passed, one opt-in local-assets skip; all
  169,029 assertions passed. This includes the repaired historical world-only
  import/brush error boundary, strict `*N` rejection, entity reuse, committed
  delta/clientdata-only/failed-record behavior, B1/C/D1/D2 and H3/H4/A1 coverage.
- Final read-only crossfire OpenGL control: 43 assertions passed. Same-camera
  A/B changed pixel counts: `*1=4692`, `*12=2529`, `*21=71660`, `*38=4692`;
  GL error zero. These are inspected offline states with synthetic sparse
  slots, not claimed server entity IDs. Ground medkit/battery Studio bindings
  also import/project independently; the historical unidentified items' live
  effects/visibility remain unobserved.
- Synthetic GL controls test masked holes, world occlusion/depth, translated
  bounds/frustum rejection, alternate texture pixels and stable upload counts.
- Eight selected process-level offline replay/Null CLI checks passed.
- Normal Release core/API executable: 95 cases / 1,424 assertions passed;
  same-host alternate module has no Half-Life factory/fallback.
- G1 dependency guard passed allowed/direct/alias/transitive/generator/source/
  object/include cases. Native reporting self-test, PowerShell retained
  diagnostics/restoration test, 12 no-stock launcher fixtures and synthetic
  50HP profile tests passed. CheckOnly from System32 passed for reference/50HP.
- Broad skips are explicit opt-in local assets, unavailable Windows symlink/
  reparse/second-volume capabilities and hidden-SDL focus/relative capture.
  The D3 local-assets control was run separately, not counted as live proof.

The initial concurrent builds exhausted compiler memory. Bounded compiler
parallelism replaced that invocation; the two missing ASan objects were
recompiled directly without Clean, tree removal or source rollback. Failed
attempt logs remain intact. Final Debug and ASan Debug each passed the same
362 focused cases / 169,029 assertions (one optional local-assets skip), plus
95 core/API cases / 1,424 assertions. No AddressSanitizer error was reported.
Core-only Release completed for `hlclient`, `hlclient_core_api_tests` and
`hlclient_application_runtime_replay` with `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`.
Both selected CTests (`hlclient_core_api_offline`, `hlclient_game_boundary_guard`)
passed. Inspection of 149 actual generated project files found zero concrete
Half-Life source/library/project references. Normal Release remains separate.
Final `git diff --check` passed; no staged changes or HEAD movement occurred.

Primary result: `live_brush_entity_rendering_integrated_offline_verified`.

50HP helper SHA-256 remains
`AC96548EFF7E4B39FA34543C0E17CEC3DC666338332E971BCE7A97A0EA232703`;
crossfire BSP remains
`6222243E0839022F3041E6B9E97776CE54D0CF87D4C3810E627B8B2815B555F5`.
Of the 168 snapshotted pre-existing changed source files, 157 remain byte-identical;
the other 11 have the scoped D3 rendering/reporting/test/documentation edits.

## Manual handoff

Actual `build/bin/Release/hlclient.exe` SHA-256:
`467554397BBC86D50542B04582F5B48CE6CB6CB94499DEFAA8D3AF1B4A535659`.
The launcher and 50HP helper are unchanged; no new mandatory parameter exists.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```

This command was **not run** by the agent (only `-CheckOnly` was run).
New visibility manual validation=`not_run`; new live launches=0.
No Steam initialization, WFP activation, capture, ETW, staging, commit or push.
Historical reports are unchanged and the user's successful 50HP/interactions
are recorded separately with `attribution=user_report`.
