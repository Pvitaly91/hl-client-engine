# External GoldSrc maps for manual QA

Initial status: `third_party_manual_test_map_workflow_prepared`, with assets
not supplied. On 2026-09-30 the user explicitly requested download and
installation. Both selected Half-Life BSPs are now installed in the isolated
research copy. Their initial production preflight failures were diagnosed and
fixed in the follow-up below; both now pass read-only production validation
through spatial-scene construction.
This is an opt-in manual workflow, not a CI dependency or a claim of gameplay
compatibility. The E7/E7.1 Release remains `manual=not_run` for these maps.

## Ownership and architecture

The normal `hlclient.exe` uses the existing approved asset composition. The
compatibility follow-up changes only bounded BSP/lightmap and texture handling,
not sign-on, gameplay, impact profiles or launch parameters. The managed H2 launcher starts its
owned HLDS on the exact selected map; the client connects through normal
sign-on and accepts ServerInfo/resource-list map identity. It does not load
an arbitrary local BSP instead of the server map. Both HLDS and the client
use the same isolated `-ResearchHalfLifeRoot` (default
`D:\DEV\HLCLIENT-RESEARCH\Half-Life`). The client's approved local resource
roots for `--game valve` reduce to that copy's `valve` directory; there is no
independent overlay, package directory, current-directory search or fallback
to the original Steam install. The launcher already manages HLDS; do **not**
start another server. Steam files remain untouched.

