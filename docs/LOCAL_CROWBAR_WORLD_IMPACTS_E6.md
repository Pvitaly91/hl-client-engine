# E6 — local-compatible crowbar impact on static BSP world

Status: normal `hlclient.exe` integration and offline verification; **new E6
Release manual=not_run**. No server-authoritative crowbar hit or damage claim.
The pre-edit 20-file source snapshot is under
`manual-artifacts/task-records/hlc-e6-crowbar-static-world-impacts-20260929/source/`.

## Ownership and production chain

The existing B1 controller accepts one Half-Life crowbar swing; the host
attaches the eye and quantized wire-command direction once to that submitted
command. `WeaponVisuals` matches the action's generation and command sequence,
then emits one owning `LocalWorldImpactRequest`. The generic
`WorldImpactPresentation` traces the already imported BSP collision world,
checks the nearest committed brush blocker, uniquely maps an opaque static
render surface, and clips a decal polygon to its triangles. It returns a
typed result. `GameClientHost` forwards this to `HalfLifeClientModule`, which
keeps the miss pose or refines it to a hit pose and emits only HL-specific
sound intents. The same E3 `LocalAudio` mixer and E5.1 neutral OpenGL decal
pass execute those intents. The renderer, mixer and collision code contain no
crowbar ID, sample path or sequence rule.

The accepted action, not a held attack bit, replay, render frame or later
camera direction, owns the trace. Missing command-space context means no
trace or invented hit. A second sample, confirmation or replay does not
schedule another request. The initial capture click remains a no-action
command. Only a finite supported static BSP world surface is a positive local
hit. A miss, startsolid, invalid/expired trace, unavailable collision world,
surface mismatch, and a nearer represented dynamic brush blocker do not
produce a wall sound/decal or entity-hit substitute. Player/monster/Studio,
breakable, moving BSP and other entity hits are not supported. Dynamic Studio
occlusion is not represented by this BSP/brush query, so this slice cannot
promise a wall behind an unmodeled actor is masked.
Server-origin crowbar sounds can arrive independently; without a confirmed
server event/result identity this local slice cannot deduplicate those voices
against its own presentation by inference.

## Pinned SDK reference and local profile

The pinned Valve Half-Life SDK revision is
`b1b5cf5892918535619b2937bb927e46cb097ba1`. In `dlls/crowbar.cpp`,
`Swing` begins with `GetGunPosition` and a 32-unit line trace; the server can
fall back to a head-hull trace. This client uses **line only**: its neutral
collision library has no verified equivalent for that crowbar hull fallback,
and no offset heuristic was added. The 32-unit reach belongs to the HL module,
not the generic tracer. This is a local-compatible presentation, not a
server result, and it never writes HP, armor, ammo, damage, frags or life state.

The model profile verifies sequences 3/6/8 (hit) and 4/5/7 (miss) and body 1
from actual bound Studio metadata. The accepted miss starts once; a supported
world hit selects the corresponding hit pose with exactly one new restart
identity. The already emitted `weapons/cbar_miss1.wav` swing voice is retained
across that local refinement. The first profile uses a deterministic action
cycle, not random or render-frame state. Stock server sequence choice and
random pitch/volume are not claimed identical.

For a positive static hit, the HL module chooses one of
`weapons/cbar_hit1.wav` / `weapons/cbar_hit2.wav` by action identity and one
concrete fallback contact `player/pl_step1.wav`, both at the frozen world hit
point with existing spatial attenuation. This concrete choice is local-
compatible; it is not a `materials.txt` classifier and does not guess metal,
wood or glass from texture color. The pinned SDK `dlls/sound.cpp` has a
concrete `pl_step` branch and distinct material branches; the pinned
`dlls/crowbar.cpp` uses the material sound plus one `cbar_hit` variant. Missing
or pending audio does not block the decal/animation, and the mixer reports
request/ready/missing/rejected rather than interpreting a request as audible
output. The two contact voices use automatic bounded mixer voices, distinct
from the pre-existing swing weapon voice and from server audio.

`dlls/crowbar.cpp::Smack` calls `DecalGunshot(DMG_CLUB)` at +0.2 seconds.
For an ordinary BSP entity, `CBaseEntity::DamageDecal` in this pinned SDK
selects the registered `{shot1..5}` series, not a separate club texture.
E6 deterministically chooses **`{SHOT2`**, which is present in the approved
`decals.wad`; E5 Glock remains `{SHOT1`. This is an evidence-based compatible
variant, not parity with the SDK's random choice. The existing exact-root,
identity-checked importer decodes each profile into one of two bounded slots.
The E5.1 white-neutral modulation mode keeps white texels neutral over a lit
wall; there is no global white key or fullbright patch. The trace immediately
freezes the hit point, clipped geometry and action; the same map-scoped
presenter publishes it after 0.2 s on update without sleep or a second trace.
The two presentation pools each hold at most 64 decals of at most 96 vertices,
with 20 s expiry and deterministic oldest-slot replacement.

Map/session generation or world resource-identity change clears pending and
published map-scoped effects. Weapon switch or life transition cancels an
unresolved action but does not teleport or revoke an already fixed map decal.
No audio from a previous generation is replayed after teardown. The existing
Netchan, movement/reconciliation, B1 action eligibility, and E5.1 Glock/shell
paths are unchanged. `-MuteGlockFireSound` remains diagnostic-only and does
not mute crowbar or impact audio; no extra crowbar flag was needed.

## Evidence and handoff

Project-owned component tests cover action/context one-shot, hit/miss
sequence and restart, frozen 32-unit reach, a horizontal BSP floor contact,
typed skips, delayed publication,
reset, exact-root dual decal assets, independent world-space audio voices and
nonzero isolated mixer PCM. The production OpenGL A/B composition goes from
`HalfLifeClientModule` through the actual BSP query and secondary neutral
decal pass; the dark center changes while neutral in-rectangle control pixels
preserve the lit wall. The opt-in installed WAD/WAV checks are read-only
controls, not a build dependency. Actual device audibility and new E6 manual
gameplay remain unconfirmed until the user runs the prepared Release.

Final offline evidence (2026-09-29): Debug E6 focused 8/8 cases, 215
assertions; Release E6 focused 8/8, 215 assertions; final full Release
2,389 passed/31 skipped of 2,420, 819,777 assertions. The 31 skips are
opt-in asset/device and unavailable platform-capability fixtures, not E6
failures. Read-only installed WAD/WAV/model controls passed 74 assertions in
two cases. Core-only Release engine/API build and both API/architecture tests
passed 2/2. Launcher `-CheckOnly`, no-stock fake-runner fixture and
`git diff --check` passed. ASan runtime was not revalidated and remains
unverified. Release `hlclient.exe` SHA-256 is
`A9CC208A11DF33379080305A9A3A9394C89041D499C2D401B5C222370CF6A2AF`.
New stock/HLDS/managed/live sessions: 0. Staging, commit and push: none.

## Subsequent E6 manual report

`attribution=user_report`, `manual=passed` for this exact scope: crowbar miss,
wall hit, contact sound and decal worked; the decal remained in place after
camera motion; Glock remained functional. This report does not establish
entity/player hits, damage, hull fallback, multiplayer, stock-exact timing,
material classification, every map or every surface orientation. It does not
retroactively change the earlier new-E6-build `manual=not_run` handoff above.
The later E7 executable requires its own manual validation.
