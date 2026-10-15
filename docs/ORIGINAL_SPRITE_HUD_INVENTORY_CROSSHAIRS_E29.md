# E29 — original sprite HUD, inventory selection and crosshairs

## Q last-used weapon follow-up — 2026-10-16

Normal keyboard gameplay now binds the physical Q key to immediate previous-weapon
selection. It toggles between the two most recently confirmed active weapons;
before a second weapon is confirmed it does nothing. Q is independent of visible
HUD, suit icon, bucket candidate and fastswitch setting. The existing focused,
captured input snapshot supplies one press edge: key repeat, a held key after
focus/capture loss and a click used to capture the mouse do not select or fire.

The pinned SDK `b1b5cf5892918535619b2937bb927e46cb097ba1` dispatches `lastinv`
in `dlls/client.cpp` to `CBasePlayer::SelectLastItem` in `dlls/player.cpp`.
That operation requires a last item and permits the current item to holster,
then swaps active/last and deploys; `SelectItem` does not advance last-item
history for an absent or already-active item. `RemovePlayerItem` clears a removed
last item. The mirrored client weapon callback has the same swap/holster intent.
No SDK implementation body was copied.

This client's bounded two-ID history follows committed CurWeapon transitions,
including records received without a HUD frame, plus the existing ordinary
observation path. Fresh source ordinal/bit bounds reject duplicate or older
observations; clip-only publications do not replace the last weapon. Candidate
movement, request creation and queue receipts never advance history. Q passes
the previous ID into the existing typed inventory-selection channel, retaining
current ownership, safe catalogue token, supported-ammo/exhaustible and
CanHolster/RPG guidance checks. The server remains authoritative for the actual
switch. A pending request blocks another Q until fresh confirmation, rejection
or the existing two-second confirmation timeout. Death/life, HUD reset,
generation/disconnect and lost ownership invalidate remembered targets.

Q itself creates no primary/secondary edge, release, fire action or effect.
If it closes an already-open bucket while LMB is held, the original menu's
physical-click consumption remains in force until release; an intentionally
held attack outside that menu remains normal gameplay input.

This is a local-compatible reconstruction of observed last-item history through
the existing validated named-selection path, not a new arbitrary console-text
executor or access to the server's private `m_pLastItem`. Unobserved server-only
history and unsupported mod inventory rules are not claimed stock-exact.
Tests tagged `[last-weapon]` cover input repeat/focus/capture, pending handoff and
server confirmation, rejection/timeout, hidden HUD, stale/bit-ordered commits,
life/reset/ownership boundaries, primed grenade/RPG gating and no false fire.
No live session was launched for implementation; new manual acceptance is
`not_run` until the user switches weapons and presses Q in normal gameplay.

The six `[last-weapon]` cases passed in both Debug and Release. The coordinated
Debug input/game/HUD/E22 regression passed 292 cases (558,289 assertions), with
five explicit original-resource opt-in skips. Release Q/E22/E29 passed 87 cases
(499,478 assertions), with four such skips. Final full-suite evidence and the
executable identity belong to the current Architecture follow-up and
`manual-artifacts/task-records/q-last-weapon-egon-light-20261016/`.

Manual check: select Glock, then another owned weapon; separate Q presses
should alternate between them without opening the bucket or creating a shot.
Holding Q should select only once. Recheck after death/respawn: no stale
previous-life weapon should be requested.

## Ammunition history overlap follow-up — 2026-10-14

`attribution=user_report`: the bottom-right screenshot contains overlapping ammo
numbers. The extra 68 is pickup history, not a duplicate reserve authority.
Its fixed source-space offset ignored the actual 40x48 digit and 48x48 icon
geometry: at 1080p, history y=1005..1041 intersected the readout at y=1028.7.
History now stacks by complete row extents above both primary and secondary ammo.
Counts, source sprites, history lifetime and fade are unchanged.
`[ammo-history-layout]` covers four sizes, three scales, Glock/MP5 and production
GPU A/B preservation of the entire permanent ammo area. New `manual=not_run`;
evidence is under `crossfire-presentation-repairs-20261014` in task records.

Task: `HLC-E29-ORIGINAL-SPRITE-HUD-INVENTORY-CROSSHAIRS`.
Result: `original_sprite_hud_weapon_selection_crosshairs_integrated`.
Primary checkpoints A–C are integrated in normal `hlclient.exe` and verified
offline. The Release below is ready for the user's manual acceptance, which
has not been run. This is not completion of every original HUD subsystem.

