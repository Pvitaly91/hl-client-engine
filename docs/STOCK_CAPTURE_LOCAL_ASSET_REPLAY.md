# Recorded stock scene with explicit local assets (M4.7.1.2I)

The application now has an executable offline local-asset provider, separate
from G's project-generated diagnostic model and the strict evidence-pending
stock adapters. It uses one `RuntimeReplaySceneSource`, one
`RuntimeReplaySession`, and the existing `ClientWorldState`/OpenGL application
loop. No socket, authentication provider, download, stock executable, DLL,
event/script execution, prediction, weapon simulation or live game is involved.

## Explicit invocation

From `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`:

```powershell
.\build\bin\Release\hlclient.exe --renderer opengl `
  --runtime-replay-capture '.\manual-artifacts\research-runtime-capture\ededd068a0444ff497d98204eacec8a4' `
  --runtime-replay-visuals local-assets `
  --basedir 'D:\DEV\HLCLIENT-RESEARCH\Half-Life' --game valve
```

Optional `--runtime-replay-screenshot <png-output>` saves the real completed
backbuffer through the platform image writer, before swap, with corrected row
orientation. It requires local-assets/OpenGL. It does not capture a desktop or
generate an illustration. Only this explicit option writes an image; the
capture and game roots remain read-only.

The unchanged H control command is:

```powershell
.\build\bin\Release\hlclient.exe --renderer null `
  --runtime-replay-capture '.\manual-artifacts\research-runtime-capture\ededd068a0444ff497d98204eacec8a4'
```

That command never creates the local-asset provider or reads game files.
Local-assets with `--renderer null` exercises the same CPU package/frame
composition without pacing or GL. Live connect/authentication, consistency,
stop/view and unrelated camera options remain incompatible with replay.

## Ownership and exact binding

The production functional corpus loader, transport reassembler/decompressor,
sign-on parsers, baseline decoder and A/B/C/D runtime dispatcher are unchanged
as decoding authorities. Sign-on now retains an owning `ResourceListState`
alongside ServerInfo and schemas; graphics does not decode packets again.
The captured server map path is resolved through the actual resource list and
`PrecacheManifestBuilder`, not a synthetic manifest or directory-name guess.

`PublicGoldSrc48ModelResolver` resolves an explicit public-reference model
slot only in the manifest's sparse **model** table. Its shared exact-table
validation does not convert the input to a diagnostic/synthetic reference.
Entity number, wire ordinal, type-local slot, library index and renderer asset
index remain different values. The library's reference-only planning entry
point shares the existing import/publication implementation. Same-source alias
references point to one library record; multiple entities keep that shared
resource when another entity is removed.

Root construction, ASCII virtual-name validation, reparse/containment checks,
locators, verified reopening and bounded reads are the existing local-resource
capabilities. The only explicit root is the supplied basedir/game. There is no
Steam-installation fallback. The world-texture path uses the existing 64 MiB
file profile (the stock `halflife.wad` exceeds the generic 16 MiB default).
Imports prepare in batches of eight under unchanged library hard limits.
Library/texture/geometry/pose limits remain enforced. No executable is loaded.

Positive recorded model/BSP size codes are compared to the approved local
file size. Zero and the opaque `0xFFFFFF` code do not establish a size match.
A mismatch is an explicit preparation failure, not model substitution.
Successful import establishes path/format compatibility and current local
fingerprints, **not historical byte identity**. Captured map CRC/client-DLL
metadata is not verified as a byte-identity assertion in this slice; WAD and
model-companion historical hashes are unavailable. Whole Steam userdata is
neither read nor checked.

## Semantic and presentation contract

Only exact ordinary/player entity schema names are projected for this slice.
Custom/beam schemas do not inherit ordinary model semantics. Exact descriptors
are checked before reading committed canonical values: modelindex, sequence,
body, rendermode and effects use unsigned integer; frame uses unsigned float;
skin uses signed short; controller/blending values use unsigned byte. Missing
fields stay optional in the observation, and mismatched present descriptors
reject the semantic transaction. No native offsets or fuzzy names are used.

For supported Studio presentation, missing optional sequence/frame/body/skin,
controller/blending and mouth controls use explicit zero presentation defaults;
scale is one. These are not fabricated wire observations. Origin/angles and a
nonzero model reference are required. Studio frame coordinate is
`decoded_frame * (sequence_frame_count - 1) / 256`, without time extrapolation
or animation events. The existing pose evaluator receives the separate
`public_goldsrc48_discrete_local_asset_v1` profile. GoldSrc pitch/yaw/roll is
adapted to the existing XYZ render transform as roll/negative-pitch/yaw for
Studio, then the existing matrix, bounds/material composition primitives and
`EntityRenderFrameBuilder` own neutral frame validation.