Download/extract legally obtained packages yourself, outside the repository.
Inspect the archive's README/license and select the Half-Life/`valve` variant,
not a CS/Opposing Force variant. The launcher accepts **an explicit installed
BSP path**, not an archive: install the selected BSP manually at
`<ResearchHalfLifeRoot>\valve\maps\<actual-name>.bsp` and any required WADs
at `<ResearchHalfLifeRoot>\valve\`, plus referenced files under their exact
`valve` virtual paths. Do not overwrite unrelated files. If names conflict,
use a separately prepared isolated research copy and inspect the collision
first. No script downloads, extracts, copies or repacks third-party assets.
Never put them in Git, tests, fixtures or the Steam library. The default
`crossfire` command and `-Map crossfire|boot_camp|stalkyard` are unchanged.

The optional path is deliberately `Fast` manual keyboard/mouse only:
`-ExternalMapBsp` cannot be combined with `-ValidationMode Strict`,
`-TestStartHealth 50`, `-Scenario damage-respawn-check` or a second `-Map`.
The existing Strict prepared-copy projection rejects research-only files;
Fast validates the prepared marker/critical files and scoped restoration but
does **not** attest the entire modified content tree. The managed runner
retains isolation, process ownership, port checks and restoration. It also
requires the BSP be an ordinary file in the exact research `valve\maps`
directory, not a symlink/hardlink/alternate stream. No new trust is granted
to the client's resource resolver.

## Read-only preflight, then a user-run session

The actual BSP filename must come from your extracted package, never from a
webpage title. Set the root/path explicitly (example placeholders only):

```powershell
$research = 'D:\DEV\HLCLIENT-RESEARCH\Half-Life'
$bsp = Join-Path $research 'valve\maps\<actual-name>.bsp'
pwsh -NoProfile -ExecutionPolicy Bypass -File 'D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1' -Prediction reference -ResearchHalfLifeRoot $research -ExternalMapBsp $bsp -CheckOnly
```

`-CheckOnly` starts no HLDS, client, Steam API or WFP. It checks the exact
client/server BSP path, safe basename/ancestors, required executables and
launcher contract; reads the bounded BSP worldspawn WAD declarations and any
same-name `.res` for a path/presence inventory; runs the existing read-only
`hlclient_bsp_compat_check` through the **production approved resolver** at
texture-import stage; and prints the current Release SHA-256. A declared WAD
may be unused when all textures are embedded; successful production texture
import is the decisive check for required WAD data. RES presence is an
inventory, not proof that every runtime resource is usable. The checker can
reject malformed/unsupported BSP or unresolved textures; no fallback map is
selected. Compare reported `maps/<actual-name>.bsp` to the later ServerInfo
identity in the managed run report. A server-only map install cannot pass this
same-root preflight. Package archive inspection itself is not implemented:
provide an extracted directory if the archive cannot be inspected safely.

Only after the preflight passes, the **user** may run the same command without
`-CheckOnly` in an administrative PowerShell 7 window. That is one managed
session with the existing 45-second gameplay limit and bounded cleanup. Wait
for restoration before closing the console. To isolate Glock contact audio,
optionally add `-MuteGlockFireSound`; it does not mute impact/shell/crowbar.
No new feature flag is needed inside `hlclient.exe`.

## Metadata-only profiles

| Profile | Source page | Purpose | Actual BSP |
| --- | --- | --- | --- |
| materials-showcase | [Materials Listing and Showcase](https://gamebanana.com/mods/38227) | Material contact, decals, Glock/crowbar/audio; future footsteps | `_materials_valve.bsp` from the package's `BSP files/valve/maps/` entry |
| generic-goldsrc-test | [Generic Testing Map for GoldSrc](https://www.moddb.com/games/half-life/addons/generic-testing-map-for-goldsrc) | Movement, slopes/steps, camera, weapons, lighting and general smoke | `gentest.bsp` |

The source pages do not establish BSP basenames, dependency lists or playable
multiplayer spawnpoints. A selected map may be unsuitable for HLDS despite a
valid BSP. Inspect the package's game variant, `maps/*.bsp`, `*.res`, WADs,
models, sprites, sounds and readme/license without copying those assets into
this repository. Record actual zones only after viewing map/readme evidence.

### Authorized local installation and read-only findings (2026-09-30)

Archives were obtained from the source pages' official download links after
the explicit user request, not by the launcher. Local package storage is
`D:\DEV\HLCLIENT-RESEARCH\external-manual-maps-20260930`. No executable or
script from either archive was run. Only the two selected BSPs were copied
into `D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps`; neither target existed,
and copy operations did not overwrite any file. Archive copies and selected
documentation/map-source inspection files remain outside the repository.
No WAD/game asset was copied into Git or Steam, and no live session was run.

| Package | Size (bytes) | Source-published MD5, verified locally |
| --- | ---: | --- |
| `786_materials.zip` | 869181 | `aff6fed78d73f7e7e6d4c94711af2805` |
| `Generic_Test_Map_for_GoldSrcMissingno50_ModDB.7z` | 134892 | `b3137cdf3f0373376c5391362ba4d1f6` |

Installed BSP SHA-256 (identical to the selected archive entry):

- `_materials_valve.bsp`: `6C71AB96DCBF890901844EFDA48A84D0AA668308C686ED1EF36F205C4C282FF0`.
- `gentest.bsp`: `9338A39EAA14332625A8F4935DED3345A1CD9EC3ABB141750590091A4D21D7A2`.

The showcase package has multiple game variants; only `valve` was selected.
Its HTML catalogue lists 375 Half-Life entries, including 11 names marked
unused/not present in the game's WADs. Worldspawn declares `halflife.wad` and
`xeno.wad`, with sky `BLACK`. Both declared WADs and the six named sky faces
already exist in the isolated copy. The production approved-resolver texture
checker initially resolved both WADs, decoded 396 textures and reported **11
unresolved bindings**, so launcher `-CheckOnly` failed at texture import. A bounded
read-only name-table comparison confirms these absent names:
`DRKMTLT_WALL`, `ELEV1_DWN`, `FIFT_BLOODFLR`, `FIFT_BLOODFLRA`,
`FIFTIES_CMP3`, `GENERIC0150`, `GLASSGREEEN`, `LAB1_BLUX1`, `LAB1_BLUX1B`,
`LAB1_COMP2A`, `LAB1_FLOOR10`. This is not a missing whole WAD or failed
download; the package does not supply replacement texture data. No fallback
textures were invented and no assets were modified to bypass validation.

`gentest.bsp` declares `halflife.wad` and sky `2desert`; those resources already
exist in the isolated copy. The package includes no separate WAD/RES. The
initial production BSP parser rejected face 660 as `invalid_light_offset`:
its light offset is 122493, exactly the lighting-lump byte length, and all
four style slots are 255 (no active style). The earlier parser required
every nonnegative face light offset to be strictly inside the lump, without
distinguishing zero-style faces. This establishes the rejection boundary,
not proof of a corrupt archive or gameplay compatibility. No parser change
or BSP patch was made as part of download/installation.

Both maps have `info_player_start` (showcase: one; generic: two) and no
`info_player_deathmatch`. The pinned SDK contains an `info_player_start`
fallback, but actual stock-HLDS multiplayer spawn behavior remains untested.
The showcase catalogue and generic source-page description establish the
intended QA categories; actual gameplay zones remain manually unverified.

Exact read-only commands, with no filename placeholders:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\_materials_valve.bsp" -CheckOnly
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest.bsp" -CheckOnly
```

Historical download/install result: `external_test_map_assets=downloaded_installed`;
`external_test_maps_preflight=blocked`; `manual=not_run`. Download/install
does not silently authorize changes to importer validation or E7 semantics.

### Authorized compatibility follow-up (2026-09-30)

The user subsequently requested the production fixes. No BSP, WAD, server,
Steam or launcher file was altered to bypass the failures. Installed BSP
hashes above remain unchanged. The implementation is project source, not an
asset patch or a checker-only exception:

- `GoldSrcBspParser` accepts a nonnegative cursor at the lighting-lump end only
  when the first style is 255 (no samples). Negative offsets below -1 and
  beyond-end cursors remain invalid. Active-style end cursors remain invalid.
  `GoldSrcWorldLightmapImporter` treats bounded zero-style faces as unlit,
  retains their raw cursor and allocates no lightmap samples/atlas for them.
- `WorldTextureImportOperation` keeps strict rejection as its API default.
  Normal application and compatibility-checker composition explicitly select
  `MissingWorldTexturePolicy::placeholder_for_absent_name`. Only an absent
  texture name after every declared, required WAD was successfully resolved
  may receive the small project-owned black/magenta checkerboard. Missing
  WAD declarations/archives, malformed sources, missing BSP references,
  dimension mismatch, unsafe paths and configured resource limits remain
  failures. No arbitrary fallback search or new asset permission is added.
- One owning 16x16 RGBA image with four mips (1360 bytes) is shared across
  missing bindings per import and charged to existing texture/byte bounds.
  Source BSP material dimensions still determine UV scale. Neutral renderer,
  world package, brush library and stage validation accept the typed
  `substituted_missing_texture` binding; its evidence is
  `project_generated_placeholder`, never decoded WAD content.
- `complete_for_world_materials()` remains false for substituted source
  textures. `renderable_for_world_materials()` is separate; decoded and
  generated image counts are separate. The checker emits a bounded typed
  warning; normal live composition emits one aggregate warning on asset
  preparation, not per-frame log spam.

The showcase now has **396 decoded images + one generated image, 11 substituted
bindings**; its missing catalogue entries are visibly diagnostic checkerboards,
not restored original artwork or stock-exact texture behavior. All available
texture names retain their real images. `gentest` uses nine real textures and
does not require a placeholder.

Read-only production controls passed through geometry, textures, lightmaps,
render package and spatial scene: showcase 696 world faces / 1392 triangles /
75 PVS rows; generic 643 world faces + 28 brush faces / 1268 triangles /
63 PVS rows / two supported brush instances. Mandatory regressions use only
project-owned fixtures, including normal application asset composition, UV
scaling, strict failure/bounds/provenance cases and actual hidden OpenGL pixel
checks of the uploaded checkerboard and subsequent render pass. External
assets are optional read-only controls, not build/test dependencies.

Current result: `external_test_maps_compatibility=offline_verified`;
`external_test_maps_preflight=passed`; **`manual=not_run`**. Multiplayer spawn,
actual device/gameplay behavior and the material/audio checklists still require
the user's session. New live/HLDS/stock/Steam/WFP launches: zero. No staging,
commit or push. The default crossfire path and existing manual flags are
unchanged.

Ready user-run commands (one command = one managed session):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\_materials_valve.bsp"
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest.bsp"
```

Verification record for the new Release:

- Existing VS2022/Win32 Debug and Release builds succeeded. Final focused
  regressions passed in both: 139 cases, 83662 assertions, including actual
  production OpenGL pixel checks. The six new compatibility cases separately
  passed 136 assertions. No required test depends on installed game content.
- Existing `build-core-g1` was incrementally built with
  `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`; core/API tests and the transitive
  architecture guard passed (2/2 CTest registrations). The dependency manifest
  and core test source/link lists contain no concrete Half-Life module.
- Both exact external-map Release `-CheckOnly` commands passed, as did the
  default source-only check and no-stock launcher fixture. Reference/off,
  existing map/Scenario contracts, rejection of incompatible external-map
  options and exact same-run reporting remain covered.
- The initial complete Release run (2555 registrations, parallel 4) exposed
  the old signed-offset fixture's implicit zero-style assumption. The fixture
  now explicitly supplies an active style; its strict end-cursor rejection
  assertion is unchanged. The new independent zero-style regression covers
  the compatible case. A fake-HLDS global process-identity assertion failed
  only in the parallel run and passed sequentially (330 assertions); it does
  not observe a production game process. Final full-suite status is recorded
  below, not inferred from the focused green runs.
- Final complete Release CTest run, after the last source/test edit and with
  `--parallel 1`, finished **2520 passed, 33 skipped, 2 failed / 2555** (exit 8).
  The only failures are the two legacy fixture contracts below; signed-offset
  and fake-HLDS cases passed. Skips are explicit installed-asset/device,
  symlink/reparse/corpus capability and disabled WFP opt-ins, not claimed
  successful proofs. This is not an all-green global suite. Logs are retained
  under the ignored task record as `release-offline-serial-final.txt`.
- Two unchanged legacy fixture contracts also fail: the smoke-policy source
  scan requires the obsolete literal `(project_mode ? "true" : "false")`;
  topology's synthetic repository fails to copy the required
  `stock_manual_fast.ps1` helper. A debugger inspection confirmed the latter
  exact missing-helper exception before the version gate. Their source files
  and the native orchestrator are SHA-identical to the pre-edit snapshot;
  neither failure is suppressed or repaired by changing gameplay/source
  behavior in this compatibility fix.
- CLI version/help, Null renderer, synthetic move-transport self-test and
  orchestrator functional-log observation passed. New ASan runtime validation
  was not run; its earlier silent exit-1 limitation is not claimed fixed.
  No real-device audio or gameplay listening test was performed.
- Current `build\bin\Release\hlclient.exe` SHA-256:
  `1E5CFC660363AAABD7285C32C1BB5E93E65D2477904BEC50172B8EF9C7AB4678`.
  Both external-map launcher checks selected that exact executable/hash.
  Pre-edit source preservation: 1147 files in ignored
  `manual-artifacts/task-records/external-map-compat-20260930/source`; required
  new fixture code is a regular project file under `tests`, not hidden there.
  Hash comparison found changes only to the 25 intended existing files; the
  additional `tests/missing_texture_test_fixture.hpp` is new project-owned
  fixture code. Previous unrelated source changes and staged state were
  preserved. Final `git diff --check` passed. Branch/HEAD remain
  `codex/stock-runtime-campaign-5e48b7c1` /
  `ea738a80e5a66a500c245f95b96eed32198e2d66`.

### Materials-showcase checklist (only surfaces actually present)

For each available concrete, metal, wood, tile, grate, glass/computer, dirt,
vent and slosh surface, fire one Glock shot and compare fire voice, material
contact voice, decal, visible shell, shell contact voice, muzzle flash and
light. Repeat with `-MuteGlockFireSound` when impact audio is masked. Try
crowbar miss in air, then concrete/metal/wood and any other available surface:
verify hit/miss pose, strike plus material voice, delayed (~0.2 s) anchored
decal and no duplicate effect after camera motion. Do not infer a material
classification from texture appearance alone or treat an entity hit as the
static-world contract. The existing bounded opt-in impact diagnostic reports
surface ID, sanitized texture key, material, classification source, selected
sample and immediate resource state; use it if enabled in the current client.
`pending`/`voice_started` is not user hearing confirmation.

### Generic-map checklist

Walk/run policy (Shift), jump, duck/crouch-walk, available slopes/steps/wall
contacts/narrow passages; pitch/yaw viewmodel attachment and prediction
stability; Glock fire/reload/empty, crowbar miss/wall hit; decals, flash,
light, shell, world model and nearby lighting; fire/impact/shell/crowbar
audio; HUD, weapon switching, framerate and map/render integrity. Mark
unavailable zones `N/A`, not passed. This checklist can be reused for future
footsteps, particles, ricochets, weapons, movement, lighting and rendering,
but none of those features is implemented by this workflow.

## Return format (fill in only after your own session)

E8 optional matrix: run on available concrete/metal/dirt/vent/grate/tile/slosh
floors and across their seams; record actual material/category and normal run
audibility, stop/start, turns, jump/landing, slopes/steps and regressions.
Wood/glass/computer/snow use the reference concrete footstep fallback, not new
families. Shift/duck are normally quiet under the retained multiplayer speed
gate; mark unavailable zones N/A. Default E8 testing remains **crossfire**;
neither external map becomes a mandatory build/test asset. The new build is
`manual=not_run` until the user's session. See the
[E8 record](HALFLIFE_MATERIAL_FOOTSTEPS_E8.md) for exact policy and evidence.

```text
E7-material-map:
- map loaded: yes/no
- concrete Glock: yes/no/N/A
- metal Glock differs: yes/no/N/A
- wood Glock differs: yes/no/N/A
- Glock decal: yes/no
- shell audio: yes/no
- concrete crowbar: yes/no/N/A
- metal crowbar differs: yes/no/N/A
- wood crowbar differs: yes/no/N/A
- crowbar decal: yes/no
- regressions observed: <text/none>

generic-map:
- map loaded: yes/no
- walk: yes/no
- jump: yes/no
- duck: yes/no
- slopes: yes/no/N/A
- steps: yes/no/N/A
- Glock: yes/no
- crowbar: yes/no
- lighting: yes/no
- HUD: yes/no
- issues: <text/none>
```

## Failure boundaries and cleanup

`external_map_not_supplied` / `archive_not_extracted`: pass an explicit
installed `.bsp`, not a URL/archive. `bsp_not_found`: inspect extracted
package and install the **actual** filename. `unsupported_game_variant`:
do not use another mod's BSP in `valve` by assumption. `unsafe_path`:
no relative traversal, symlinks, network/Steam/repository roots or arbitrary
search. `map_not_visible_to_client_root` / `map_not_visible_to_server_root`:
both processes require the exact selected file in the same isolated root.
`missing_wad` / `missing_resource`: inspect package dependencies and the
preflight inventory; never fallback to Steam. `malformed_bsp` /
`material_table_unavailable`: distinguish BSP/texture import from the optional
Half-Life material table. `server_not_running_requested_map` /
`client_map_identity_mismatch`: compare requested map, HLDS command and
observed ServerInfo; stop rather than silently trying another map.

The launcher does not install or remove map assets. After testing, manually
remove **only** files you added, after confirming no managed session owns the
research root and keeping your own inventory/backups. Do not use broad clean
commands. Existing restoration owns only its scoped mutable files. CI and
mandatory offline tests use project-owned fixtures and never require these
external maps, installed Half-Life, Steam or Internet access. Absence of a map
is a capability-missing skip, never a pass.