## Preserved baseline and authority

Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`.

E28.1/E28.2 acceptance is retained only for graceful peer disconnect, normal
close, durable receipts, final verdict and cleanup/restoration reporting.
Attribution: `user_report`, as supplied in the E29 request. Run
`502e31e7acde42938f4cb155ef2dd6b0` confirms accepted establishment, committed
map/world, ready A/B receipts, both window-close exits0, correct normal verdict,
exact cleanup and scoped restoration/count0. A observed explicit server
disconnect confirmation; B observed transport ACK without explicit confirmation.
The38.5118ms B-close -> A absence interval is a committed observation interval,
not packet delivery. Whole-tree verification remains `not_performed`.

Historical client SHA:
`F8A0050D1DC027B10AE3157EA8BE20CDF284A650D3573F2469C9CD89C846A75C`.
It is not the identity of the new E29 binary. The31 baseline artifacts remain
unchanged. Pre-edit source1309 and Release rollback34 files were copied and
SHA-verified using the existing task-record layout under
`manual-artifacts/task-records/e29-original-sprite-hud-20261010`.

There is no new gameplay inventory, ammo accounting, hit/damage authority,
weapon implementation, console command executor, DLL/plugin loader or server
modification. Brush `unsupported_render_mode` remains an unrelated known gap.

## Exact missing crosshair boundary and pre-fix proof

Previously `HalfLifePresentation::hud` emitted the HP/ARM/weapon/clip debug panel.
`HudDrawCommands` carried only rectangles/text; `main.cpp` copied these to
`RenderScene.basic_hud`, and OpenGL `render_hud` rasterized untextured geometry.
There was no descriptor -> approved SPR -> cropped screen-space command ->
materializer -> textured HUD draw path. This was not established from the user
report alone and was not a Windows volume, hidden-HUD or guessed-alpha diagnosis.

The deliberate pre-fix probe compiled against preserved pre-E29 headers and
still-unrebuilt Release libraries (link input SHA manifest retained). A healthy,
confirmed, suit-owning Glock with HUD unhidden/FOV90 was rendered by the actual
old module/host/OpenGL path at640x480. Legacy HUD changed the frame signature,
but0/961 center pixels changed; the crosshair expectation failed with exit1.
This used a hidden offline GL window, not a game/live session. The preserved
probe is not rerun against new incompatible library inputs.

## New production data flow and dependency direction

```text
HalfLifeClientModule compiled presentation dependencies
  -> GameClientAPI immutable descriptor requests
  -> ApprovedHudAssets: exact selected-world root / verified locator / opener
  -> inert bounded manifest parser
  -> existing AssetManager + GoldSrcSpriteImporter + SpriteRenderAssetBuilder
  -> owning HudResourceSet (roles/tiers/crops/status/ID/revision)
  -> HalfLifeSpriteHud (confirmed state, visibility, FOV, menu, pickup policy)
  -> neutral HudDrawCommands.sprites
  -> app::materialize_hud_sprites (approved immutable bindings only)
  -> OpenGL screen-space HUD pass / actual pixels