Supported materials are the existing opaque/masked Studio profiles. Nonzero
render modes, no-draw effects, unsupported material/pose/schema, missing model,
unknown slot, unsafe/ambiguous source and dependency/import errors have separate
outcomes. Other visual effects are not simulated. Sprite support is limited to
the existing non-additive supported orientations with one image; animated/group
variants are unsupported. Inline `*N` is retained as a typed map-submodel
reference and never opened as a path; dynamic inline-brush composition remains
unsupported here. Missing models never become procedural substitutes.

The old A-H canonical observation hash remains the same defined field set.
`visual_semantic_hash` additionally covers missingness and all committed visual
fields, so preserving the old hash is not presented as proof of un-hashed
visual values. Clientdata-only records retain the exact frame/package; entity
records replace only the packet-set frame. A decode failure never invokes the
projector. A fatal visual failure leaves the previous complete visual frame
and the already committed decoder observation/history intact. Restart clears
local world/package/bindings; a provider rejects a foreign generation.

## Timing and spectator camera

OpenGL uses the existing F/G per-record offset scheduler. An offset is the
prefix maximum of observed datagram timestamps through the delivery ordinal
which produced that owning payload, rebased at the first runtime record.
The transport's completing-fragment provenance therefore prevents presentation
before all required fragments are available. This is an **observation-clock
presentation policy**, not an exact client delivery timestamp. All records are
applied in their original order; a frame can present several intermediate
commits, but no decoder record or delta chain is skipped. Null mode remains
immediate and deterministic. The selected run spans 14.8494 presentation seconds.

The camera is a one-time spectator view around the first supported decoded
model for which placement succeeds. Eight bounded candidate traces use the
existing canonical BSP collision attachment/query, keeping the camera before
a wall. It never follows/re-fits later records and does not infer local-player
identity from entity 2, health, weapon observations or `svc_setview`. It is not
the original player's camera. The recording contains the received packet set,
not every object in every part of the map. EOF holds two seconds and closes via
the existing lifecycle. An early close/limit is incomplete, not success.

## Pinned public cross-checks

No third-party function or test was copied or compiled. The independently
written adapter uses these protocol/format facts:

- Valve `b1b5cf5892918535619b2937bb927e46cb097ba1`,
  [network/delta.lst](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/network/delta.lst):
  ordinary/player field descriptors and the distinct custom schema.
- Same Valve revision,
  [StudioModelRenderer.cpp](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/StudioModelRenderer.cpp):
  discrete 256-based sequence-frame conversion and Studio pitch sign.
- ReHLDS `6266cd23faee4a6e9cf3974f9605b2cadd86f0a4`,
  [sv_main.cpp](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_main.cpp):
  model precache slot becomes the model resource index; the resource serializer
  transmits that index and the 24-bit download-size code. Resource-list order
  is not modelindex.
- GoldSrc-compatible Xash3D `e9b63241616c390d4d1720ced80e88de2acf83a6`,
  [cl_parse_gs.c](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/parse/cl_parse_gs.c)
  routes resource lists to
  [cl_parse.c](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/parse/cl_parse.c)
  with the GoldSrc protocol selector; type, explicit resource index and size
  code remain separate fields. Current Xash protocol is not substituted.

## Local evidence and limits

Exact run: `ededd068a0444ff497d98204eacec8a4`; functional corpus SHA-256:
`614a07db0d29c13cc7a121c636deeda9e75e12bb75956dcb3ac609896c78137c`.
H inventory SHA-256:
`5441BFDF980F7285BEF00547B75360858158A7F45E02479C38A760782E04867F`.
Pre-I snapshot inventory SHA-256:
`81199FC339102E4637E7AD61AF2D6EB6A98FE949119A846A8DF425C9F117B9A5`.
The existing snapshot mechanism preserved the dirty A-H/launcher sources before
I edits. No commit/push or other worktree/game changes were made.

Actual retained context: `maps/boot_camp.bsp`, 540 resources, seven schemas,
212 baseline entities, 1,184 delivered datagrams, 1,169 payloads, five reassembled
and four decompressed payloads. Local imports: BSP/WAD3/lightmaps, 69 Studio
assets and 16 Sprite assets; 86 available positive model/BSP sizes match.
Manifest remains `world_ready_but_incomplete`, not globally complete.