```

HL names, aliases, roles, visibility, weapon eligibility, menu rules and samples
remain in `games/halflife`. The generic parser treats roles as inert tokens;
approved opening, image import, materialization and rendering know no weapon
names. Module output contains owning IDs/revisions/frame/crop/destination/tint/
blend/clip/order, not GLuints/native pointers/root handles. Lower targets do not
link the Half-Life module; alternate-module/core checks exercise the same seams.

| Old boundary | New owner/path |
| --- | --- |
| No HUD descriptor preparation | `hud_resource_profile.*` -> API requests -> `goldsrc/hud_assets.*` |
| No inert `.txt` parser | `goldsrc/hud_manifest.*`, no execution of file contents |
| Debug-only HUD meaning | `games/halflife/sprite_hud.*`, existing state retained |
| No SPR draw primitive | `game_api/presentation.hpp`, `renderer/render_scene.hpp` |
| No materialized HUD bindings | `app/hud_presentation.*`, normal `apps/hlclient/main.cpp` |
| Untextured GL HUD only | existing `opengl_renderer.cpp` HUD pass extended in place |
| Immediate normal digit/wheel switch | module menu -> validated selection handoff -> server confirmation |
| Escape released capture before UI | neutral SDL escape ownership during keyboard gameplay; module cancel before fallback |

## Actual resource contract

Pinned behavioral reference is base `cl_dll` at
`b1b5cf5892918535619b2937bb927e46cb097ba1`: ammo, ammo history, hud/redraw/msg,
health, battery, flashlight, status icons and death. No SDK implementation bodies
were copied. Actual approved installed descriptors, not v-model guesses, define
the visual roles.

Read-only audit found15 descriptors,483 records and224 unique SPR dependencies;
all referenced files are present and all crops fit actual frame dimensions.
Source bytes:9,575,715 including20,195 descriptor bytes. The data supplies
320/640/1280/2560 tiers. Legacy tiers use shared sheets; modern resources include
nested relative names such as `2560/weapon_9mmhandgun_weapon`.

`sprites/hud.txt` declares257 but actually contains247 records. Engine-side
SPR_GetList parser implementation is not present in the pinned SDK, so an
upper-bound-count grammar is not claimed. Generic corrupt counts are rejected.
Only this complete audited file SHA, declared257 and parsed247 activates the
explicit compiled compatibility exception:
`E7922C3D3ED2EC59614E032D1C5A8194AA4ABED7713193191EE1E68A2FF39EF5`.
Any modified file/count fails that exception; installed assets are not edited.

The parser bounds64KiB text,512 total entries,63-byte tokens, coordinate/numeric
ranges and256 dependencies. It rejects traversal/absolute paths, duplicate
role/tier pairs, invalid numbers/counts and out-of-frame crops. The same role in
different tiers is valid. Unknown inert roles remain metadata without execution.
Loader uses only the approved world's exact root, never arbitrary search or
another installation/server precache slot. SPRs are capped8MiB each, aggregate
reads64MiB, decoded resource budget160MiB including preallocation/copy allowances,
with a20s approved reader/IO deadline and existing bounded source opens. This
is not a preemptive timeout for an already running CPU import; finite decode
operation/allocation limits bound that separate stage. No HUD-frame or
audio-callback filesystem reads occur.

Source materials:221 additive sprites and3 alpha-test crosshair sheets. Palette
black is neutral under additive blending; only source index255 is transparent
for masked crosshairs. Original orange crosshair texels use white modulation;
gray HUD glyphs use the HL orange HUD tint. No global white/black color-key is
applied. Integer crop, top-left UV origin, nearest/clamp/no-mip HUD sampling avoid
neighbor atlas bleed. World SPR/blood/explosion import/render paths are retained.

## Fourteen-weapon role coverage

Every entry has all four installed tiers and active/inactive weapon icons.
Actual source rectangles, full hashes/revisions/materials and absent roles are
exported separately in `docs/evidence/ORIGINAL_SPRITE_HUD_COVERAGE_E29.json`.

| ID / catalogue name | Crosshair | Autoaim | Zoom | Zoom autoaim | Ammo / ammo2 |
| --- | --- | --- | --- | --- | --- |
|1 crowbar | intentionally absent | absent | absent | absent | absent |
|2 9mmhandgun | yes | yes | absent | absent |9mm / absent |
|3 357 | yes | yes | yes | absent |357 / absent |
|4 9mmAR | yes | yes | absent | absent |9mm / AR grenade |
|6 crossbow | yes | yes | yes | yes |bolts / absent |
|7 shotgun | yes | yes | absent | absent |buckshot / absent |
|8 rpg | yes | absent | absent | absent |rockets / absent |
|9 gauss | yes | absent | absent | absent |uranium / absent |
|10 egon | yes | absent | absent | absent |uranium / absent |
|11 hornetgun | yes | absent | absent | absent |hornets / absent |
|12 handgrenade | intentionally absent | absent | absent | absent |grenades / absent |
|13 tripmine | intentionally absent | absent | absent | absent |mines / absent |
|14 satchel | intentionally absent | absent | absent | absent |charges / absent |
|15 snark | intentionally absent | absent | absent | absent |snarks / absent |

Confirmed catalogue aliases only: `weapon_glock` ->9mmhandgun, `weapon_python`
->357, `weapon_mp5` ->9mmAR. Crosshairs are selected from descriptors, not an
ID-to-procedural-cross table. `hud.txt`'s `cross` is the health icon, not the
weapon reticle. Missing zoom falls back to normal; missing zoom-autoaim falls
back to zoom. Autoaim requires confirmed CurWeapon on-target, never proximity.

## HUD, selection, input and lifecycle

The default gameplay panel uses original number/icon sprites for health, suit
armor, clip/reserve and MP5 secondary ammo. No-clip omits the magazine rather
than displaying-1; existing shared ammo/authority is untouched. Suit and flash
fills crop the visible portion without squeezing the whole icon. Source-backed
value fades/tints are presentation only. Debug panel is explicit
`--hud-diagnostics`, off by default; it is not an automatic missing-resource
fallback. `--hud-draw 0|1`, `--crosshair 0|1`, `--hud-fastswitch 0|1` are bounded
typed presentation settings, not arbitrary console commands.

Preferred tier follows the pinned thresholds: width>2560 AND height>1600 ->2560;
width>=1280 AND height>720 ->1280; width>=640 ->640; otherwise320. Missing tiers
fall back deterministically to the greatest ready tier not above preferred,
otherwise the smallest ready tier. Sprite tier and physical framebuffer are
separate; layout uses native tier pixel dimensions without aspect stretching
or double scaling. Resource sets publish atomically and keep image/crop revision
association across resize.

Server WeaponList slot/position and owned weapon bits drive buckets. Catalogue
or pickup notification alone does not grant ownership. Candidate, awaiting
handoff, actually queued request and fresh confirmed active weapon are separate.
Original menu is default:1–5 open/cycle buckets, wheel cycles candidate, LMB
confirms, Escape cancels. Fastswitch applies only to a single eligible candidate
in a newly opened bucket, not arbitrary instant wheel/multi-weapon switching.
CanHolster, RPG guidance, zero-ammo Hornet recharge, Satchel radio and exhaustible
retirement remain existing validated policies. Direct internal/script helpers
remain unchanged.

UI confirmation masks primary from unsent held/pressed input and clears only its
pending latch before command materialization. It never rewrites history/TX or
invented release events. A subsequent real held input is not permanently blocked.
Escape is routed to an open menu before SDL's release/close behavior; outside a
menu the existing behavior and fail-safe focus/event-limit release remain.
While keyboard gameplay is active, Escape is forwarded before native fallback;
cancel wins over group/cycle intent collected in the same input frame. This
prevents a rapid group-key/Escape pair from releasing or closing before UI.
An in-flight old-active CurWeapon clip update is not treated as switch rejection.
Primed Hand Grenade CanHolster denial must not mask its physically held primary
button or manufacture a throw-release through menu consumption.
If outside-menu Escape releases capture after input collection, the same unsent
command interval is immediately marked uncaptured and its scoped mouse actions
are suppressed; no extra attack/charge/use interval or local-look update leaks.
WASD, mouse look, secondary/release state, network updates and TAB are separate.

Pickup history reuses committed AmmoPickup/WeapPickup/ItemPickup observations,
with4 bounded fading rows, expiry/reset and correct named icons. It performs no
second ammo/inventory update. Missing icons are degraded statuses, not another
weapon's icon. Four nonspatial menu samples use the existing approved loader/
LocalAudio/mixer: `common/wpn_hudon.wav`, `wpn_moveselect.wav`, `wpn_select.wav`,
`wpn_hudoff.wav`. Input events, not render frames, produce cues.

Death/ResetHUD/InitHUD freshness floors prevent pre-life active/autoaim/FOV
presentation from surviving into a new life. Menu/history/pending/UI audio/fade
identities reset; immutable asset owners stay in application cache and rebind
through the host reset serial without rereading files on weapon switches.
Fresh reliable fragmented CurWeapon follows the same record/bit freshness
floors as unfragmented state; reassembly alone is not grounds for rejection.
E14 scoreboard/feed state and sorting are retained; overlays order above the
new HUD. hud_draw hides the feed while independent TAB remains available.
Weapon markers in killfeed retain documented safe text fallback.

Supplementary implemented capabilities: confirmed Flashlight/FlashBat crop,
bounded8 StatusIcon names/colors and8 original Damage tile roles from existing
damage observations. Unknown flashlight battery/on state stays absent.
Known limitations: directional pain indicator, full intermission/spectator HUD
and world flashlight are not implemented. Damage tile expiry/layout is an
explicit local-compatible2s profile, not SDK cycle/pixel-exact parity. Source
assets/layout do not prove stock-binary pixel parity without matching captures.
Directional pain is a deferred controller/binding, not a claim that Damage or
its resources are absent: committed Damage origin/amount and player origin
exist; the separately loaded `sprites/{320,640,1280,2560}_pain.spr` files are
present but are not `hud.txt` roles or currently prepared HUD bindings. A coherent
pain-compass orientation is not retained by this HUD implementation. Intermission,
in contrast, has no typed observation/context/decoder capability currently.
These supplementary gaps are not included in the validated A–C primary result.

## Final offline verification

Mandatory source-owned parser/renderer/input/module/PCM/alternate fixtures are
implemented. Actual approved asset preparation/OpenGL screenshots require the
separate read-only opt-in; synthetic PCM is not listening confirmation.

| Check after last production edit | Observed result |
| --- | --- |
| VS2022/Win32 normal Debug and Release targets | Built successfully |
| Focused E29/E28.1/E28.2, migrated legacy cases and E8 readiness, each configuration |96 total,93 passed,3 asset opt-in skips;24,838 assertions passed |
| Final read-only E29 approved assets + OpenGL + menu WAV decode |3 cases /1,115,061 assertions passed |
| Core-only Release, `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` |42/42 CTest;308 core cases /143,207 assertions; alternate module and architecture guard passed |
| ASan normal client + tests build |Passed; latest test-only opt-ins also compiled |
| ASan `--help` and focused runtime |Both blocked before entry:3221225781 /`0xC0000135`, no program output; not passed |
| Exact manual command with `-CheckOnly` |Passed; selected normal Release;`process-launches=0` |
| Full final Release offline driver after all production/behavioral test edits |3362 CTest entries:3303 passed,59 skipped,0 failed; driver exit0 and post-checks passed |
| Direct Catch entries in that suite |3240 discovered,3181 passed,59 skipped;1,799,790 executed assertions |

Actual asset validation checks all15 descriptors/224 SPR dependencies, plus four
menu WAVs through approved opening/decoding. Source hashes of239 descriptor/SPR
files match the published machine coverage. The compiled startup/remote audio
profile audit observes83 unique names against128 existing cache slots; dynamic
Studio-event dependencies are explicitly outside that capacity proof.

The mandatory source-owned menu audio fixture routes each of the four UI effects
through GameClientHost -> LocalAudio -> existing OfflineOutput mixer. It checks
nonzero finite isolated PCM, symmetric nonspatial output, exactly-once delivery,
missing/pending/late outcomes and reset ownership. Real menu WAV validation proves
decode/nonzero PCM/cache availability separately, not device playback or listening.
No new device-output or listening acceptance is claimed.

Mandatory renderer fixtures cover subrect/atlas neighbors, tint/material modes,
clip/order/frame/revision/budgets, Null and invalid-data-before-upload. The actual
original asset integration covers normal Glock/Shotgun, Python/Crossbow zoom,
health/suit fill/clip-reserve/MP5 ammo2/Gauss no-clip/menu/pickups at640x480,
1280x720,1920x1080,3440x1440 and3440x1800. CPU crop/tint/blend expectations,
neutral-background controls, retired crosshair and state changes are checked,
not only changed-pixel counts. These are hidden offline GL fixtures on a stable
flat backdrop, not native fullscreen/DPI transitions or live world acceptance.

Forty lossless PNG screenshots plus a contact sheet are saved under
`manual-artifacts/task-records/e29-original-sprite-hud-20261010/visuals` with
source/pixel/PNG hashes in `rendered-screenshots.json`. Only the forty newly
created PPM duplicates were removed after lossless pixel/hash verification;
historical files were not cleaned.

Preservation verification:1309 source copies,34 rollback Release copies and31
historical run files match saved SHA-256; no missing current source.116 matching
prediction/collision/history/replay/network/transport/framing/usercmd/GoldSrc SPR
files are byte-identical. The shared live-stage change is only neutral unsent
input masking/capture release, not wire/history/prediction. Old assertions are
not relaxed; all182 directed weapon transitions remain in their unchanged test.

The first complete Release attempt is retained: 3361 entries, 3299 passed,
59 skipped, 3 failed. E14 expected a rectangle from the removed default debug
panel; E28 expected that diagnostic text; E25 indexed `texts[0]` without checking
whether the now opt-in panel existed. These were legacy fixture assumptions,
not evidence of a normal-application Satchel crash. Their specific legacy text/
rectangle samples now explicitly opt in to diagnostics. Existing assertions
remain unchanged, and E25 additionally requires a nonempty vector before indexing.
Together with the two earlier diagnostic-panel fixtures this is five targeted
legacy opt-ins, never a change to the normal default. The 182-directed-transition
test body is untouched. The whole Release suite is rerun, not replaced by only
the three passing reruns; first failed logs are not discarded.

The second full attempt is also retained: the three HUD fixtures passed, but
the existing E8 first-PCM fixture failed once (zero voices/output);12 isolated
unmodified reruns passed. Code audit found an existing nonblocking race: its
first drain also prepares three Egon samples not supplied by the movement-only
fixture; `ApprovedSoundAssets::request_local` uses `try_to_lock` and may return
transient pending even for decoded cache entries while the worker publishes
those results. `LocalAudio` retains the cue, but the fixture previously asserted
after just one update. The failed mutex interleaving was not captured, so this
causal explanation is an inference from the unchanged loader/delivery code.
The fixture now pumps an empty batch with identical session/scope/movement epoch
at its unchanged logical instant, bounded by2s wall time, before the unchanged
voice/PCM assertions. A deterministic pending-once -> empty retry -> one voice
test verifies the contract. No production mixer, loader, attenuation or gameplay
code is changed. Its initial fixture omitted unity listener gain (default0.35)
and correctly failed the strict PCM threshold; explicit fixture gain1 fixes
that setup without relaxing the threshold. All preliminary logs remain saved.

Stock visual comparison is skipped: `matching_stock_hud_capture_unavailable_in_repository`.
Read-only repository audit found116 raster files:41 new E29 PNGs,71 effect-fixture
images and4 historical project-renderer offline replay PNGs; no matching original
stock HUD/crosshair image or video. Original imported assets and wire/corpus
evidence do not substitute for stock-rendered reference pixels.

The59 full-suite skips are explicit opt-in installed assets/device/corpus,
filesystem-link/capture capability and disabled WFP checks, not passes. The three
E29 installed-asset/GL/WAV cases are separately enabled and passed in the final
run; older unenabled opt-ins remain untested. Mandatory project-owned GL, PCM,
Null, parser, input, alternate-module and lifecycle fixtures run without stock.
Full driver post-checks pass version/help/Null, synthetic transport, functional
log observer, jump/duck/speed summaries and launcher no-stock/source-only controls,
including reference/off, maps, Scenario damage-respawn and E28.1/E28.2 receipts.

Final detailed logs, per-skip names, preservation inventory and source changes
are retained in `manual-artifacts/task-records/e29-original-sprite-hud-20261010`.
Machine acceptance: `docs/evidence/ORIGINAL_SPRITE_HUD_E29.json`; full role/crop/
hash mapping: `docs/evidence/ORIGINAL_SPRITE_HUD_COVERAGE_E29.json`.

## Release identity and handoff

Normal executable: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
4,103,168 bytes. SHA-256:
`4154434D9DD86735B5BF035AC04C3905847B901325DC10422E700495668C10DC`.
The launcher still resolves its own `build/bin/Release/hlclient.exe`; only menu
help changed in E29, not configuration/session/cleanup logic. Native runner source
and the E28.2 baseline run remain unchanged. Its relinked runner SHA is
`47936E2762CA8A3BDE9102FD4C3B4F1AB0D645EC2D78B260770B50C2DDECC868`.

No hidden flag, alternate executable, manual asset copy or server modification
is needed for the default original sprite HUD. Git HEAD/branch remain the
preserved baseline; staging/commit/push are none. `git diff --check` passes.
The extra no-index check also covers untracked sources. It found one trailing
blank EOF in a new parser test, removed after the full run; no C++ tokens,
assertions or production code changed in that final whitespace-only cleanup.

Manual E29 acceptance: `not_run`. New live/Steam/HLDS/capture sessions0.
Staging/commit/push: none. Stock assets/server/config/maps unchanged.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

## User checklist after final Release

- [ ] Original health/armor/ammo sprites, no default debug panel.
- [ ] Glock reticle in gameplay viewport.
- [ ] Correct new weapon reticle after confirmed switch.
- [ ] Python/Crossbow zoom/reset leaves no stale reticle.
- [ ] 1–5 open the correct weapon buckets.
- [ ] Wheel/repeated group key cycles candidates.
- [ ] LMB confirmation causes no extra shot/prime/throw/mine.
- [ ] Escape closes menu; outside menu release-mouse remains correct.
- [ ] Ammo/secondary icons and counts are correct.
- [ ] No-clip has no-1; shared ammo is preserved.
- [ ] Armor fill and health changes follow server state.
- [ ] Correct pickup icons fade and expire.
- [ ] TAB and killfeed remain usable together with the new HUD.
- [ ] Death/respawn clears menu/crosshair state.
- [ ] Resize/fullscreen does not crop HUD or shift the reticle.
- [ ] A/B have independent HUDs; B actions do not change A HUD.
- [ ] B close, A close and final E28.2 summary remain correct.

Do not expect a reticle for crowbar, handgrenade, tripmine, satchel or snark:
their actual original descriptors intentionally have no crosshair role.

## E29.1 — post-manual kill feed and confirmation-gesture correction

The user's subsequent screenshot/report is retained as `attribution=user_report`:
the sprite health/armor/ammo HUD is visible, but the feed still shows tiny
`HLC_E9_B [EGON] HLC_E9_A` diagnostic text; clicking a weapon-menu candidate
produces a shot visual and gunshot sound. No executable SHA was supplied for
that manual session, so its identity is not inferred from the current disk file.
This is a defect report, not acceptance of the entire E29 checklist. The original
E29 build/test figures and its `manual=not_run` record above remain historical.

Cause of the feed: `compose_multiplayer_hud` still generated a single text
command at pixel_size1.2 (about8.4px glyph height) instead of using already
prepared `d_*` entries in `sprites/hud.txt`. Normal ready-resource composition
now receives the HL-owned immutable base descriptor/tier from HalfLifeSpriteHud,
selects the exact committed DeathMsg role (`d_egon`, case-sensitive `d_9mmAR`,
etc.), or `d_skull` for an unknown role, and submits a neutral HudSprite. Separate
killer/victim names use the existing generic glyph renderer at2/4/6 pixel scale,
14/28/42px actual glyph height. Original top34/text offset4/rightmargin4,
orange/teammate-green icon and snapshotted team colors follow the pinned base
`cl_dll/death.cpp`. Monospace glyphs, bounded '-' elision and conservative
20/36/52px line-box spacing are explicitly local-compatible, not stock-font
pixel parity. Unequal row widths cannot place an icon over preceding name ink.

No new sprite importer/font/device/resource preparation path is added. All
death icons are existing approved HUD entries. Missing/not-ready whole HUD
descriptors retain the old safe text fallback. Notice authority, six-second
expiry, four-row bound, scoreboard and source/name snapshots are unchanged.
Trailing neutral display-only team snapshots prevent player-slot reuse from
recoloring previously committed notices. No score/damage/corpse rule is changed.

Cause of the confirmation shot: consumption ended after the confirmation frame;
the still physically held primary button entered the next normal usercmd and
then the same accepted-command visual/audio path. The earlier test/comment
claiming that the pinned SDK allows this next held frame was incorrect. The
audited base chain is ammo.cpp clearing IN_ATTACK, hud_update.cpp calling
CL_ResetButtonBits, and input.cpp clearing attack state while KeyDown retains
the physical down owner until KeyUp. The correction is an HL-owned suppression
latch for the complete UI-spent physical press, cleared on physical release or
life/session reset. Hidden HUD, resource rebinding, rejected selection and server
confirmation do not leak that held press. CanHolster refusal does not arm the
latch for an already primed grenade. The engine still only consumes each supplied
neutral mask; no transport, wire grammar, prediction or sent history is rewritten.

Regressions exercise confirmation -> several held frames -> release -> new click
through GameClientHost, the existing input-consumption mechanism and production
weapon/effect composition for Glock/Python/MP5/Gauss/Egon/Hand Grenade/Tripmine/
Snark. Held confirmation creates no action/flash/shell/light/impact/beam/audio;
a fresh real click fires normally. CPU and production GL cases cover original
crop/tint/neutral pixels, readable glyph height, long names, four rows, skull/
world/self fallbacks, team-slot reuse, viewport bounds and expiry baseline.

Preservation record: `manual-artifacts/task-records/e29-hud-menu-corrective-20261010`:
1332 source files and34 Release rollback files SHA-verified before edits.
Debug/Release focused each:115 passed,4 asset opt-in skips,88,861 assertions.
Actual approved HUD/killfeed assets + GL + WAV cases:4 passed /1,126,526 assertions.
Core-only remains42/42 with308 cases /143,207 assertions and architecture guard.
The first parallel Release compile hit MSVC C1060 heap exhaustion; the retained
log is not a successful build. Serialized compilation (`CL_MPCount=1`) passed.
Full final Release after the last production edit:3371 registered CTest entries,
3311 passed,60 skipped,0 failed; complete driver exit0 and post-checks passed.
Direct Catch coverage:3249 discovered,3189 passed,60 skipped;1,848,845 executed
assertions, plus separate core/API308 cases /143,207 assertions. The60 skips are
explicit opt-in assets/device/corpus and filesystem/capture/WFP capability
gates, not passes; the four relevant actual-asset cases were separately enabled
and passed. Reference/off, Scenario damage-respawn-check, Null, all182 directed
weapon transitions and E28.1/E28.2 closing/receipts/verdict controls remain green.
No old assertions were relaxed; the one incorrect E29 held-frame expectation
was corrected against the pinned input chain and stronger held/release effects
regressions. New source/whitespace checks cover the untracked test file too.

Final ASan normal client/tests compilation passed after the spacing fix, but
both `--help` and focused runtime still fail before entry with `0xC0000135` and
no output; runtime is not passed. No ASan infrastructure changes were made.
The unchanged launcher selects the updated normal Release; exact reference/
peer/no-time-limit `-CheckOnly` passed with `process-launches=0`.

Current corrective executable: `build/bin/Release/hlclient.exe`,4,107,776 bytes.
SHA-256: `50D10DFB55CB96C0667D250D9B22C4F50806890B60E651A0D0EE4B58AB187FBC`.
This supersedes the executable identity above for the next manual test, not the
historical manual session. Logs and machine-readable final counts/preservation/
source mapping: `manual-artifacts/task-records/e29-hud-menu-corrective-20261010/final-verification-summary.json`.
1332 preserved source copies,34 rollback files and31 historical receipts match
their SHA manifests; missing/protected changes0. Eleven existing files and one
new test are in corrective scope. `main.cpp`, `live_runtime_stage.*`, transport,
prediction, renderer mechanisms, launcher and scripts are byte-identical to the
pre-corrective snapshot. Original assets/server/config/maps are untouched.

New manual acceptance is `not_run`; new live/HLDS/Steam/capture sessions0;
staging/commit/push none. The existing command above remains ready. Check normal
Egon/MP5/etc. kills for a readable icon/name feed; select a weapon with LMB and
hold it briefly (no fire), release then click again (normal fire). Also check
multiple feed rows, world/self kill, TAB, weapon switching and orderly A/B close.

## E29.2 — committed kill feed clock ordering after focus switches

The follow-up is `attribution=user_report`: the feed initially worked, then new
kill notices no longer appeared after switching between A/B. The user explicitly
distinguished this from expiry of an old six-second row. No executable SHA or
matching fresh run trace was supplied; the specific manual trigger is not
claimed as measured. E29/E29.1 history and previous identities remain historical.

A deterministic production defect was reproduced. DeathMsg creation uses
`GameClientHost::presentation_seconds`: double(receipt epoch) minus double(origin
epoch). Normal `main.cpp` caches double(frame time minus session start) before
`session.update`. A receipt can therefore be later than that cached frame, and
even the same exact clock instant can round slightly later under epoch-double
subtraction. `compose_multiplayer_hud` treated any `now < created_seconds` as a
reason to permanently delete an already committed notice before its first draw.
The normal focus-loss hook, `cancel_uncommitted`, does not reset multiplayer
notices, HUD resources or the session serial. Focus is the reported circumstance,
not an independently traced cause; this clock defect can also occur without it.

The only production change is in `src/games/halflife/multiplayer.cpp`: display a
committed notice immediately even if its receipt time is ahead of this cached
frame. Keep the original creation timestamp and expire at `created_seconds+6`.
No timer pause, receipt timestamp rewrite, stale replay, permanent feed, input
change or new renderer/resource path is introduced. Four-row FIFO, source dedup,
server DeathMsg/ScoreInfo authority and full session reset remain unchanged.
Both ready-sprite composition and missing-descriptor text fallback use the fix.
Timestamps are locally generated, not server-controlled; malformed custom
callers with a grossly future finite time are outside this normal-clock proof.

Six new source-owned regressions were built against the unchanged E29.1
production first: all six failed at fresh-notice visibility (exit42, six failed
assertions). The same tests pass after the production edit: six cases /5,453
assertions. They cover the actual host/chrono mixed-epoch conversion, a receipt
25ms ahead of a cached frame, A->B->A cancellation hooks, a new kill after genuine
expiry, FIFO/reset and missing-descriptor fallback. The GL case stages an actual
DeathMsg through the module/host, materializes approved project-owned sprite data,
checks source crop/tint/neutral pixels and readable name pixels, preserves the
same commands/pixels through cancellation, returns to baseline on expiry and
renders a separately staged new kill without reuploading immutable textures.
Its wider future timestamps are synthetic policy coverage, not measured live
latency. Focus tests exercise the normal cancellation hook, not real OS window
switches or a live UDP session.

Debug/Release normal VS2022/Win32 builds passed; focused each configuration:
125 cases,121 passed,4 explicit asset opt-in skips,94,314 assertions. Separately
enabled approved original HUD/killfeed assets + GL + menu WAV checks passed:
4 cases /1,126,526 assertions. Core-only Release with HL disabled passed42/42,
including308 cases /143,207 assertions and the architecture guard. ASan normal
client/tests compiled, but both `--help` and `[e292]` still fail before entry with
`0xC0000135` and no output; runtime is not passed and no infrastructure was changed.
The unchanged reference/peer/no-time-limit launcher `-CheckOnly` passed with
`process-launches=0`.

Preservation/evidence record:
`manual-artifacts/task-records/e29-killfeed-clock-corrective-20261010`.
Before edits,1,333 source and34 Release rollback files were SHA-verified. Scope
is one existing production file, two existing test files and this task record.
Normal app, host clock conversion, input/transport/prediction/renderer/SPR paths,
launcher/scripts and old182 directed transitions are unchanged. Original assets,
server/config/maps and other worktrees are untouched. Staging/commit/push none;
new live/Steam/HLDS/capture sessions0; new manual acceptance `not_run`.

For manual acceptance use the unchanged A/B command above. Create a fresh kill,
switch A->B->A, then create another kill in each direction. Confirm the new
icon/name rows appear in both clients; an older row may legitimately expire
while the other window is active. Also check LMB menu confirmation, TAB and normal
A/B close without stale notices or an extra confirmation shot.

E29.2 complete Release CTest attempt after the final production edit:
3,377 registered,3,316 passed,60 explicit skips,1 failure. The only failure is
the unchanged `hlclient_normal_close_verdict_e282_no_stock` PowerShell matrix
reaching its180s timeout, after reporting20 passing subcases. Its separate exact
CTest retry passed in161.87s with no edits to the test, deadline, classifier,
launcher or executable. The timeout is retained and this retry does not relabel
the full attempt green. All3,195 executed direct C++ Catch cases passed with
1,854,298 assertions;60 direct cases were skipped, plus the separate308 core/API
cases /143,207 assertions passed. All six new E29.2 cases passed in that full
attempt too. Because the driver stopped at CTest's exit8, its unchanged offline
post-check commands were subsequently run separately and passed. No second
full-success driver marker is claimed. The earlier tool-session-interrupted
attempt is also retained and is not counted as a completed result.

New normal Release: `build/bin/Release/hlclient.exe`,4,107,776 bytes.
SHA-256: `C264160E03298B5F36408C2DDB19BC83D84092C140E649E80C620C827094FC83`.
This is the next manual-test identity, not an attribution to the user's previous
session. Final source/rollback/historical preservation checks:1,333/34/31 files,
zero SHA mismatches; four existing source/task files changed, no new source
files. Machine-readable totals and explicit timeout/retry separation are in
`manual-artifacts/task-records/e29-killfeed-clock-corrective-20261010/final-verification-summary.json`.
New manual acceptance remains `not_run`.

## User acceptance carried into E30

The E30 request explicitly supplies “HUD готовий, все працює”. E29
`manual=passed`, `attribution=user_report`,
`scope=HUD/inventory/crosshairs and reported working presentation`.
This does not fabricate a new automatic run, an executable SHA for that manual
session or stock-exact/optional full-suite acceptance. Historical evidence and
the E29.2 timeout/retry distinction above remain unchanged. E30 manual=not_run.