Final coverage: 29 decoded, 21 resolved model instances, 16 Studio draw
instances, eight unsupported inline brushes and five unsupported material
instances; zero entity-culler rejections (this slice submits without entity
PVS/frustum culling). Draw-instance counts are submissions, not proof that all
16 appear as visible pixels. Nine model slots are used: 52 shotgun (1), 55
shotbox (1), 65 9mmAR (2), 68 9mmARclip (4), 69 ARgrenade (1), 80 RPG (1),
85 RPG ammo (3), 101 satchel (2), 107 squeak nest (1). No Sprite instance is
claimed rendered merely because Sprite resources were imported.

The inspected real PNG `opengl-final.png` shows textured Boot Camp geometry,
the original shotgun and shotbox. That run observed 20,152 world draws, 8,436
Studio draws, one world upload, 69 Studio uploads, 172 non-clear sampled
framebuffers and zero GL errors, then exited normally. Counters depend on
presentation speed; only their measured run is claimed. The first camera
attempt also drew originals but was outside a wall; it is retained as an
intermediate diagnostic, not the final visual evidence.

Before and after the original I integration, the historical result was
input/applied `323/323`, failed/visual_failed/pending `0/0/0`, final entities
29, health 200, slot2 clip 34, canonical hash `14031366596970435596`. M4.7.2F
later corrected public signed delta scalars from two's-complement to GoldSrc
sign-plus-magnitude. The current null replay remains 323/323 with zero
failures and has health 100 and canonical hash `7014210005320501317`; the
field set itself is unchanged. The historical I visual extension hash is
`13043716238117813159`. `entity2_x=unavailable` remains honest.

Debug and Release affected builds and the full Release build succeeded.
Focused I tests passed in both normal configurations: six cases / 155
assertions. Relevant ASan Debug regression coverage spans 109 cases / 82,552
assertions across I, A-H replay/control/entities/clientdata/sign-on, binding,
pose and textures. Its installed matching MSVC runtime is supplied only in
the test process PATH; the first attempt without it failed before main with
missing-DLL status, not a test failure. ASan Release was not run.

The complete permitted offline Release suite was executed once: 2,093 passed,
19 explicitly skipped for unavailable link/SDL capabilities, zero failed;
165 socket, process/WFP and active capture/Steam/lifecycle script tests were
excluded by the task's restrictions. Skips are not reported as passed. The
eight application F/G/H regressions and all five A-E literal checkers passed.
The audit selection and actual logs are under ignored
`manual-artifacts/research-runtime-capture/i-prechange-20260920`.
Public automated regressions use independently authored miniature BSP/WAD/MDL
inputs, never proprietary game assets or the private capture. Old active
capture failures and ASan Release pre-main limitations remain historical;
they do not invalidate the successful H replay, and no new active campaign
was run for I. Universal stock compatibility is not claimed.

## Proposed commit boundary (not committed)

I consists of retained captured resource/presentation context; exact optional
visual semantic projection and its separate hash; public sparse-model binding;
the local-asset scene provider and shared composition helpers; opt-in CLI and
real framebuffer export; six public-input regressions and retained-resource
assertions; build wiring and linked documentation. Review this delta against
the pre-I source snapshot, not just HEAD: A-H and launcher/diagnostic work were
already dirty. No capture bytes, game assets, build products or private output
belong in a commit. Suggested subject: `Render captured GoldSrc scenes with
explicit local assets`. No commit or push was performed.

## Live visual-provider reuse in M4.7.2F

The F live route reuses I's rooted readers, importers, neutral resource
packages and `build_render_scene()` boundary, but does not open a replay
capture. Its model namespace, world map and precache entries come from the
current live session. Static CPU imports are prepared once on the bounded
asset task and retained; OpenGL creation and reuse stay on the render thread.
Dynamic entity projection advances from committed live runtime revisions, so
a clientdata-only camera update does not reimport or upload unchanged meshes.

I's offline spectator-camera policy is now explicit. Live visual control uses
the external receiving-client camera policy and therefore cannot be
overwritten by the replay provider. Eye translation is the current validated
server origin plus its validated view offset; yaw and pitch are local input
state, with a typed server angle correction applied once when observed. The
current clientdata schema supplies a vertical-only view offset, which is
reported as such rather than fabricating horizontal server fields. Missing,
stale, dead or otherwise unsupported receiving-client state produces a typed
unavailable context, not guessed coordinates or a local-player entity guess.

Existing limitations remain visible classifications: inline brush instances
and some material variants are unsupported, and not every decoded entity must
be submitted or visible. F adds no prediction, interpolation-based camera
translation, HUD, weapon viewmodel, firing, or full GoldSrc view calculation.
