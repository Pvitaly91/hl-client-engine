# Architecture

## Observer Egon emission (2026-10-16)

The existing optional beam scene-light profile can now specify a bounded RGB
override and whether to inherit ribbon opacity. The application still samples
only accepted current-frame geometry into the existing light budget; a fully
transparent ribbon cannot emit. Defaults preserve existing modules. Half-Life
uses this for the approved observer Egon main strand, whose original server
ribbon RGB/opacity pulses previously suppressed its scene light. Its fixed
blue-violet emission now matches the current local beam while visible server
ribbon parameters remain unchanged. No new resource path, lifetime, renderer
or network authority is added. See E22's additive follow-up and evidence under
`manual-artifacts/task-records/egon-observer-light-20261016/`.

Follow-up validation: normal Debug client build, Release client/test target,
199 focused Release cases (66,622 assertions; two approved-asset opt-in skips),
core-only Release build and all 44 CTests, normal architecture guard, launcher
`-CheckOnly` and null bootstrap pass. The isolated actual attached-observer
GPU test verifies stable wall/Studio lighting across SDK dim/bright ribbon
states, preserved ribbon presentation, transparent/removal cleanup and no
duplicate light from the noise strand. Local fixture readbacks match the prior
build byte for byte. Historical full-suite counts below are not this follow-up's
test count. Manual acceptance remains `not_run`; live sessions/captures and
staging/commit/push are zero. ASan runtime status is unchanged/unverified.

New `build/bin/Release/hlclient.exe` SHA-256:
`F2D7A32A0B1E40C73019EDEF9E8C80A1E9E27A7A07A38BD6EB7761BA2F4F9CF7`.
The launcher stays at SHA-256
`636D89EE3816B7F2B330D1E68F1AB03B90D8FADD36219E252E037AF7482425FA`;
the normal launch path needs no additional option or asset copy.

## Last-weapon input and beam scene illumination (2026-10-16)

Half-Life opts into physical Q through the neutral movement/input policy. The
application forwards a focused, captured press edge through the existing typed
inventory handoff; SDL key repeat never creates another press. The game-owned
HUD/inventory owner remembers confirmed active/previous weapons, including
committed switches between rendered frames. Requests, menu candidates and
repeated clip updates do not advance this history. Life, reset and ownership
boundaries invalidate it. The server still decides whether a requested switch
can occur; no predicted active item, ammo or weapon effect is manufactured.
The action works independently of visible HUD drawing. This reconstructs
observed confirmed history rather than exposing the server's private last-item
pointer. See the additive E29 record for source reference and coverage.

Egon separately opts into a bounded numeric scene-light profile. Its logical
main strand supplies illumination; the second strand and existing halo do not
duplicate lights. Approved local and observer sprite materialization prepares
at most four current-frame point lights, with symmetric endpoint-inset sampling
and nearest-camera selection across simultaneous emitters. Successful frame
publication is required. Deferred composition fills only remaining slots in
the existing eight-light batch, preserving prior muzzle/impact/explosion work.
The existing world, brush and Studio shaders consume these lights unchanged.
No extra light lifetime, resource opener, renderer or physics owner is added.
Release/removal and generation reset discard the candidates. The accepted
spiral/halo geometry and camera-local viewmodel lighting remain unchanged.
These are unshadowed point lights, not shadow-mapped lighting; long beams use
bounded sparse samples. See E22 for the chosen visual profile and limitations.

The pre-edit source/executable snapshot and this task's offline verification
belong to `manual-artifacts/task-records/q-last-weapon-egon-light-20261016/`.
No live game/server session, staging, commit or push is part of this task.
New manual acceptance remains `not_run` until the user tests the new build.

Final source-frozen validation uses VS2022 v143 Win32. Debug `hlclient_tests`
(including normal client dependency) and Release `ALL_BUILD` passed. Debug
Q/E22/E29/input/game regression passed 292 cases and 558,289 assertions, with
five explicit resource opt-in skips; Release Q/E22/E29 passed 87 cases and
499,478 assertions, with four such skips. The separate illumination GPU runs
each passed four cases and 206 assertions; local and observer isolated readback
brightens 56,644 world and 19,602 Studio pixels, and saved Debug/Release PPMs
match byte for byte. Translated runtime-brush lighting, far/stop controls and
existing beam/halo invariants also pass.

The full Release CTest after the last production edit contains 3,733 tests:
3,665 passed, 68 explicit capability/private-resource opt-in skips, zero
failures (784.37 seconds). Core-only Release `ALL_BUILD` and all 44 CTests pass
(78.67 seconds), including the architecture boundary guard. No skipped test is
claimed as executed acceptance. ASan runtime was not rerun and remains
unverified; these normal Debug/Release results do not change that status.

The normal CLI version/help/null-renderer bootstrap, reference transport and
orchestrator literal checks, G/H summary controls, no-stock manual-launch
fixture, source-only and full launcher `-CheckOnly` all passed. No live game,
Steam API session, server modification or capture was performed. Both changes
are enabled in the normal application without extra flags or copied assets.
The launcher retains `192.168.0.104:27016` and its settings file is unchanged.

Ready Release: `build/bin/Release/hlclient.exe`, SHA-256
`727891A4907E225663A5893751256A921C927629E26E05BEB963915EE5C0B253`.
The rebuilt `HLClientLauncher.exe` SHA-256 is
`636D89EE3816B7F2B330D1E68F1AB03B90D8FADD36219E252E037AF7482425FA`.
These identify the new files, not any historical manual session. The final
audit verifies all 1,426 preserved source copies, no missing source files,
unchanged branch/HEAD/index and clean `git diff --check`; no stage, commit or
push was performed. Existing task histories and unrelated edits are preserved.

## Original environment/muzzle resources and beam halo (2026-10-15)

The normal application copies the existing live signon sky name before its
async asset preparation. The GoldSrc sky adapter validates a bounded basename,
opens six `gfx/env` panels only through the selected world's exact approved
root, and uses a separate filesystem-free bounded TGA decoder. An absent server
name may use inert worldspawn `skyname`; a different worldspawn field is never
inferred as a sky. Preparation is transactional and optional: missing/rejected
panels retain the existing fallback without mixing installations. The renderer
receives six owning RGBA images only, no names, paths or GoldSrc state. Its
translation-free cube and sky-surface branch share depth-tested ordinary scene
rendering; sky polygons retain their actual depth to occlude objects beyond
the sky boundary. See `WORLD_RENDER_PACKAGE.md` for resource and orientation
evidence and the deliberately bounded TGA-only profile.

The concrete module declares compiled muzzle sprite dependencies and selects
source-scale/frame profiles from validated weapon events. The same preparation
worker opens these exact-root resources through the existing source capability
and SPR importer, without inventing server model indices. The existing entity
package supports bounded owning presentation sprite sources alongside its
presentation Studio sources. Generic materialization supplies ordinary additive
sprites in world and camera-local passes, preserving the original source frame
extents, black-neutral pixels, depth and immutable GPU texture cache. No filename
or weapon rule enters the renderer. Missing sprites do not suppress independent
flash lights, audio or shells. Legacy procedural intents remain available to
alternate modules; selected HL profiles use the original assets. See E4's
additive follow-up for exact mapping and stock-parity limits.

An optional bounded neutral beam-halo profile adds a second low-opacity sample
of the same ribbon texture and exact sampled spine. Half-Life selects it for
Egon's approved beam resource; alternate modules and other resources default
off. Core geometry, time, endpoints and lifetime stay unchanged. Visibility
bounds include the halo, while wall depth testing stays enabled; the halo
does not create another effect owner, light system or postprocessing bloom.
The local and committed observer paths share it. See E22 for the selected
width/opacity and isolated framebuffer evidence.

Coordinated evidence belongs to
`manual-artifacts/task-records/sky-muzzle-egon-20261015/`. The source snapshot
preserves the pre-task dirty tree; this follow-up does not rename completed
milestones. Manual acceptance remains `not_run`: check the server-selected sky
while turning/moving, original Glock/MP5/Python/Shotgun flashes in first person
and as an observer, and Egon glow with the accepted spiral unchanged and hidden
by opaque walls. No new live session or server change is part of these offline
checks. The normal launcher and its settings remain the user's existing path.

Final coordinated verification (VS2022 v143 Win32): Debug target and Release
ALL_BUILD passed. Each focused Debug/Release run passed 29 cases and 11,915
assertions, with 3 explicit original-resource opt-in skips. The wider Debug
renderer/game-interface regression passed 287 cases and 1,513,056 assertions
(12 opt-in skips). A separate read-only original-resource run passed all 6
cases / 4,862 assertions, including the six desert panels and all four muzzle
marker profiles on the actual OpenGL backend. Local/observer halo A/B controls
measured 2,259/2,288 added halo pixels around 1,094/1,103 core pixels, with
unchanged spine, repeated texture-upload count and successful wall-depth checks.

After the final production edit, the complete Release CTest registration ran:
3,723 tests, 3,655 passed, 68 explicit capability/private-resource opt-in skips,
zero failures (808.12 seconds). The subsequent normal CLI/null-renderer,
transport, no-stock launch fixture and SourceOnly CheckOnly gates passed.
The separate full launcher CheckOnly passed too, without a game/server launch.
Core-only Release ALL_BUILD and all 44 CTest entries, including the architecture
guard, passed. ASan runtime remains unverified and was not rerun in this task.

All 1,420 pre-task snapshot files were hash-verified; no source was deleted.
Branch, HEAD and index are unchanged, as are the launcher executable and saved
settings. No staging/commit/push, game/stock/HLDS session or managed capture.
The current normal Release client is 4,730,880 bytes; SHA-256:
`17D8E688EB1146645A2B5D5C5B0869680A93D1B3BD0E8A5AA3FCC6893A480604`.
This identifies the new build, not any earlier user screenshot/manual run.
`verification.json` and `final-audit.json` in the evidence directory retain the
counts, explicit limitations and unchanged launcher SHA. New manual status
remains `not_run`; offline pixels do not claim stock-binary visual parity.

## E32 contact and stale-seed recovery (2026-10-15)

The live owner separates validated simulation from its camera policy. A finite,
successfully rebased but unclassified large correction now keeps the exact
corrected history with a hard visual snap. It cannot retain an old residual,
support predecessor or exterior-corner bridge. Failed seed occupancy, invalid
history/suffix, life boundaries and failed simulation still suspend. The
velocity-backed 100 ms visual profile is unchanged; no damage cause is inferred.

Before ground queries, replay or movement-audio rewind, the existing owner
checks exact immutable boundary/suffix identity, sample clocks and the existing
64-command bound. Recovery from inactive, missing or stale accepted authority
also requires boundary sample age and summed suffix duration within 250 ms.
Fresh continuous authority may legitimately repeat an older transport ACK;
that is not a stale state and retains the original bounded rebase contract.
Expired recovery records still commit protocol, gameplay and canonical
observations; only speculative replay is rejected. It neither drops network
records nor fabricates jump/duck state. These are per-seed limits, not an
aggregate CPU-time guarantee.

The movement kernel validates a generated step-up endpoint before speculative
horizontal movement. A contact rounded into the solid BSP child permits only
one outward binary32 neighbour (at most 1/64 unit), complete-scene clearance
and a clear hull path. An unavailable candidate leaves the valid direct route;
provider errors remain failures. No global epsilon, map, hull or collision
permission changes. Existing bounded scratch observations retain actual failed
initial/ground/slide queries, with movement and support purposes distinguished.

## E32 server-velocity camera correction (2026-10-15)

The normal reference owner may retain a successfully rebased history for a
bounded position correction explained by a newly observed server velocity.
It compares the exact acknowledged movement boundary, not a guessed damage
event. Eligibility requires continuous life/pose, fresh authority, no new
setangle or advertised EF_NOINTERP, and clear full-hull and eye paths. As of
the contact/stale-seed follow-up above, an unclassified correction beyond the
16-unit visual gate keeps a valid physical rebase but snaps instead of smoothing.

The existing neutral visual-correction owner carries at most 32 units of
camera-only residual for 100 ms. Canonical origin/velocity and suffix replay
are corrected immediately. Ordinary accepted ACKs retarget the displayed eye
without adding its previous residual twice or extending the original deadline.
Every sample rechecks pose/context and retains world, brush and actor-hull
checks; this profile never uses the small exterior-corner route. Only one
immutable pose reference is retained, cleared on cancellation/completion.
No damage, input, command, renderer, audio, server or wire authority changes.
The policy is local-compatible, not stock-exact damage prediction. See the
E32 record and [visual correction](PREDICTION_VISUAL_CORRECTION.md).

## E32 lift contact and bounded frame work (2026-10-15)

The existing vertical-support command solver can acquire a rising brush top
after the ordinary kernel selects a tied world floor. This is only a new
contact witnessed between the command's start/end scenes: the grounded walking
hull must be clear in the start scene, blocked by that exact rising brush at
the end, and recover to its walkable plane within the derived command rise.
The upward path retains world, other brush and entity-box blockers; complete
end-scene occupancy is mandatory. Side/deep/descending contacts and blocked
pushes retain failure. No unseen server stop is invented; newly observed stop
authority replaces derived movement through the normal rebase.

Runtime interpolation shares one input validator with the buffered player and
ordinary owners. Validation-only calls no longer construct, hash and discard
whole output observations before the real sample. Output ownership, clocks,
lifetimes, finite-value/byte limits and canonical validation remain unchanged.
Runtime replay reserves its configured bounded identity ledger before generation
publication; a successful commit appends without copying past identities.
Decoder history publication transfers ownership with the intended move, avoiding
an accidental copy from a const temporary. Neither wire grammar nor the atomic
publication boundary changes. See the 2026-10-15 E32 follow-up for evidence.

## E32 long-session diagnostics and server-enabled crouch jump (2026-10-14)

Normal keyboard sessions keep bounded rolling send/sample diagnostic histories.
Lifetime counters and causal prediction carriers are independent of retention;
scripted evidence modes preserve their strict full-history contract. Only typed
failure categories reach durable external diagnostics.

The negotiated clientdata physics string is bounded neutral observation data.
The game movement policy selects an exact capability key and optional crouch-jump
parameters; no Half-Life key or boost constant is hardcoded in the engine.
Coherent server capability becomes immutable movement state and participates
in prediction identities/rebase. Default/core-only policies select no boost.

World glow sprites use ordinary world depth testing with additive blending and
no depth writes. The renderer knows no RPG/Egon names; resource and effect
ownership remain in the existing module/application paths.

## E32 lift reseed and command post-think ordering (2026-10-14)

The immutable neutral command retains independent bounded pre/post velocity
deltas. Optional GameClientAPI phase selection defaults to the unchanged pre
path; Half-Life alone selects the post path for Gauss. The normal live owner
captures phase and delta once alongside the exact submitted sequence. History
hashes nonzero post values separately; wire bytes remain unchanged. The ordinary
movement and ladder solvers add post velocity after movement/friction/collision,
without integrating it into the firing command's origin or changing ground
evidence. Rebase reuses the saved command, never current weapon state or effects.

Canonical ground derivation may witness the exact immutable reference command
that produced an upward post delta. Matching sequence/wire fields and the
existing generation/life gates allow the normal support query to retain the
completed step's observed ground flag even above the ordinary upward limit;
free occupancy and walkable support are still required. No delta is applied
during categorization. Default/no-witness and embedded-brush recovery rules
remain unchanged, and the next real movement step handles leaving the ground.

Coherent grounded prediction seeds may recover a quantized brush top within a
local-compatible1/8-unit vertical envelope. Full-scene occupancy, exact brush
identity and entry/downward-support walkable-plane witnesses are required. The
plane is solved geometrically, not by enlarging trace epsilon. World/box/ULP
rules, airborne/deep/wall/ceiling rejection and canonical state remain unchanged.
No renderer, camera teleport, server activation, new asset open or physics owner
is introduced. See the additive E32/E21 follow-ups for evidence and limitations.

## E31.1 native display and player appearance boundaries

Versioned neutral launch settings carry requested scene MSAA and independent
HUD scale. SDL3 reports actual logical/pixel extents and display density; it
does not scale game HUD commands or request redundant default-framebuffer AA.
The existing OpenGL renderer owns bounded multisampled color/depth storage,
native-size resolve, then the existing once-only image adjustment and sharp
HUD passes. No game policy, filesystem names or HL model/color rules enter it.

Committed userinfo projects owning bounded appearance fields through the
optional GameClientAPI. The HL module chooses safe player-model virtual names
and supported indexed-palette regions. The approved exact-root adapter imports
these bounded local presentation dependencies without inventing network slots.
Supplemental immutable Studio owners feed the ordinary pose/frame builder;
canonical modelindex, server collision/damage and prediction remain unchanged.
Pose identity follows the selected model and corpses freeze their originating
appearance. Core-only and alternate modules retain default optional behavior.
See [E31.1 implementation and evidence](FULLSCREEN_HUD_MSAA_PLAYER_APPEARANCE_E31_1.md).

The fullscreen timing follow-up keeps reference movement history transactional.
While a prepared outgoing move owns its revision, the live owner progresses that
submission before advancing sampling deadlines or consuming input edges. The
single shared driver pump and exact send-receipt accounting remain the ownership
boundaries. Unplanned unsent history still permits ordinary bounded sampling;
focus/life/close cancellation and stalled-transmission deadlines remain active.
Durable external diagnostics retain only typed command failure labels.

The existing opt-in movement-session recorder also owns bounded FPS episode
observations: contiguous known controlled/alive endpoint intervals over20ms,
64 worst episode details, whole-session counts, and original worst-frame
CPU/stage/delayed-GPU timings. This stays within the512KiB aggregate bound,
independent of exhausted detailed incident slots, with no per-frame I/O,
unbounded history or gameplay/timing mutation. Reciprocal frame-time FPS is
explicitly distinguished from sustained FPS and root-cause inference. See the
2026-10-13 FPS logging follow-up in `RUN_FIRE_LIFT_TOP_CORNER_E32_1.md`.

## E29 screen-space HUD boundary

The concrete module declares bounded immutable HUD descriptor dependencies and
owns roles, tier selection, weapon/visibility policy, menu/history and UI cues.
`GameClientAPI` carries descriptor metadata and ID/revision/frame/subrect draw
values only. The generic GoldSrc HUD adapter opens the selected approved world's
exact root through the existing locator/opener and AssetManager/SPR importer.
Normal application materialization binds immutable sprite owners; OpenGL draws
one bounded screen-space pass without weapon names or filesystem access.
Menu consumption precedes command materialization and never rewrites history.
SDL's optional Escape-input ownership is an overlay mechanism; the old default
and focus-loss/event-limit fail-safes remain. See
[E29 resource, authority, lifecycle and verification contract](ORIGINAL_SPRITE_HUD_INVENTORY_CROSSHAIRS_E29.md).

## E27 committed sound cosmetic observation

Atomic runtime replay publishes neutral `CommittedSoundObservation` through
GameClientHost after entity/TE commit, independently of the audio queue, output
device, focus and scene visibility. The host converts the receive clock; the
normal app binds advertised model/sound slots without granting file access.
Only HalfLifeClientModule interprets the exact Snark blast name, CHAN_ITEM,
observed living-model entity continuity and the yellow cosmetic profile.
Approved sprite materialization, depth/blending and server audio stay shared.
Sound-derived intent provenance is distinct from actual server TE provenance;
the fallback neither invents a TE nor replays audio or gameplay. Core-only's
alternate module exercises the same optional seam with no Half-Life fallback.
See the authorized sound-pop follow-up in the existing E27 task record.

E27.1 adds a neutral owning768-byte palette table and compiled gfx-leaf
preparation token. The startup asset worker resolves only the selected world's
verified root through the existing opener. HAL interprets host palette195;
renderer receives RGB modulation only. A compact generic sprite burst expands
once in the existing temporary owner (16/event,128drops+32ordinary sprites,
160output), with seeded independent velocities/lifetimes/fixed imported frames.
No game names or damage rules enter its animation/materialization mechanisms.
Spray and droplets use the original two approved sprite assets; numeric dynamics
are explicitly local-compatible until stock visual comparison is available.
Optional actual server blood-setting evidence reaches GameClientAPI through
ordinary runner arguments. HAL suppresses sound fallback on explicit false;
real TE retains server authority. Module generation reset clears metadata and
receipts; app rebinds approved palette/policy with resource slots. See E27.1's
existing task record for original-asset facts, visual artifacts and limitations.

## E26 Tripmine boundary

Half-Life owns ID13/no-clip/exhaustible/inventory/HUD, exact tripfire event
mapping, validated v_tripmine roles, provisional128-unit DRAW eligibility,
bounded receipts and retirement. `prepare_presentation` borrows a synchronous
neutral point query only during a frame; no callback/engine handle is retained.
The app uses the latest committed observation with the existing BSP/brush/BBOX
union and protocol player hulls, excluding the receiving actor. Shared point,
standing and duck sweeps execute one implementation; physics/history/replay
are not changed by presentation queries.

World v_tripmine/body3/sequence7, peer p-model, arming, ENTITY_BEAM, audio/pitch,
removal and TE_EXPLOSION/TE_SMOKE are server-driven generic paths. A bounded
neutral beam endpoint receipt prevents stale attachment across observed target
slot/model/player-life boundaries. No client mine, arming/fuse, beam trigger,
owner-death cleanup or damage authority is added. See the
[E26 recovery/implementation contract](TRIPMINE_PLACEMENT_ARMING_LASER_E26.md).

The shared packet decoder projects negotiated common entity-state members when
a new full-packet entity reuses a signon slot under another normal/custom beam
schema. The signon baseline remains immutable; generation, member layout,
alternate-slot and existing-delta identity checks stay strict. This is a generic
GoldSrc decoding mechanism, not a mine-specific renderer or fallback.

## E25 Satchel boundary

The E32 gib-contact correction adds at most8 owning exact virtual-model/
movetype point-box exclusions to the immutable game movement policy. The
generic current-frame builder applies them only to complete, finite, validated
nonplayer BBOX observations with all six relative bound coordinates equal to
zero. Half-Life selects its pinned SDK gib pairs; the core selects none by
default and knows no gib names. A model slot number alone, absent movetype or
nonzero bounds never removes a blocker. This is a narrow local-compatible
prediction filter, not server collision/damage authority or a global degenerate
box rule. Canonical entities and ordinary visual/audio presentation remain
unchanged; there is no new asset open, copied physics or renderer exception.
See the gib-contact follow-up in `RUN_FIRE_LIFT_TOP_CORNER_E32_1.md`.

Half-Life owns ID14/no-clip/select-empty rules, chargeReady0/1/2 interpretation,
validated charge/radio model roles, command-time throw/detonator presentation,
bounded unconfirmed reservations and retirement. The two-model pair belongs to
one weapon lifecycle: an authoritative model echo does not reset its action.

GameClientAPI exposes compiled virtual model preparation tokens and at most8
owning metadata variants. The normal app resolves these solely against its
already approved session resource table/cache. The module retains two validated
models; the host/renderer knows no Satchel filenames or state meaning. Reset,
resource revision and input cancellation have explicit ownership boundaries.
The same API is covered by the core-only project-owned alternate module.

The neutral projection adds optional signed weapon iuser1 with exact current
mask presence, and optional ordinary owner reference. It does not interpret
chargeReady or perform owner searches. Wire grammar, canonical hashing and atomic
publication remain unchanged. Existing packet entity/Studio interpolation,
server svc_sound, TE_EXPLOSION/TE_SMOKE and p-model bone merge execute all world
behavior. No client charge, physics, fuse, detonation or damage is introduced.
See [E25 contract and evidence](SATCHEL_DEPLOY_RADIO_DETONATE_E25.md).

The deployed-charge follow-up adds negotiated owning optional entity bounds.
Half-Life explicitly selects BBOX/receiving-owner filtering through its existing
movement policy; the generic current-frame context/query executes bounded
axis-aligned hull sweeps alongside BSP/brush collision. Rendering/model readiness
does not supply or remove those bounds. No charge/object physics or damage
authority is added. The standard owner projection is used by this explicitly
selected prediction filter, not to invent local gameplay ownership.

Half-Life also selects a bounded authoritative translation transition for
confirmed death. The application camera mechanism samples it from explicit
presentation time and resets it at session/life boundaries. It is presentation
only; live movement prediction and remote Studio death playback are unchanged.
The same neutral bounds/query and camera mechanism run in core-only tests with
no Half-Life linkage or installation. See the E25 contact/death-camera follow-up.

The clustered-box follow-up keeps physics/replay unchanged and repairs H3's
render-only step path. Between same-hull grounded endpoints with world/box
support and a height delta within observed server `step_size`, the shared
`sample_reference_box_step_eye` verifies the complete up/across/down hull
route and the sampled free hull. Its eye uses the existing command-time alpha
within that hull, with independent BSP/brush eye clearance. It does not fall
back from a partly interpolated eye to the old endpoint because a straight
body chord intersects the low box. Walls, ceilings, airborne/hull transitions,
missing/invalid step limits and failed queries retain existing H3 handling.
No Satchel class/model/owner meaning is added to this generic mechanism.
Death-camera transitions and authority/correction/history/TX remain separate.

## E24 Hand Grenade boundary

The existing Half-Life weapon controller owns the validated grenade profile and
prime/hold/release/throw/retirement presentation. It consumes immutable submitted
commands through GameClientAPI, including optional owning command-time angles,
observed punch and player velocity. The new neutral action kinds are `prime` and
`throw_release`, not firearm fire events. No local grenade entity, fuse callback,
collision simulation, damage or score authority is created.

The schema decoder exposes its already parsed changed-field mask as bounded
metadata. Optional weapon-slot auxiliary floats retain negotiated `fuser2/3`
values and current-record presence; Half-Life alone interprets them as absolute
StartThrow/ReleaseThrow times. These are distinct from clientdata Gauss fields.
Wire grammar, canonical hashing and atomic publication are unchanged. Explicit
new corrections must not be confused with reconstructed old zero values.

World grenades reuse ordinary BOUNCE entity interpolation and Studio models,
committed spatial server audio, TE_EXPLOSION/TE_SMOKE and E11 p-model merging.
No grenade-named resource or policy is added to renderer, transport or audio
mechanisms. See [E24 contract and evidence](HAND_GRENADE_COOK_THROW_E24.md).

## E23 Hornet Gun boundary

The Half-Life module owns exact Hornet Gun ID/name and no-clip semantics,
validated v-model roles, TRACK/FAST event interpretation, local presentation
cooldowns, deterministic launch samples/recoil and server-derived Hornets HUD.
The existing weapon controller retains bounded presentation receipts, not an
independent ammunition store. Biological recharge and targeting remain server
rules: no new recharge protocol field, client projectile, homing AI, hit or
damage computation is introduced.

Ordinary packet entity model indices, transforms and Studio state use the
existing entity pipeline; modelindex=0 hides a live entity without requiring
its deletion. Generic TE_BEAMFOLLOW consumes per-message resource/color/width
and maintains entity-lifetime-scoped history. An unprofiled trail's transient
tip reaches the current interpolated entity between retained 20-ms samples;
this adds neither physics nor history growth. RPG's smoke profile is unchanged.
Server buzz/hit sounds remain committed svc_sound events and E13/E14 remain
solely server-driven death/score presentation.

`SoundReferenceSource::module_profile_dependency` carries an owning exact virtual
token selected only by a compiled module's allowlist (E23: three agrunt launch
samples). It is never selected from an untrusted server string or MDL marker.
The shared loader validates a flat canonical directory/basename.wav, forbids
absolute/traversing/double-sound-prefix names and opens only through the existing
approved exact world root. Async bounds and readiness deadlines are unchanged;
no game-specific filename, AI or recharge policy is added to lower core targets.
See [E23 contract and evidence](HORNET_GUN_TRACKING_FAST_RECHARGE_E23.md).

## E22 continuous beam boundary

Half-Life's `EgonPresentation` owns primary-only/no-clip/shared-uranium semantics,
assigned fire/stop events, lifecycle/confirmation, original sample/sequence
profiles and verified p-model socket policy. It runs through the same module,
host, viewmodel, scene and audio composition as the other weapons.

Generic protocol projection separates custom beam coordinates/references from
ordinary Euler/Studio fields. Checked mixed intra bases keep the selected schema
and target defaults. Current committed beam entities are resolved against current
owner attachments/merged sockets; the receiving override is module-handoff gated.
Ordinary endpoint sprites retain server frame/scale/visibility. The established
sprite renderer executes bounded absolute-time noisy/scrolling ribbons; its
neutral sine profile winds a 64-segment helix around the beam axis while the
secondary noise retains 32 segments. Endpoint pinning, a short-trace radius cap
and shared conservative bounds preserve attachment and visibility. Beam sprite U
is across ribbon width (the transverse glow profile); V scrolls/repeats along
its length. Ordinary sprite UVs are unchanged. The renderer knows no Egon/resource
name. Audio's stable owner can replace a finite startup with a
loop, retain only current continuous intent through focus mute, and clear the
exact domain on reset. No second loop, device, raw send or SDK ABI crosses G1.

Normal `hlclient_game_halflife` privately uses the pure shared `studio_pose` for
cached immutable socket planning; lower targets never depend on the game. The
same host/API is used by the core-only alternate persistent-beam test module.
Server ScreenShake and ScreenFade are strict transactional HL decoding with
bounded receipt-clock presentation (2026-10-14 follow-up). The module supplies
neutral camera offsets and RGBA/modulation; OpenGL privately adjusts the world
camera and composites before HUD, never changing command aim/prediction/listener.
The original E22 shake-deferred acceptance remains historical, not current.
See [E22 contract, resources, bounds and evidence](EGON_CONTINUOUS_BEAM_E22.md).

## E17 Shotgun composition

The existing `hlclient_game_halflife` module owns Shotgun selection, validated
v/p-model profiles, assigned single/double event bindings, cadence/recoil,
server-observed special reload stages and fire/contact sample policy. It emits
one neutral flash/light, one or two uniquely identified named shells and four
or eight independently identified world-hit requests. No extra loop, audio
device, physics simulator, damage authority or renderer weapon switch exists.

`GameClientAPI` retains owned bounded command/event snapshots: accepted-command
eye and direction/right/up are frozen once; committed-only local events carry
their own exact event-time context. Effects never borrow RX or asset buffers.
Neutral frames allow at most two shells/eight rays. The application consumes
every entry through existing approved assets, BSP point sweeps, decal/particle
composition, shell pool and spatial mixer. Each shell carries its virtual model
token through the pool, so brass and shotshells materialize independently using
the same approved Studio bindings. Pool slots are not causal action identities.

The generic replay projection exposes optional unsigned `m_fInSpecialReload`
and, only when exactly advertised, finite `m_flPumpTime`; it does not invent
missing timers or interpret reload policy. The historical canonical hash is
unchanged. All weapon semantics remain above the existing G1 boundary; the
Half-Life-OFF build uses the same generic code and alternate test module.
Reload/pump audio remains committed server sound, not a locally repeated MDL
marker. HLDM spread, sockets, resource evidence, authority/lifecycle limits and
verification are recorded in [E17](SHOTGUN_SINGLE_DOUBLE_RELOAD_E17.md).

The camera-local Studio pass has an independent 1-unit near plane, preserving
the world FOV/far plane without mutating world camera/depth. This neutral
projection contract clips geometry parked at the eye (including the inspected
Shotgun's idle reload-shell mesh) while retaining ordinary held/reload geometry.
It does not hide a named weapon/bodypart or alter model bones, assets or game
rules. Both ordinary renderer branches use `camera_local_first_person_camera`.

## Two-client weapon effects correction (2026-10-01)

The committed scripted-event bridge now connects the bounded `svc_event` and
`svc_event_reliable` observations to `game_api/remote_effects.hpp`. It publishes
only after the complete runtime record commits. Packet references select the
exact preceding packet snapshot ordinal, not an edict number, rendered entity
index, or local-player guess. Explicit delta-field presence is retained as
metadata; explicit zero coordinates are not confused with inherited vectors.

`games/halflife/remote_effects.*` owns the exact advertised Glock/crowbar event
bindings, sample choices, emitter identity, one-shot dedup and bounded outputs.
The host never executes `.sc` text. `app/remote_effect_presentation.*` executes
those neutral requests using the existing world-impact and shell simulation
owners. OpenGL draws bounded world flash billboards before first-person depth
reset. Local and remote presentation audio share the approved loader, mixer
and device, with disjoint voice identities. Scripted event delivery is separate
from movement replay and local weapon confirmation.

Command IDs remain contiguous across scheduler stall recovery. Local weapon
notifications retain their exact generated sample deadline through the TX
receipt instead of deriving wall time from command number. This changes no
wire bytes, physics cadence, history identity, or game authority.

Remote effects currently cover Glock fire/shell/static-world impacts and
crowbar swing. Server `svc_sound` continues to supply crowbar hit sounds.
World muzzle position, missing velocity inheritance and inherited player pitch
are a documented compatible presentation profile, not stock-binary parity.
Unsupported events/delays are explicit skips; the renderer retains one point
light (local shot priority, otherwise closest active remote light). See the
additive correction record in `REMOTE_PLAYER_MOVEMENT_AUDIO_E9.md`.

E9 verifies and hardens the existing E1 committed server-audio path, without
remote movement heuristics or a second scheduler. One-shots keep server-event
origin; looping sources may follow valid entity observations until a committed
slot boundary invalidates attachment. Local E8 remains module-owned and uses
its separate presentation voice namespace. Optional same-owned-host A/B manual
composition enables background audio only for A and disables B output; default
single-client focus policy is unchanged. See
[E9 remote sound contract](REMOTE_PLAYER_MOVEMENT_AUDIO_E9.md).

E8 extends the existing command-driven local movement audio path, not physics
or rendering: exact support face/origin -> the same owning E7 materials table
-> Half-Life step profile -> neutral approved spatial one-shot. Reversible
cadence is separate from monotonic heard foot identity; movement pending cues
have life ownership independent of weapon scope. See
[E8 material footsteps](HALFLIFE_MATERIAL_FOOTSTEPS_E8.md).

E7 carries an exact neutral BSP surface ID and canonical texture name from
the shared impact tracer through GameClientAPI. A single session-owned
Half-Life `materials.txt` classifier selects Glock/crowbar material samples;
the existing generic audio and decal mechanisms execute them. See
[E7 static surface impacts](HALFLIFE_SURFACE_MATERIAL_IMPACTS_E7.md).

E6 extends the same boundary with local-compatible crowbar static-world
impact presentation: [E6 contract](LOCAL_CROWBAR_WORLD_IMPACTS_E6.md).
E5.1 corrects the existing E5 path without moving ownership: the Half-Life
module selects the `{SHOT1` white-neutral decal and concrete-hit/shell-contact
sound profiles; shared BSP point sweeps emit neutral contact observations,
OpenGL executes a profile-selected blend mode, and the E3 approved loader/mixer
plays distinct spatial one-shots. See
[E5/E5.1 impacts and contact audio](LOCAL_GLOCK_WORLD_IMPACTS_E5.md).

E4 local Glock flash/casing presentation: see
[LOCAL_WEAPON_VISUALS_E4.md](LOCAL_WEAPON_VISUALS_E4.md). The G1 Half-Life
module owns accepted-action and Studio marker meaning; the shared renderer
only consumes neutral camera-local/world-space visual instances.

E3 local dry movement sound boundary: see
[LOCAL_MOVEMENT_AUDIO_E3.md](LOCAL_MOVEMENT_AUDIO_E3.md). The Half-Life module
owns sound rules; the host supplies completed prediction commands and an
identity-constrained presentation-only surface trace.

R1 terrain/ladder/viewmodel corrective boundary: see
[TERRAIN_LADDER_VIEWMODEL_R1.md](TERRAIN_LADDER_VIEWMODEL_R1.md). The concrete
Half-Life module selects ladder parameters; shared collision, history and
renderer mechanisms do not link the concrete game.

## E1 shared server audio

The normal application now composes a committed GoldSrc sound outbox, exact
sparse approved sound-resource loader, neutral PCM WAV importer/mixer and SDL3
playback. `hlclient_audio` and `hlclient_audio_sdl` know no GoldSrc or game rules;
`hlclient_goldsrc_audio` translates committed protocol events and
`hlclient_goldsrc_sound_assets` retains approved resource capabilities. Neither
depends on HalfLifeClientModule. Sound is an event side channel after the
existing atomic world commit, not a second scene or prediction side effect.
Listener/brush source sampling uses the final D4 presentation boundary without
visibility gating. Bounds, lifecycle, null/device policy and pinned references
are in [E1 server audio](SERVER_AUDIO_E1.md). Existing module and scene graphs
remain intact; future locally generated game sounds belong to the game module.

## Live brush rendering (D3)

The G1 game boundary is unchanged. `RuntimeReplayLocalAssets` now binds exact
current-session `*N` model references through the canonical BSP import attachment
to `GoldSrcBrushRenderLibraryBuilder`. Immutable world/brush resources are
separate from owning `RuntimeBrushRenderFrame` instances published atomically
with Studio/Sprite frames. `ClientWorldState -> RenderScene -> OpenGlRenderer`
retains the existing world, viewmodel and HUD domains. No Half-Life classname
or interaction rule enters this renderer path. See
[D3 contract and verification](LIVE_BRUSH_ENTITIES_M473D3.md) for coordinates,
texture states, visibility fallback, lifecycle and offline limits.

## Optional owned test-server health condition

The manual launcher's optional `-TestStartHealth 50` uses an isolated
`test-server-assisted` Metamod-P profile, not client health authority. The
project-owned helper in `test_server/start_health` is a standalone test-server
build, outside all engine/client/core-only dependency graphs. It writes only
public server entvars health once after the first completed, uniquely identified
local player's spawn. The application has a read-only fresh-clientdata readiness
gate, while HalfLifeClientModule/HUD continue using ordinary server observations.
Runner-owned exact restoration removes the run-scoped profile and restores
liblist; normal launches never enable it. It is not unmodified stock, damage
proof or universal mod support. See [the bounded profile contract and offline
handoff](OPTIONAL_50HP_TEST_START_D2.md).

## Purpose and constraints

`hl-client-engine` is a standalone, independently authored client whose first
compatibility target is the original GoldSrc Half-Life network ecosystem. The
same core should later support an in-process `hl.exe` injection bridge without
turning the renderer, simulation, or asset code into two separate engines.

The reference ABI remains Windows x86. Platform size assumptions must be made
explicit at serialization and ABI boundaries; wire data must never be decoded
by casting packet bytes to host structs.

## G1 current engine/game ownership

The E32 remote-player smoothing follow-up keeps the sole renderer/scene and
the existing runtime snapshot interpolator. An owning player presentation
history retains at most16 immutable committed observations and32 continuity
entries. Its monotonic application-to-server clock fills a selected <=100ms
reserve without restarting on ordinary packet arrivals. Current-list/visibility
authority remains latest; the concrete module owns life/discontinuity decisions,
while exact selected committed brackets feed its Studio/gait/attachment path.
No player velocity, input or physics is guessed. The existing ordinary kinetic
buffer retains its separate verified birth/cadence rules, sharing immutable
observation owners rather than copying assets or introducing another renderer.
Source socket freshness remains distinct from player render sampling. A delayed
old p-model cannot supply an incompatible new-weapon socket; ordinary server
event fallback remains available. See the2026-10-13 follow-up in
`REMOTE_PLAYER_VISUAL_ANIMATION_E10.md` for bounds and acceptance.

The E32 railing follow-up keeps animation-only same-time publications out of
the position continuity floor. Real same-time position corrections still hold
exactly; newer same-position anchors at an existing held boundary retain their
true source identity. The Half-Life module defers discrete main/gait changes
until the selected player endpoint and samples moving gait yaw across the
committed interval, after backwards-facing compensation. No collision,
locomotion prediction, server state or extrapolated player path is introduced.

For a continuous delayed looping main sequence, the Half-Life module selects
the preceding committed animation clock until the newer anchor's animtime.
The existing raw frame/rate evaluator still owns pause, reverse and loop wrap;
byte-frame blending and a new animation timeline are not introduced. New-clock
corrections/restarts remain exact, and nonloop/death, gait, transition, p-model
and lifetime policies remain independent. See the E32 looping-clock follow-up
in `REMOTE_PLAYER_VISUAL_ANIMATION_E10.md`.

D2 Use selects a narrow HL1 input/reference movement profile through the same
module policy: physical E -> typed Use -> immutable usercmd/history -> Netchan.
Shared input has a policy-selected capture/life action gate; the movement
kernel accepts a simulation-local ground button speed cap before duck scaling.
Neither mechanism selects interaction objects or changes HP/armor. Absolute
Health/Battery and bounded observation-window semantics remain game-owned.
See [D2 contract](HALFLIFE_USE_INTERACTION_M473D2.md) for profile bounds,
diagnostics, offline verification and the separate pending manual test.

The normal executable now composes one `GameClientHost` with a static
`HalfLifeClientModule` through `IGameClientModule`. Actual existing game body
interpretation, inventory/weapon rules, B1 presentation, HUD composition,
camera eligibility/recoil, C life transitions and live input/movement policy
selection reside under `include/hlclient/games/halflife/` and
`src/games/halflife/`. The host retains its single pump, clock, driver,
scheduler, RX/TX, scene and renderer; no second engine loop is introduced.

`hlclient_game_halflife -> hlclient_game_api` and
`hlclient_game_client_host -> hlclient_game_api` are separate dependency
branches joined only by executable/test composition. Shared helpers used by
the concrete module are pure lower mechanisms; lower engine targets cannot
link or include the Half-Life module, even through aliases or intermediate
targets. Configure checks the actual graph and source/include closure.
`HLCLIENT_BUILD_GAME_HALFLIFE=OFF` excludes its target and object files while
retaining `hlclient_engine_core` and `hlclient_core_api_tests`.

Game-message views borrow the exact bounded decoded body only during staging.
The module returns owning state; successful decoder/world validation precedes
the existing single commit boundary. Malformed suffixes publish no partial
game effects. Initial/reset staging uses no previous game state and resets
the module only after validation succeeds. Repeated frames and movement replay
do not resubmit weapon actions. Current network grammar and canonical hashes
are unchanged.

The subsequent visual-tuning profile adds neutral transient glow parameters.
`WorldImpactPresentation` emits tracer-head lights at the computed moving
front; `ImpactParticlePool` emits one short contact flash/light per accepted
burst and bounded emissive sparks. Half-Life owns colors/density/spread/
durations. The core pool stays 32 bursts, 128 identities and 256 output
particles (16 per burst with explicit output saturation). `RenderScene`
adds a frame-owned eight-light batch, separate from the existing primary
muzzle light. Shared GL upload and world/brush/Studio shaders consume it;
camera-local first-person draws use an empty world batch. Remote muzzle
flashes retain submission priority over the added secondary flashes.
No new assets, game logic, wire grammar or physics enters the renderer.
These lights use the existing unshadowed point-light mechanism, not bloom
or a shadow system. See the E15 follow-up for actual profile values/bounds.

The asset provider materializes neutral model/sequence/body/frame requests;
OpenGL draws module-composed bounded HUD primitives. Camera geometry and H3/H4
physics/replay remain shared, with actual policy supplied by the module.
The original `IClientSceneSource -> ClientWorldState -> RenderScene -> IRenderer`
path remains the sole scene path. Later milestone sections below describe
their original stage boundaries; the current ownership, precise limitations,
transaction/lifetime contract, build commands and verification record are in
[GameClientAPI and Half-Life module](GAME_CLIENT_MODULE_G1.md).

## D1 server-authoritative pickups

The Half-Life module now stages AmmoPickup/WeapPickup/ItemPickup as bounded
owning presentation events, independently of absolute AmmoX/Health/Battery,
CurWeapon and clientdata ownership. GameClientHost validates event bounds and
exact decoded-message provenance before the existing single atomic commit.
The module's fixed four-row, five-second presentation-clock history never
writes counters, recreates actions or infers pickups from world visibility.
Per-slot reserve/clip message sources are retained without changing the
existing canonical/value hash profiles. Generic entity effects/full/delta
reconstruction and imported/GPU asset reuse remain shared.
See [D1 contracts and handoff](HALFLIFE_PICKUPS_INVENTORY_M473D1.md).

## Non-negotiable data flow

```text
GoldSrc network source --\
                         +-> validated CPU assets -> WorldRenderPackage
hl.exe bridge source ----/                              |
test/offline source ------------------------------------+-> WorldSceneRenderPackage
                                                           + visibility/draw list
                                                                    |
                                                            ClientWorldState
                                                                    |
                                                               RenderScene
                                                                               |
                                                                         IRenderer
                                                                        /         \
                                                                    OpenGL       Null
```

Each arrow is a translation boundary:

1. `hlclient_network` receives bytes without interpreting GoldSrc semantics.
2. `hlclient_goldsrc` bounds-checks and decodes those bytes into protocol values.
3. Format-specific importers produce validated owning CPU assets; the pure
   builder translates complete world assets to an immutable neutral package.
4. A concrete `IClientSceneSource` applies accepted input or a shared package
   to `ClientWorldState`, an engine-owned representation of client-visible
   state.
5. `hlclient_scene_api` derives an immutable or frame-local `RenderScene`.
6. An `IRenderer` backend consumes only `RenderScene` and renderer resources.

This is an invariant, not a suggested layering style. In particular:

- renderer targets must not include GoldSrc packet or network headers;
- render code must not open sockets or drive connection state;
- network and GoldSrc code must not issue OpenGL calls;
- SDK structs must not leak across public module APIs;
- test providers, replay readers, standalone networking, and the future bridge
  must feed the same neutral world-state path;
- protocol decoding must be length-bounded and explicit about endianness.

## Providers and consumers

The target design allows several state providers without contaminating the
consumer side:

```text
GoldSrcNetworkSceneSource --\
ReplaySceneSource -----------+-> ClientWorldState -> RenderScene -> IRenderer
TestSceneSource -------------+
HlInjectionSceneSource -----/
```

The current standalone provider owns the M1 challenge, M2.1 one-shot connect,
M2.2 bounded connectionless response, and the same-socket netchan bootstrap
composition. M2.3.2 adds persistent reliable state; M2.3.3 adds a strict
fragment codec, bounded normal reassembly, deterministic outgoing
fragmentation, and a reusable transport-facing driver without interpreting
opaque bytes. M2.4.1 reuses that exact driver for the one captured client
request, strict in-memory service-envelope decoding, confirmed simple early
messages, and a typed stop before the first complex body. M2.4.2 continues that
exact owning payload through a strict typed server-info body, one confirmed
neutral control, and a category-C stop before the next complex body. The
M2.4.3 continuation decodes the exact ordered delta-schema registry, and M2.4.4
decodes the confirmed opcode-44 movement/environment metadata plus only the
confirmed simple controls before an exact opcode-13 boundary. M3.1.1 decodes
the bounded opcode-13 sequence to exact first-batch end, then optionally queues
the one fixed transition request, validates its reliable ACK lifecycle,
decodes the fixed opcode-45 control, and stops at a neutral body-unconsumed
opcode-43 boundary. M3.1.2 optionally continues from that exact retained
cursor, decodes the confirmed LSB-first standard `ResourceListMessage` into
ordered owning metadata, validates exact end-of-payload, and stops at a
metadata-only required-response boundary without TX or filesystem access. The
M3.1.3 continuation then separates descriptor/41-byte semantic/tail geometry,
uses the neutral `Opcode5ResourceResponse` codec, obtains private local-derived
fields only through `hlclient_resource_consistency_api`, queues semantics once
through the retained driver, and stops at the first opcode of the next complete
server payload. M3.2.1 adds an opt-in, pre-network
`PreparedLocalResourceConsistencyProvider`, a handle-sandboxed local resolver,
an evidence-gated GoldSrc name mapper, and an ordered metadata-only inventory.
M3.2.2 retains that same validated local environment after the response, strictly
correlates inventory/list metadata, selects the exact ServerInfo map entry, and
publishes an immutable metadata-only manifest with type-local sparse slots. The
M3.2.3 continuation opens only the selected world locator through the retained
environment, validates a bounded same-handle byte source, and runs pure,
deterministic importer selection before any parser or renderer boundary. M4.1
composes the filesystem-free `hlclient_goldsrc_bsp` importer after that source
boundary, validates BSP v30 transactionally, and publishes neutral owning CPU
world geometry for model 0 without texture pixels or renderer resources. M4.2
retains the same approved BSP bytes, extracts only used physical texture
records, decodes embedded indexed miptex data, parses inert first-entity WAD
metadata to safe basenames, opens required WAD3 sources through the retained
local environment, and publishes a neutral owning texture set with exact
material bindings. Missing assets may yield a typed incomplete set; malformed
bytes fail transactionally. M4.3 then decodes the retained BSP RGB lighting
lump into deterministic four-layer padded atlases and transfers the complete
textured world plus lightmaps into an immutable renderer-neutral
`WorldRenderPackage`. Its CPU-only stage finalizes the retained network and
authentication lifetime once; only afterward may a bounds-derived local scene
upload the package to OpenGL. M4.4 consumes the same canonical BSP document to
build an owning world-spatial/PVS package, optional static initial brush
resources and instances, and a renderer-neutral scene package. Per-frame CPU
PVS/frustum resolution publishes only indices, commands, bounded statistics,
and a visibility revision; it does not alter scene resource identity or return
to parser/network/filesystem layers. The inert entity document is parsed at
most once for requested brush/spawn metadata and is not retained by the scene.
M4.4.1 corrects the earlier synthetic-only geometry assumption inside the BSP
CPU boundary: one `GoldSrcFaceGeometryBuilder` now validates the evidenced
Valve qbsp clockwise wire, performs bounded collinear T-junction cleanup, and
publishes canonical counter-clockwise faces for both world and brush models.
No texture, lightmap, scene, visibility, or renderer component has an alternate
winding path. No post-manifest semantic packet or runtime entity behavior is
added. The sign-on target still sees only the path-free provider API.
Custom/player-resource
grammar, server-info second-client slot evidence, snapshots, movement
application, and commands remain future increments behind the same boundaries.
The future bridge adapts
observed or exported in-process state to the same project types. It must not
make renderer behavior depend on injected addresses or Valve private layouts.

## Target responsibilities

| Target | Responsibility | Must not own |
| --- | --- | --- |
| `hlclient_core` | logging, command-line parsing, fundamental utilities, version | SDL, sockets, protocol parsing |
| `hlclient_game_api` | project-owned game/session contracts, borrowed record views, owning game/presentation values | concrete game/host headers, native handles, SDK ABI structures |
| `hlclient_game_client_host` | session-owned module lifetime and typed dispatch | concrete Half-Life rules, transport, second application loop |
| `hlclient_game_policy` | validation of explicit input/movement policy using shared mechanisms | choosing Half-Life defaults, server MoveVars fabrication |
| `hlclient_game_halflife` | existing Half-Life messages, inventory, weapon/HUD/view/camera presentation, lifecycle and policy selection | sockets, SDL/GL handles, renderer calls, scene/asset host, new physics kernel |
| `hlclient_input_api` | bounded physical keyboard/mouse events, immutable frame snapshots, state tracking, and null/scripted sources | SDL/native handles, filesystem, commands, network, renderer state |
| `hlclient_platform_sdl_input` | private SDL3 scancode/button/window-event translation for the sole platform pump | gameplay actions, client/world state, renderer state |
| `hlclient_platform` | SDL lifetime, the sole ordered platform event pump, windows, relative-mouse capture, GL context, and clocks | game protocol, gameplay bindings, or world state |
| `hlclient_gameplay_input` | immutable project-owned physical bindings and keyboard/mouse-to-intent sampling | SDL, command strings, stock button masks, `usercmd`, network |
| `hlclient_filesystem` | safe base/game path discovery and asset-facing I/O foundations | Steam discovery policy embedded in render code |
| `hlclient_network` | address values, Winsock lifetime, nonblocking datagram transport | GoldSrc message meaning |
| `hlclient_goldsrc_byte_io` | protocol-neutral bounded GoldSrc byte reader/writer primitives | sockets, transport/session state, files, renderer |
| `hlclient_goldsrc` | connectionless codecs, strict info strings, and opaque auth value, layered on the byte-I/O primitive | retries, files, logging, OpenGL, UI |
| `hlclient_goldsrc_netchan` | netchan classifier/base/fragment codec, payload transform, wrap-safe persistent session, bounded pending plus one reliable unit in flight, transactional unfragmented/fragment sends, filesystem-free slot-0 normal reassembly, bounded same-transport driver, owning events, metadata-only traces, and first-ACK compatibility primitive | transport creation/closure, authentication semantics or bytes, slot-1/file interpretation, decompression, files, `svc_*`, world/render state |
| `hlclient_hash_md5` | project-owned incremental MD5 required only for GoldSrc compatibility material | filesystem, external crypto libraries, trust/security policy, GoldSrc wire types |
| `hlclient_local_resources` | explicit validated local environment/search roots, byte-exact virtual names, path-safe locators, exact-root verified reopen, Win32 read-only handle sandbox, final-handle containment, stable equality-only identity, bounded resolution and streaming inspection | sockets, server messages, downloads, cache/assets, renderer |
| `hlclient_resource_consistency_api` | path-free bounded provider requirements, move-only asynchronous operation/session/material ownership, cancellation, and private opaque-material handoff | filesystem/path policy, local lookup, checksum calculation, sockets, GoldSrc list types, assets, renderer |
| `hlclient_resource_consistency_local` | pre-network preparation of the fixed `tempdecal.wad` compatibility target and one-shot nonblocking provider operation | server-derived paths, response codec/layout, network creation, writes, downloads, cache, assets |
| `hlclient_goldsrc_delta_schema` | socket-free LSB bit reader, delta descriptor parser, immutable schema metadata, and bounded schema registry | Netchan/session state, sign-on stages, sockets, resources, filesystem, renderer |
| `hlclient_goldsrc_move_vars_api` | immutable validated MoveVars state/codec and post-delta boundary value, layered only on byte I/O and delta schema | sign-on lifecycle, Netchan/session state, sockets, movement simulation, renderer |
| `hlclient_goldsrc_signon` | exact fixed initial/transition requests, strict `BZ2\0` decoding, owning immutable sign-on/list/response states, historical neutral opcode-43 and zero-TX resource-list stop, exact standard list and neutral 41-byte opcode-5 codecs, carrier/tail separation, provider-required response stage, same-driver semantic-once lifecycle, and next-payload opcode boundary | arbitrary commands, custom/player-resource bodies, production consistency material, resource resolution, runtime application, command execution, filesystem, renderer, SDL, assets, world state |
| `hlclient_goldsrc_delta_values` | immutable schema-aligned generic values, bounded transactional synthetic-neutral runtime-mask/scalar mechanics, and fail-before-read stock evidence boundary | schema reparsing, native/HLSDK struct writes, stock mask claims, wall clock, filesystem, assets, renderer |
| `hlclient_goldsrc_usercmd_api` | immutable fixed-width usercmd semantic state, validated compatibility/input/schema profiles, bounded limits, project-local command identity, and pure duration quantization | delta schema parsing, wire bytes, sockets, input polling, renderer, prediction/collision |
| `hlclient_goldsrc_usercmd_codec` | exact 15-field descriptor binding plus synthetic-only bit writer, delta codec, client-move envelope, and sequence-aware checksum | stock wire/runtime claims, native `usercmd_t` layout, history/scheduling, input mapping, sockets, filesystem, SDL/rendering |
| `hlclient_goldsrc_usercmd_sampling` | network-free synthetic input/camera adaptation and fixed-step command scheduling | history, packet planning/transmission, sockets, stock compatibility claims, prediction/collision/reconciliation |
| `hlclient_goldsrc_usercmd_session` | bounded history/backup planning and transactional same-driver unreliable transmission through a sequence-bound context, layered on usercmd sampling and codec targets | caller sequence overrides, second sockets/sessions, reliable usercmd workaround, stock compatibility claims, prediction/collision/reconciliation |
| `hlclient_goldsrc_entity_snapshots` | immutable generic baseline/full/delta/history state, explicit add/remove and exact-base mechanics, strict ordering, bounded retention, and sealed synthetic-neutral builders | stock entity wire/opcode claims, clientdata prediction, model binding, filesystem, renderer |
| `hlclient_prediction_api` | immutable prediction session, authority, history, and comparison values with bounded publication and typed stock evidence gates | collision execution, input, replay, renderer, filesystem, network |
| `hlclient_local_prediction` | pure session signatures, independent synthetic authority, strict reconciliation, and bounded replay through the existing movement kernel | input resampling, effects, packet resend, renderer, filesystem |
| `hlclient_prediction_visual` | time-based collision-constrained camera-only residual correction | movement-state smoothing, authority inference, input/network/GPU ownership |
| `hlclient_goldsrc_post_resource_signon` | exact unconsumed post-response cursor, bounded metadata transcript, typed request evidence gate, private same-driver/source-payload continuation, and a sealed four-control-fixture synthetic stage that publishes typed baseline/full/delta state for fake-HLDS tests | arbitrary commands/raw injection, stock request invention, stock entity-body decoding without evidence, opcode scanning, stufftext execution, assets, entity rendering |
| `hlclient_goldsrc_local_resources` | evidence-gated resource-type/name classification and ordered metadata-only `LocalResourceInventoryState` adapter | sign-on transport, readiness/precache decisions, downloads/cache, file contents, asset loading |
| `hlclient_goldsrc_resource_readiness` | strict list/inventory correlation, per-entry readiness and aggregate impact, exact map selection, immutable ordered manifest, and bounded type-local sparse slots | downloads/cache, file-content parsing, assets, renderer, OpenGL |
| `hlclient_local_asset_source` | incremental exact-root locator reopen, bounded same-handle read, exact EOF and final-snapshot validation, owning `AssetSource` publication | GoldSrc types, importer selection, path fallback, renderer, network |
| `hlclient_asset_dispatch` | pointer-free pure importer probes, deterministic typed/cross-category selection, and owning imported-asset result | filesystem, GoldSrc protocol, network, renderer, OpenGL |
| `hlclient_goldsrc_approved_asset_source_api` | network-free owning read-only capability shared by manifest approval and offline visual composition | source approval policy, network/sign-on stages, importer selection, renderer |
| `hlclient_model_asset_api` | format-neutral owning skeletal model metadata layered additively over the legacy model asset | GoldSrc wire names, playback state, entity binding, renderer/GPU state |
| `hlclient_goldsrc_indexed_texture` | shared strict miptex grammar, four indexed mip/palette ranges, incremental RGBA8 conversion, masked-index-255 alpha, and neutral texture assets | BSP/WAD container parsing, filesystem, network, SDL, OpenGL, renderer/GPU work |
| `hlclient_goldsrc_studio_model` | strict CPU-only IDST/IDSQ v10 parsing, skeletal geometry/textures/animations, and deterministic importer probing | filesystem/environment access, entity binding, playback, renderer/GPU work, network |
| `hlclient_goldsrc_sprite` | strict CPU-only IDSP v2 palette/frame/group parsing and owning indexed/RGBA sprite import | filesystem/environment access, billboarding/blending, renderer/GPU work, network |
| `hlclient_goldsrc_bsp` | strict BSP v30 byte grammar, structural validation, shared world/submodel `GoldSrcFaceGeometryBuilder`, evidenced qbsp-clockwise to renderer-counter-clockwise conversion, bounded collinear compatibility and diagnostics, exact texture ordinals, owning spatial-source records/entity bytes, used physical texture-source ranges, canonical inert entity parsing, and owning CPU assets | filesystem/local-resource APIs, WAD opening/catalogs, network/sign-on, `AssetManager`, SDL, OpenGL, renderer/GPU work |
| `hlclient_goldsrc_builtin_importers` | the single caller-owned production composition that registers BSP, Studio, and sprite importers exactly once | parser implementation, global registries/state, filesystem, network/sign-on, renderer/GPU work |
| `hlclient_goldsrc_wad3` | strict bounded WAD3 header/directory catalog, uncompressed type-`0x43` lookup, normalized duplicate rejection, and shared miptex adapter | filesystem/local-resource resolution, compiler-path interpretation, network, SDL, OpenGL, renderer/GPU work |
| `hlclient_goldsrc_asset_dispatch` | evidence-derived resource role/plan, approved source facade, selected-world manifest continuation, and same-session terminal dispatch state | format parsing, download/cache, renderer/GPU work, extra network messages |
| `hlclient_goldsrc_visual_asset_bundle` | canonical model-or-sprite dispatch plus transactional exact-root Studio companion composition for approved or verified local sources | network/sign-on stages, native-path input, renderer/GPU work, entity/gameplay state |
| `hlclient_entity_interpolation` | pure bounded snapshot-pair selection and renderer-neutral synthetic projection interpolation | visual-asset import/readiness, local resources, filesystem, renderer/GPU work |
| `hlclient_entity_interpolation_stage` | client-side ownership and sequencing from an immutable visual-asset stage result through pure interpolation to a coherently paired renderer-neutral entity frame | renderer/GPU calls, network mutation, filesystem I/O |
| `hlclient_goldsrc_world_texture_import` | network-free retained-BSP texture resolution, sandboxed declared-WAD source opening, incremental decode, exact material bindings, and immutable complete/incomplete texture sets | sign-on/stage state, sockets, downloads/cache, entity instantiation, lightmaps, texture effects/animation, `AssetManager`, SDL, OpenGL, renderer/GPU work |
| `hlclient_goldsrc_world_textures` | same-session terminal stage that composes asset dispatch with the CPU-only world-texture import target | texture codec duplication, SDL, OpenGL, renderer/GPU work, extra network messages after the manifest boundary |
| `hlclient_goldsrc_lightmaps` | exact face-local GoldSrc lightmap extents, RGB/style-slot decode, deterministic padded multi-page atlases, and immutable per-surface bindings | filesystem, network, SDL, OpenGL, gamma/overbright, dynamic lighting |
| `hlclient_goldsrc_world_render` | same-session texture/lightmap/package stage, bounded events and terminal publication, and exactly-once retained network/authentication cleanup | new semantic TX, SDL/OpenGL initialization, package drawing, gameplay continuation |
| `hlclient_world_render_api` | renderer-neutral world vertices, materials, batches, coordinate metadata, and immutable package contract | GoldSrc/network types, SDL, OpenGL handles, filesystem |
| `hlclient_world_render_package` | typed renderable-texture/lightmap validation, checked UV/material/batch construction, package limits, and stable renderer resource identity/revision | parsers, native paths, SDL/OpenGL upload, network state |
| `hlclient_world_spatial` | immutable planes/nodes/leaves/PVS table, marksurface membership, and bounded point/AABB queries | BSP bytes, entity interpretation, renderer/GPU work, filesystem, network |
| `hlclient_goldsrc_spatial` | canonical BSP-record adaptation, strict bounded GoldSrc PVS RLE decode/deduplication, leaf-zero and marksurface mapping | independent BSP wire parsing, renderer calls, paths, network |
| `hlclient_world_visibility` | OpenGL-convention frustum extraction, explicit PVS/fallback resolution, exact surface/instance selection, and stable draw-list construction | BSP/PVS byte parsing, entity behavior, filesystem, network, GPU resources |
| `hlclient_world_scene_renderer` | immutable world/spatial/brush scene composition, renderer adapters, bounds/statistics, and stable resource identity/revision | paths, raw BSP/PVS/entity bytes, OpenGL handles, network state |
| `hlclient_goldsrc_brush_models` | inert bounded entity metadata, qcsg/AngleMatrix initial transforms, brush instances, shared texture/lightmap render library, spawn descriptor, and scene composition | runtime entity behavior, commands/touch/use/think, snapshots, translucent rendering, GPU calls |
| `hlclient_world_preview` | deterministic historical static/orbit/inert-spawn cameras plus externally supplied interactive cameras, per-frame visibility/draw-list composition, and neutral scene-source state | input polling, gameplay intent, usercmds, runtime entity updates, network mutation, GPU calls |
| `hlclient_auth` | asynchronous provider/operation contract and move-only authentication session lifetime | file policy, Steam implementation, sockets, renderer, world state |
| `hlclient_app_support` | explicit user-file auth adapter, optional dynamically loaded legacy Steam auth adapter, bounded local-file loading, and metadata-manifest CLI exit policy | implicit discovery, caching, provider fallback search, protocol parsing |
| `hlclient_goldsrc_client` | challenge/connect coordination, same-socket bootstrap/sign-on/resource composition, selected manifest/asset/world-texture/render-package/spatial-scene continuations, and driver/auth/provider lifetime ownership through the selected terminal stop | auth or consistency-material generation, wire codec duplication, arbitrary reliable payload production, native path/handle policy, runtime application, OpenGL, SDL, render state |
| `hlclient_client` | connection-independent client world and presentation state | raw socket ownership, GL resources |
| `hlclient_gameplay_camera` | immutable Z-up local free-flight/entity-attached camera state and bounded renderer-independent controller updates | SDL, renderer types, collision/physics/prediction, stock view-angle claims, network |
| `hlclient_gameplay_camera_render_adapter` | narrow validated conversion from neutral gameplay camera state to `RenderCamera` | input polling, gameplay control, network, renderer mutation/GPU work |
| `hlclient_interactive_preview` | input-intent/camera coordination, explicit synthetic entity anchoring, camera-dependent entity visibility refiltering, and transactional `ClientWorldState` publication | SDL polling, asset loading, renderer/GPU mutation, network |
| `hlclient_asset_api` | owning asset sources, neutral CPU geometry/textures/bindings, typed importer and registry contracts | filesystem I/O, SDL, OpenGL, sockets, SDK types |
| `hlclient_asset_manager` | virtual-file reads and dispatch through typed registries | format parsing, renderer resources, caches |
| `hlclient_scene_api` | scene-source contract and world-state-to-render-scene conversion | concrete network/injection providers, graphics calls |
| `hlclient_renderer_api` | neutral render scene and renderer contract | SDL or GoldSrc headers in its public API |
| `hlclient_renderer_opengl` | OpenGL 3.3 Core world/brush scene cache/upload, built-in shaders, camera/model matrices, depth/masked visible-command drawing, historical full-world fallback, and RAII GPU ownership using GLAD | packet/format/PVS parsing, filesystem lookup, client connection state, entity/gameplay logic |
| `hlclient_renderer_null` | headless renderer lifecycle and frame statistics | SDL, OpenGL, GLAD, Winsock, SDK types |
| `hlclient_local_resource_check` | network-free, read-only diagnostic composition for an explicit user-owned root | stock process launch, path/digest output, file mutation, protocol transport |
| `hlclient_bsp_compat_check` | network-free, read-only production BSP geometry/texture/render-package/spatial-scene validation with bounded aggregate compatibility diagnostics | network, SDL/OpenGL, writes, native-path or raw-asset output, gameplay |
| `hlclient_world_texture_check` | network-free, read-only BSP/WAD texture composition for one explicit safe virtual map | stock process launch, writes, downloads/cache, renderer/GPU work, native-path or asset-byte output |
| `hlclient_goldsrc_asset_check` | network-free, read-only canonical Studio/SPR composition for one safe virtual asset with bounded aggregate diagnostics | network/stage libraries, writes, native-path or raw-asset output, rendering/entity behavior |
| `hlclient_usercmd_check` | deterministic offline construction, encode/decode, and summary of named synthetic usercmd scenarios | stock evidence claims, sockets/server contact, arbitrary raw fields, runtime stage mutation |
| `hlclient_prediction_check` | read-only real/synthetic-BSP prediction campaigns with metadata-only deterministic hashes | sockets, raw state CLI, game writes, stock compatibility claims |
| `hlclient_prediction_viewer` | separate offline player-walk renderer composed with synthetic authority and corrected camera samples | production fake server/opcodes, arbitrary authority fields, stock compatibility claims |
| `hlclient_world_viewer` | network-free, read-only BSP/WAD/lightmap/spatial/scene composition and configurable local diagnostic OpenGL preview, including free-fly input, for one safe virtual map | stock/network process launch, writes, downloads/cache, native map-path input, runtime gameplay simulation |
| `hlclient_entity_viewer` | network-free, read-only mixed Studio/Sprite playback with historical cameras or a local synthetic entity-first-person camera | live snapshots, stock player semantics, commands, writes, network control |
| `hlclient` | composition root and frame loop | reusable subsystem implementation |
| `hlclient_tests` | deterministic unit/integration tests with local resources | public Internet or installed-game requirements |

Public dependencies should point inward toward stable project-owned contracts.
Private implementation dependencies may point outward to SDL, GLAD, OpenGL,
Winsock, or SDK headers without exposing them to unrelated consumers.

The M4.4/M4.4.1 target direction remains deliberately acyclic:

```text
hlclient_hash_md5 -> hlclient_core
hlclient_local_resources -> hlclient_core + private Win32 APIs
hlclient_resource_consistency_local
    -> hlclient_resource_consistency_api
    -> hlclient_local_resources
    -> hlclient_hash_md5
hlclient_goldsrc_local_resources
    -> hlclient_goldsrc_signon
    -> hlclient_local_resources
hlclient_goldsrc_resource_readiness
    -> hlclient_goldsrc_signon
    -> hlclient_goldsrc_local_resources
    -> hlclient_local_resources
hlclient_local_asset_source
    -> hlclient_asset_api
    -> hlclient_local_resources
hlclient_goldsrc_approved_asset_source_api
    -> hlclient_asset_api
    -> hlclient_asset_dispatch
    -> hlclient_local_asset_source
    -> hlclient_local_resources
hlclient_model_asset_api -> hlclient_asset_api -> hlclient_core
hlclient_goldsrc_studio_model
    -> hlclient_model_asset_api
    -> hlclient_asset_api
    -> hlclient_core
hlclient_goldsrc_sprite -> hlclient_asset_api -> hlclient_core
hlclient_goldsrc_indexed_texture -> hlclient_asset_api -> hlclient_core
hlclient_goldsrc_bsp
    -> hlclient_asset_api
    -> hlclient_goldsrc_indexed_texture
    -> hlclient_goldsrc_spatial
hlclient_goldsrc_builtin_importers
    -> hlclient_goldsrc_bsp
    -> hlclient_goldsrc_studio_model
    -> hlclient_goldsrc_sprite
hlclient_goldsrc_wad3
    -> hlclient_asset_api
    -> hlclient_goldsrc_indexed_texture
hlclient_asset_dispatch -> hlclient_asset_api
hlclient_goldsrc_asset_dispatch
    -> hlclient_goldsrc_approved_asset_source_api
    -> hlclient_goldsrc_resource_readiness
    -> hlclient_local_asset_source
    -> hlclient_asset_dispatch
hlclient_goldsrc_visual_asset_bundle
    -> hlclient_goldsrc_studio_model
    -> hlclient_goldsrc_sprite
    -> hlclient_goldsrc_approved_asset_source_api
    -> hlclient_local_asset_source
    -> hlclient_local_resources
    -> hlclient_asset_dispatch
hlclient_goldsrc_asset_check
    -> hlclient_goldsrc_builtin_importers
    -> hlclient_goldsrc_visual_asset_bundle
    -> hlclient_local_asset_source
    -> hlclient_local_resources
hlclient_goldsrc_signon -> hlclient_resource_consistency_api
hlclient_goldsrc_world_texture_import
    -> hlclient_goldsrc_bsp
    -> hlclient_goldsrc_wad3
    -> hlclient_local_asset_source
hlclient_goldsrc_world_textures
    -> hlclient_goldsrc_asset_dispatch
    -> hlclient_goldsrc_world_texture_import
hlclient_goldsrc_lightmaps
    -> hlclient_asset_api
hlclient_goldsrc_world_render
    -> hlclient_goldsrc_world_textures
    -> hlclient_goldsrc_lightmaps
    -> hlclient_world_render_package
hlclient_world_render_package
    -> hlclient_world_render_api
    -> hlclient_asset_api
hlclient_world_spatial -> hlclient_asset_api
hlclient_goldsrc_spatial
    -> hlclient_asset_api
    -> hlclient_world_spatial
hlclient_world_visibility
    -> hlclient_world_spatial
    -> hlclient_world_render_api
    -> hlclient_renderer_api
hlclient_world_scene_renderer
    -> hlclient_world_render_api
    -> hlclient_world_spatial
    -> hlclient_world_visibility
hlclient_goldsrc_brush_models
    -> hlclient_goldsrc_bsp
    -> hlclient_goldsrc_world_texture_import
    -> hlclient_goldsrc_lightmaps
    -> hlclient_world_render_package
    -> hlclient_world_spatial
    -> hlclient_world_scene_renderer
hlclient_world_preview
    -> hlclient_client
    -> hlclient_scene_api
    -> hlclient_world_scene_renderer
    -> hlclient_world_visibility
hlclient_renderer_opengl
    -> hlclient_renderer_api
    -> hlclient_world_render_api
    -> hlclient_world_scene_renderer
    -> hlclient_world_visibility
    -> hlclient_glad + OpenGL::GL
```

The BSP parser target remains independent of Studio and sprite. The separate
`hlclient_goldsrc_builtin_importers` composition target owns the one canonical
production registration helper, so world-only BSP consumers do not inherit
the visual importer libraries.

The offline `hlclient_world_texture_check` and `hlclient_world_viewer`
composition roots link `hlclient_goldsrc_world_texture_import`, never the
same-session `hlclient_goldsrc_world_textures` stage. The offline
`hlclient_goldsrc_asset_check` similarly links the canonical builtin registrar,
network-free visual bundle, and approved-source API seam, never
`hlclient_goldsrc_asset_dispatch`. Their
generated link closures therefore exclude `hlclient_network`, Winsock,
netchan, sign-on, resource-transition, readiness, and asset-dispatch stage
libraries. The lightmap target
likewise links only `hlclient_asset_api`; its private implementation reads the
clean-room inline BSP v30 header constants and does not acquire the BSP parser
library or any network-bearing target.

The corresponding Visual Studio folders include `Engine/Core/Hash`,
`Engine/Resources/Local`, `Engine/Resource Consistency`,
`Engine/Assets/Source`, `Engine/Assets/Dispatch`, `Engine/Assets/Models`,
`Engine/Assets/Formats/GoldSrc`, `Engine/Assets/World Textures`,
`Engine/Assets/World Render`, `Engine/Renderer/World`,
`Engine/Renderer/Visibility`, and
`Engine/Resources/GoldSrc`; offline asset diagnostics belong under
`Tools/Assets`, and the graphical diagnostic belongs under `Tools/World`.
In particular, `hlclient_goldsrc_signon` does not acquire a filesystem or
concrete local-provider dependency.

## Platform boundary

SDL3 is the owner of:

- process-level SDL initialization and shutdown;
- window creation and destruction;
- event polling and input normalization;
- OpenGL context creation, swap interval, and buffer presentation;
- portable timing needed by the client loop.

Native APIs are allowed only where SDL does not provide the required contract
or interoperability demands it. Current examples are Winsock2 behind
`hlclient_network` and, later, module loading/injection primitives behind a
dedicated Windows bridge target. Include Windows headers in implementation
files or narrowly scoped private headers, with `WIN32_LEAN_AND_MEAN` and
`NOMINMAX` where appropriate.

## Renderer boundary

`hlclient_renderer_api` owns the backend-neutral `RenderScene` and `Renderer`
contract. `hlclient_renderer_opengl` implements that contract with OpenGL 3.3
Core and the committed GLAD2 loader. `hlclient_renderer_null` implements the
same contract without SDL, a window, a graphics context, or a GPU. Another
renderer backend must be able to consume the same scene without changing
GoldSrc decoding.

`RenderScene` carries clear color, a validated Z-up camera, and an optional
static package reference plus cull/baseline-style policy. M4.4 may additionally
carry an immutable scene package and frame-local visible draw list. The
packages contain format-neutral CPU vertices, indices, materials, exact
surface ranges, base mip levels, four-layer lightmap pages, spatial metadata,
and supported static brush resources. Stable scene resource identity/revision
lets one renderer reuse the same world/brush uploads and replace them
transactionally when resources change. Visibility has a separate revision and
changes commands only; there is no global cache or per-frame upload.

The renderer boundary receives only M4.4.1 canonical counter-clockwise
triangles whose standard cross product has a strictly positive margin against
the flat face normal. The BSP layer computes that margin from a
centroid-rebased double area vector with an extent-scaled bounded tolerance;
the renderer does not invert `glFrontFace`, globally disable back-face culling,
flip normals in a shader, or retry a face with another order.

Resource handles crossing this boundary must be project-owned opaque values,
not raw OpenGL names. Asset decoding produces CPU-side formats before a
renderer-specific upload step. The OpenGL backend owns all shader, VAO, buffer,
base-texture, lightmap-array, and white-fallback names through RAII; a failed
partial upload cannot become active. Destruction order preserves the GL context
until all renderer resources are released. See
[world render package](WORLD_RENDER_PACKAGE.md) and
[world visibility](WORLD_VISIBILITY.md),
[brush-submodel rendering](BRUSH_SUBMODEL_RENDERING.md), and
[OpenGL world renderer](OPENGL_WORLD_RENDERER.md).

## Protocol and safety boundary

The E32 tunnel follow-up enables a bounded outer GoldSrc UDP split envelope in
the existing normal-live `NetchanDriver` profile. It retains opaque, still
munged parts only after exact-endpoint filtering, then passes a complete,
validated inner datagram through the unchanged netchan/ACK/service pipeline.
Strict bootstrap/research callers keep their special-packet boundary. This is
transport framing, not another reliable reassembler or application decoder.
Reference movement shares one existing-collision helper for at most one
binary32-step BSP boundary recovery; full-scene clearance is mandatory and
server entity boxes/real penetration are not relaxed. Canonical server seeds
remain owning observations; a derived physical query records its actual origin.

The subsequent uneven-ground fix consumes validated sweep contacts through a
one-ULP outward endpoint only after full-scene occupancy and a clear hull path
from the original start. It does not alter pure BSP query signs/fractions.
Coherent grounded reference seeds may use a narrowly bounded box-boundary
quantization repair (<=1/8 unit): every collider remains present, a reverse
sweep must witness the exact box, and a valid support probe must exist. Raw
clientdata remains canonical. Half-Life explicitly opts into player-schema
SOLID_SLIDEBOX bounds in the existing neutral server-box query; no Studio model
shape, player-intent prediction or local player damage authority is added.

Network input is untrusted. Parsers must:

- consume a bounded byte span through checked readers;
- reject truncated, oversized, inconsistent, or unsupported messages cleanly;
- use fixed-width integer types for wire fields;
- decode byte order explicitly;
- cap strings, resource counts, fragment sizes, decompression output, and
  allocation sizes before allocating;
- keep connectionless and sequenced-channel parsing separate;
- make malformed-packet tests local and deterministic.

Compatibility constants may be checked against official SDK declarations, but
the runtime implementation remains project-owned.

The current application path exposes explicit bounded stops through the
provider-gated post-resource response boundary:

```text
--connect IPv4:port
    -> UdpDatagramTransport
    -> ChallengeExchange
    -> getchallenge request / strict challenge response
       |-> default terminal M1 success
       `-> optional ConnectRequestStage on the same transport/socket
           -> one connect send
              |-> terminal M2.1 success/error
              `-> optional ConnectResponseWaitStage on that transport/socket
                  -> strict connectionless ACCEPT/REJECT
                     |-> terminal M2.2 outcome
                     |-> optional NetchanBootstrapStage on the same socket
                     |   -> coordinator/stage-owned NetchanDriver
                     |   -> one unfragmented or reassembled slot-0 payload
                     |   -> required transport acknowledgement(s)
                     |   -> terminal CLI netchan-bootstrap outcome
                     `-> optional InitialSignonStage on the same socket
                         -> one typed reliable client request
                         -> request ACK and normal-stream reassembly
                         -> strict BZ2-NUL in-memory envelope
                         -> bounded opcode-8 text control
                         |-> terminal opcode-11 boundary; body untouched
                         `-> optional PreResourceSignonStage facade
                             -> exact-cursor typed opcode-11 server-info
                             -> bounded empty-string/zero opcode-54 control
                             -> opcode-14 delta-description boundary
                              `-> optional DeltaDescriptionStage facade
                                  -> seven exact-cursor LSB-first schemas
                                  -> immutable ordered metadata registry
                                  |-> terminal numeric opcode-44 boundary;
                                  |   body untouched for `delta-schemas`
                                  `-> optional MovementEnvironmentStage facade
                                      -> strict typed opcode-44 MoveVars state
                                      -> exact opcodes 32, 5, 39, and 9
                                      -> exact opcode-13 boundary
                                      |-> terminal `movevars`; body untouched
                                      `-> optional UserInfoSignonStage
                                          -> one or more exact opcode-13 messages
                                          -> immutable private-value metadata
                                          -> exact end of first service payload
                                          |-> terminal `user-info`; no TX
                                          `-> optional ResourceTransitionStage
                                              -> one exact typed 9-byte request
                                              -> driver-owned TX/retry/covering ACK
                                              -> bounded later BZ2-NUL transfer
                                              -> opcode 45 + exact 8-byte body
                                              |-> terminal neutral opcode 43;
                                              |   validated but unconsumed;
                                              |   body unread, no reply
                                               `-> optional ResourceListStage
                                                   -> exact opcode-43 cursor
                                                   -> strict LSB-first count/entries
                                                   -> immutable ordered metadata
                                                   -> terminal zero fill + exact EOP
                                                   -> response-required boundary
                                                   |-> terminal `resource-list`;
                                                   |   historical metadata only,
                                                   |   zero queued response/TX/files
                                                   `-> optional ResourceClientResponseStage
                                                       -> descriptor/41-byte/tail split
                                                       -> typed provider requirements
                                                       |-> no selected provider:
                                                       |   `provider_required`, no TX
                                                       `-> prepared local material:
                                                           -> queue semantic bytes once
                                                           -> driver retry/covering ACK
                                                           -> first complete next payload
                                                           -> opcode at offset 0;
                                                              complex body unconsumed
```

The application has an explicit user-file authentication provider and an
optional Steam legacy game-connection provider selected by
`--auth-provider steam --steam-api-runtime <absolute-path>`. Default/offline
modes do not load Steam, and failure never falls back to a file. The local consistency
provider is likewise explicit and is fully prepared before network creation.
Each later terminal stage composes the driver on the already-bound transport
and validates unchanged
local/exact remote endpoints. The sign-on branch queues only the fixed
five-byte initial request. The transition branch queues only the fixed
nine-byte request and owns at most one second payload received before its
covering ACK. The resource-list continuation queues nothing. Each public
earlier stop closes after publishing its own
boundary; friend-only retention carries that same driver/lifetime only through
the selected continuation. The separate response continuation can queue one
typed semantic unit only after a provider succeeds; without explicit local
selection it publishes `provider_required` and sends nothing. No
route exposes received raw reliable/fragment/
delta/movevars/user-info/resource bytes or updates filesystem, world, asset,
or renderer state; the transition request object exposes only its fixed typed
nine-byte wire message. The delta, MoveVars, user-info, transition, and
resource-list states are metadata for future milestones and are not applied to
packets, simulation, player entities, resource lookup, or world memory.

The active stock verifier has no completed restoration-attested M3.1.3 runs,
and therefore no tracked response projection exists. Historical reconstructed
carrier evidence and deterministic fake-HLDS tests justify the bounded API and
test behavior, but are not an active project-client-to-stock response success
claim. M3.2.1 implements the local provider independently of that pending stock
evidence, and M3.2.2 builds only local metadata readiness/manifest state on top
of it. M3.1.4 remains conditional on sufficient evidence for the next complex
server message; M4.1 through M4.3 add only the local
BSP/CPU-world/texture/lightmap/package continuation and do not consume that
later network message or transmit after manifest publication. M4.3 explicitly
releases the retained network/authentication lifetime before any unbounded
diagnostic rendering.

M2.3.3 splits netchan into pure base/fragment wire codecs and transform,
transport-independent persistent reliable state, a transactional normal
reassembler, a reusable transport-facing `NetchanDriver`, and the bootstrap
composition stage. The driver borrows—never creates or closes—the already-bound
`IDatagramTransport`, requires exact endpoints, uses injected monotonic time,
and exposes only const session/reassembler inspection. The bootstrap stage's
post-success mutable session accessor is a narrow compatibility seam; it does
not expose mutable driver/reassembler state or transfer socket ownership.

Project defaults/hard maxima include 4,096/16,384-byte datagrams,
4,088/16,376-byte unfragmented bodies, a 16,376-byte pending/outgoing transfer,
1,024-byte normal fragments, 65,536/1,048,576-byte incoming normal transfers,
64/1,024 fragments and ranges, one active normal transfer, 30/300-second channel
inactivity, 5/30-second fixed fragment deadlines, 8/64 receives and 1/8 sends
per update, and 16/256 queued events with a minimum capacity of five. These are
project safety limits, not stock-engine maxima.

M3.1.1 adds project-only defaults/hard caps of 2,048/8,192 bytes per user-info
message, 1,024/4,096-byte info strings, 64/256-byte keys, 256/4,096-byte values,
64/256 entries, 32/256 messages, 32,768/262,144 total message bytes, and
64/256 user-info stage events. The transition request and opcode-45 message
are fixed at a 9/9-byte default/hard cap; the private opcode-45 `u32` has a
1,000,000/`UINT32_MAX` project upper bound, the second decompressed payload is
65,536/1,048,576 bytes, and transition events are 64/256. None is a claim
about a stock engine buffer maximum.

Session and reassembly mutations are transactional. Preparation is read-only;
only successful send or receive commit advances sequences/generations, promotes
pending A, records a fragment, or publishes completion. Failures and abandoned,
foreign, stale, or consumed plans preserve prior state. Canonical bytes are
remunged for each committed sequence. Count-greater-than-one outgoing normal
fragments use deterministic per-fragment stop-and-wait; this is project-tested
policy pending stock-client multi-fragment verification.

Fresh signed-stock capture confirms two descriptor slots, slot-0 normal shape,
1,024-byte chunks, no stable wire transfer ID, per-fragment alternating reliable
generation, ACK-gap retry, duplicate/old filtering, and next-transfer gating.
True unseen reorder, slot-1/file semantics, compression universality, explicit
old-after-completion behavior, and live project-to-stock fragmentation remain
pending. See [GoldSrc fragmentation](GOLDSRC_FRAGMENTATION.md).

The deterministic M2.3.2 fake-HLDS UDP integration reuses the exact bootstrap
socket/source/session for one outgoing reliable clear and one incoming owning
reliable delivery with duplicate/old filtering. M2.3.3 codec, reassembly,
outgoing, and driver behaviors are covered deterministically; such tests are not
promoted to stock-server interoperability claims.

`NetchanDriver` is the reusable same-transport polling/timeout owner. The
bootstrap stage/coordinator constructs and owns one through `--stop-after
netchan-bootstrap`; `--stop-after signon-boundary` instead creates
`InitialSignonStage` directly after `ACCEPT`, so no second driver competes for
the socket. `--stop-after pre-resource` creates `PreResourceSignonStage`, whose
nested private-retention mode reuses that exact initial stage, driver, socket,
and lifetime until the synchronous server-info continuation terminates.
`DeltaDescriptionStage` and `MovementEnvironmentStage` repeat that private,
friend-only retention pattern for the exact delta and movevars cursors.
`UserInfoSignonStage` carries the same owning payload to exact first-batch end;
`ResourceTransitionStage` alone may retain the same driver afterward for the
fixed request/ACK/second-transfer path. The public earlier stops retain their
historical close-on-success behavior. An
embedding composition can also own the driver persistently. In every case the
existing transport and optional opaque `INetchanDriverLifetime` stay valid
through the driver terminal. Timeout, cancellation, network/protocol
failure, event backpressure, and close clear reliable/reassembly/unreliable
state and release that guard exactly once. A lower-level session caller remains
responsible for `NetchanSession::clear_reliable_state()` on terminal failure.

Challenge traces use bounded previews. Connect-request, connect-response,
netchan, fragment, driver, initial-sign-on, pre-resource, delta, and
movement-environment, user-info, and transition traces never contain raw
packets, authentication bytes, private user-info/control values,
compressed/decompressed payloads, or boundary remainders. Typed callbacks may
carry confirmed owning-state string views (such as schema or sky names) only
for the callback duration; terminal output always passes untrusted text through
the bounded presentation sanitizer. See
[Connect response](GOLDSRC_CONNECT_RESPONSE.md),
[Netchan](GOLDSRC_NETCHAN.md), [Fragmentation](GOLDSRC_FRAGMENTATION.md),
[Initial sign-on](GOLDSRC_INITIAL_SIGNON.md),
[Server info](GOLDSRC_SERVERINFO.md),
[Delta descriptions](GOLDSRC_DELTA_DESCRIPTIONS.md),
[Movement environment](GOLDSRC_MOVEVARS.md),
[User info](GOLDSRC_USERINFO.md),
[Resource transition](GOLDSRC_RESOURCE_TRANSITION.md),
[Resource list](GOLDSRC_RESOURCE_LIST.md),
[Resource response](GOLDSRC_RESOURCE_CLIENT_RESPONSE.md),
[Local resource resolution](LOCAL_RESOURCE_RESOLUTION.md),
[Local consistency provider](LOCAL_RESOURCE_CONSISTENCY_PROVIDER.md), and
[Authentication provider](AUTHENTICATION_PROVIDER.md).

## Filesystem and asset boundary

The repository contains no game data. A user explicitly supplies `--basedir`
and optionally `--game` (default `valve`). The M3.2.1 local provider additionally
requires explicit `--resource-consistency-provider local`; basedir alone does
not enable it. A non-Valve game produces an ordered game root followed by a
`valve` fallback, while `valve` produces one deduplicated root. There is no CWD,
registry, Steam-library, environment, repository, or build-directory discovery.

Normal project runtime may read an explicitly supplied, user-owned Steam
Half-Life installation. It does so with zero writes, launches no stock process,
and does not mutate configuration. Active stock `hl.exe`/HLDS research remains
subject to the stricter isolated-copy marker and root preflight. Runtime access
does not relax that research policy, and neither path proves stock
interoperability by itself.

Server resource-name bytes never become native paths directly. The pure
GoldSrc mapper accepts printable ASCII and `/` only, rejects traversal,
absolute/drive/UNC/device/ADS/backslash/reserved-device and bounded-name
violations, and does no decoding or repair. The backend then converts the safe
virtual name component-by-component. It opens with Win32 read-only
`CreateFileW`/`GENERIC_READ`/`OPEN_EXISTING`, rejects every intermediate and
final reparse point, requires a regular local disk file, and validates the
opened handle's final target, volume, and root containment. Metadata and bounded
streaming bytes come from that same handle; initial/final identity, size, and
write/change metadata detect concurrent modification.

The production provider resolves only profile-fixed `tempdecal.wad`, never a
server- or CLI-selected filename. The general ordered resource inventory stores
correlation metadata and typed resolution status only. It is not a readiness,
precache, download, cache, asset, or renderer state. Downloaded content, if a
later milestone implements it, must be validated before becoming visible to
asset loaders.

Asset formats (BSP, WAD, MDL, sprites, sounds) should have parser libraries that
do not depend on SDL, the renderer, or a live network session. This makes them
fuzzable and reusable by tests and tools.

The M0.1 asset path is:

```text
IFileSystem -> AssetSource -> typed importer -> neutral CPU asset -> AssetManager
```

`AssetSource` owns its path and input bytes. Typed registries own importers and
select them by signature/version/structure confidence followed by explicit
priority; an unresolved tie is an error rather than a registration-order
choice. Format modules are statically linked and registered explicitly by the
composition root. See [Asset pipeline](ASSET_PIPELINE.md) for the complete
selection and extension contract.

There is no global registry, global constructor registration, renderer-side
file parsing, or GPU handle in a neutral asset. M3.2.3 adds a separate approved
path for one manifest locator:

```text
LocalResourceLocator -> reopen_verified -> same-handle bytes
    -> ApprovedAssetSource -> AssetDispatchPlan -> typed registry
```

That path never reopens `AssetSource::virtual_path()` and never calls
`AssetManager`. Pure probes preserve confidence-before-priority selection.
M4.1 production composition registers one `goldsrc-bsp-v30` world importer;
explicit empty registries retain the typed no-importer test boundary. Its
filesystem-free target publishes CPU geometry only and does not feed a
renderer.

M4.2 adds a later path that deliberately keeps container parsing separate from
filesystem authority:

```text
retained approved BSP bytes + WorldAsset
    -> used BSP miptex source ranges
    -> embedded RGBA textures
    -> inert worldspawn safe WAD basenames
    -> LocalResourceEnvironment resolver
    -> exact-root verified WAD AssetSource
    -> WAD3 catalog + shared miptex decoder
    -> immutable WorldTextureSet
```

Compiler-recorded prefixes are discarded before local resolution. The existing
resolver alone chooses the ordered game-before-`valve` root, and every found
WAD is rebound to an exact-root locator before the approved source opener reads
it. At most one WAD source is live. The texture catalog and decoder never gain
a native path, and `AssetManager` is not used. Earlier asset/geometry routes do
not enter this path. See
[world texture resolution](WORLD_TEXTURE_RESOLUTION.md).

The external-map compatibility profile separates source completeness from
renderability. The texture import API defaults to strict rejection; application
composition explicitly selects `placeholder_for_absent_name` only after all
required declared WADs were approved, opened and parsed successfully. One small
bounded project-generated checker image can represent absent names through
typed `substituted_missing_texture` bindings. It is never counted as a decoded
source image or assigned archive provenance. Missing/malformed archives,
dimension mismatch, unsafe paths and byte/count bounds remain strict. Source
material dimensions determine UVs even when a shared placeholder is used.
`WorldTextureSet::complete_for_world_materials()` still reports source
completeness; `renderable_for_world_materials()` accepts explicitly typed
placeholders. Package/stage/brush/renderer consumers use the latter without
learning Half-Life semantics. Core-only fixture tests cover this same path.

M4.3 consumes that owning state without introducing new filesystem authority:

```text
retained approved BSP bytes + renderable TexturedWorldAsset
    -> GoldSrcWorldLightmapImporter -> WorldLightmapSet
    -> WorldRenderPackageBuilder -> immutable WorldRenderPackage
    -> ClientWorldState -> RenderScene -> IRenderer
```

The lighting importer receives a byte span, never a path. BSP/lightmap validation
accepts bounded zero-sample light cursors on faces whose first style is 255;
the original cursor is retained, no atlas is allocated for that face, and
active-style end/beyond-end or invalid negative cursors remain errors. Package publication
transfers local ownership once and then the stage finalizes the retained
driver/authentication lifetime. The offline viewer begins at a caller-supplied
safe virtual map name and follows the same resolver/source/importer chain with
no network and no writes. See [GoldSrc world lightmaps](GOLDSRC_LIGHTMAPS.md),
[world render package](WORLD_RENDER_PACKAGE.md), and
[offline world viewer](WORLD_VIEWER.md).

## Clean-room and SDK boundary

No runtime target may depend on `Pvitaly91/hl-engine`, its server code, or any
other private server implementation. Compatibility work is based on observable
behavior, public protocol information, and an explicitly isolated official
Valve SDK reference.

Implementation code must not be copied from ReHLDS, Xash3D,
reverse-engineered/proprietary GoldSrc source dumps, or original Valve binaries
such as `hl.exe`, `hw.dll`, or `sw.dll`. External implementations may be treated
only as separately recorded behavioral/reference material where legally and
technically permissible. Repository code must remain independently authored.

M4.7.1.2A applies that reference-material rule to a narrow runtime-control
profile. Public ReHLDS revision-pinned opcode/field facts are recorded before
the project-owned implementation in
[GOLDSRC_RUNTIME_CONTROL.md](GOLDSRC_RUNTIME_CONTROL.md). The decoder consumes
the existing owning service payload and byte reader, publishes typed owning
events/control state, and never crosses into OpenGL. Its reference-driven
profile is a separate type and status domain from the strict accepted-stock
catalog, so running literal fixtures cannot satisfy stock evidence gates.

M4.7.1.2B/C extend the same boundary with separately named public-reference
profiles for sign-on baselines, runtime delta values, and packet-entity
snapshots. The packet path shares the owning service payload/cursor, runtime
control dispatcher, schema registry, bit reader, and delta-value decoder. It
stages a complete immutable full or reconstructed delta snapshot and an exact
30-bit, generation-bound history update before one commit. Its eight-bit base
tag is resolved against the current transport sequence; it is never a history
slot, command acknowledgement, or permission to choose a nearest frame. See
[GoldSrc entity baselines](GOLDSRC_ENTITY_BASELINES.md) and
[packet-entity snapshots](GOLDSRC_ENTITY_SNAPSHOTS.md).

M4.7.1.2D adds a separate `public_goldsrc48_clientdata_v1` profile for the
ordinary game-client `svc_clientdata` body and its embedded `weapon_data_t`
records. It reuses the same owning payload/cursor, validated schema registry,
bit reader, delta-value decoder, staged `svc_time`, and service dispatcher.
Client frames and their 64 wire-indexed weapon substates have an independent
exact-reference, generation-bound bounded history; entity-only frames do not
fabricate clientdata bases. A source frame may atomically publish both client
and entity substates without conflating either reference with a ring index,
transport ACK, usercmd ACK, inventory identity, or prediction authority. See
[GoldSrc clientdata and weapondata](GOLDSRC_CLIENTDATA.md).

M4.7.1.2E composes these exact A/B/C/D decoders in an in-memory runtime replay
session over owning, already extracted service payloads. It stages protocol
state and a narrow immutable `ClientWorldState` runtime observation before one
commit, preserving distinct freshness/source identity for control time,
clientdata and packet entities. The client attachment is renderer-neutral and
contains no packet/schema/socket/resource ownership. See
[GoldSrc runtime replay and client-state bridge](GOLDSRC_RUNTIME_REPLAY.md).

M4.7.1.2F composes that E session directly into the existing `hlclient.exe`
NullRenderer update loop through an `IClientSceneSource`. The source owns the
one application `ClientWorldState`, preserves ordered backlog under bounded
record/byte budgets, and terminates the ordinary loop on typed EOF, failure or
incomplete stop. CLI selection enters this offline path before socket,
authentication, filesystem or asset-provider construction. See
[application-owned runtime replay](APPLICATION_RUNTIME_REPLAY.md).

M4.7.1.2G extends that same source with an explicit diagnostic visual mode.
After each successful entity-bearing E commit, the application maps complete
neutral origin/angles into an immutable `EntityRenderFrame`, validates it, and
publishes it beside a once-built project-generated Studio package in the same
`ClientWorldState`. `build_render_scene()` and the existing Null/OpenGL dynamic
entity paths remain unchanged consumers. Renderer targets still cannot see the
replay session, codecs, schemas, packet bytes or diagnostic binding policy.
Presentation offsets and a fixed camera belong to the application demo; they
do not alter canonical wire state or static resource identity. See
[application-owned runtime replay](APPLICATION_RUNTIME_REPLAY.md).

M1's wire profile was established by black-box observation of stock signed
Valve programs: the exact request transmission was captured from original
`hl.exe`, and the exact response was observed live from original HLDS. That
evidence establishes the framing, terminators, response shape, and dynamic
challenge. It does not establish the semantics of the three decimal values
after the challenge; they remain opaque profile parameters. No third-party
engine implementation was used as production source code.

The Half-Life SDK submodule is pinned. Its include directories are exposed by a
`SYSTEM` interface target so SDK warnings do not weaken or pollute warning
policy for project code. Do not compile SDK game/server programs, link its
prebuilt libraries, use its bundled SDL2, or copy SDK implementation files into
project modules. Any future use beyond reference headers requires an explicit
architecture and license review.

M4.4.1 uses the pinned public compiler sources only to establish the signed
surfedge endpoint rule, the `face.side` plane-normal rule, Valve's reversed
cross-product winding convention, mirrored-face reversal, and T-junction point
emission. The project-owned builder and its centroid-rebased arithmetic remain
independently authored. Exact references, the fixed `0.02` planarity limit,
the bounded area formula, and the float-scale strictly-interior collinear gate
are documented in
[GoldSrc BSP geometry compatibility](GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md).

## Composition and lifetime

`hlclient` is the composition root. A normal standalone frame follows this
order:

1. parse command-line inputs and validate paths/endpoints;
2. create the caller-owned `AssetImporterRegistries`;
3. explicitly register the compiled-in GoldSrc BSP v30 world importer before
   any dispatcher, stage, or `AssetManager` can borrow that registry;
4. independently create the asset-facing filesystem and `AssetManager` only
   when that existing asset path is requested; the approved manifest route
   borrows the registries but never feeds its path through `AssetManager`;
5. when a later stop point is explicit, acquire bounded authentication material
   through the configured provider and retain its optional session lifetime;
6. only for response/server-baselines/entity-snapshot/manifest/asset-dispatch/
   world-geometry/world-textures/world-render-package/world-spatial-scene/
   view-world routes with explicit local selection,
   validate `--basedir`/`--game`, prepare fixed-target consistency material
   through one sandboxed handle, close the file, and retain the one-shot
   provider plus its validated environment;
7. create one nonblocking UDP transport and the M1/M2 coordinator for the
   validated endpoint;
8. for an explicit asset-dispatch, world-geometry, world-textures,
   world-render-package, or world-spatial-scene stop,
   advance that retained coordinator headlessly until the selected-world source
   is dispatched; the texture route additionally retains the BSP bytes/world,
   opens required declared WADs one at a time, and publishes the owning texture
   set; the package route also imports RGB lightmaps and builds one immutable
   package; the spatial-scene route additionally builds canonical spatial/PVS
   state and any explicitly selected static initial brush scene; print the
   requested bounded result, release the optional lifetime
   exactly once, and return before selecting or initializing any renderer;
   the separate `server-baselines` and `entity-snapshot` diagnostics likewise
   advance the retained network coordinator headlessly and return before any
   renderer, asset importer, BSP, or WAD work (the stock evidence-pending
   profile stops at the exact unconsumed body and queues no request);
9. for `--view-world`, build the same scene headlessly, resolve the requested
   initial visibility/draw list, finalize retained driver/authentication state
   exactly once, and only then initialize SDL, an
   OpenGL context, and the local bounds-derived preview; this preview is not a
   connected gameplay session;
10. for ordinary rendering routes, select the built-in OpenGL or null renderer,
    initializing SDL/window/context before OpenGL only;
11. poll events where applicable, advance the handshake coordinator, and let
    the selected stage-owned driver process the same socket until the requested
    opaque, initial-sign-on, pre-resource, delta-schema, movement-environment,
    user-info, neutral opcode-43, standard resource-list/response boundary, or
    metadata-only precache-manifest boundary is acknowledged and published;
12. derive `RenderScene` from its `ClientWorldState`, render, and present until
    the configured terminal challenge/connect-request/connect-response/
    netchan-bootstrap/signon-boundary/pre-resource/delta-schemas/movevars/
    user-info/resource-list-boundary/resource-list/resource-response-boundary/
    precache-manifest/usercmd-boundary outcome, let driver terminal cleanup
    release its optional lifetime exactly once, then shut down renderer
    resources before their platform dependencies.

The production `usercmd-boundary` diagnostic follows the existing required
local-resource/authentication path only as far as the fail-closed stock
post-resource evidence boundary. It reports zero sampled commands, zero
history entries, and zero transmitted usercmd packets, then exits nonzero; it
does not construct a synthetic message for a production endpoint. Synthetic
codec/stage operation is exercised by project-owned checker/fake-peer tests and
is not exposed by the production composition root.

The standalone viewer is a separate composition root: it begins with one safe
virtual map under an explicit user-owned local environment, validates all CPU
assets and builds the scene before initializing SDL/OpenGL, performs no
network operation or write, updates only CPU camera/visibility state per frame,
and destroys renderer resources before the context. The M4.4.1 checker/wrapper
uses the same safe virtual-name and verified-open boundary without initializing
a renderer; before/after selected-file metadata and approved-root inventories
make external drift a terminal failure. Only bounded aggregate diagnostics may
be retained—never native paths, raw face arrays, map/WAD bytes, texture names,
or entity text.

Partially initialized states must unwind safely through RAII. Logging and error
messages should identify the failed boundary without exposing secrets or
turning malformed remote data into a process crash.

## CPU visual-asset import boundary

M4.5.2 adds separate targets for neutral model types, the network-free approved
source API seam, Studio v10, SPR v2, and GoldSrc visual dependency composition.
Parsers depend inward on the neutral asset API only; they cannot see a renderer,
`LocalResourceEnvironment`, network driver, or native filesystem. The outer
visual operation owns the
approved-or-verified source/environment boundary, uses the canonical
cross-category dispatcher, invokes only the selected registered importer, and
follows only a typed Studio dependency result.

```text
approved model resource
    -> verified owning source
    -> global model-or-sprite probe
    -> self-contained IDST / exact-root Studio bundle / IDSP
    -> owning ModelAsset or SpriteAsset
```

Models retain source-native bone-local geometry and compressed animation;
sprites retain indexed frames, palette, groups, intervals, and format metadata.
No output is a render instance. Entity-number/model-index association,
fractional interpolation, body/skin/sequence application, billboarding, blend
state, and OpenGL upload remain outside the importer boundary. M4.5.3 implements
them in separate entity-visual, pose, renderer-neutral scene, and OpenGL modules.
`RenderScene` sees only shared entity package/frame contracts; it never sees a
GoldSrc snapshot, local-resource locator, or native path. Stock visual projection
remains a fail-closed evidence boundary.

M4.6.1 adds another project-owned source path without changing the renderer
contract:

```text
SdlWindow sole event pump -> InputEvent -> InputStateTracker
    -> GameplayInputIntent -> InteractivePreviewController
    -> ClientWorldState camera -> RenderScene -> IRenderer
```

`hlclient_input_api` has no SDL dependency; `hlclient_platform_sdl_input`
contains the private native translator; `hlclient_gameplay_input` has no
network dependency; `hlclient_gameplay_camera` uses only neutral camera math;
and `hlclient_interactive_preview` is the sole camera/publication coordinator.
No input type crosses `RenderScene`, and no renderer polls SDL. The profiles
are project-owned; stock input and `usercmd` mapping remain evidence-pending.

M4.6.2 adds a separate synthetic-only path without changing the renderer
contract:

```text
GameplayInputIntent + immutable first-person camera
    -> input adapter -> fixed-step scheduler -> bounded history/planner
    -> exact 15-field binding -> delta/envelope/checksum
    -> sequence-bound Netchan context -> same driver/socket unreliable suffix
```

The codec targets see only the narrow socket-free delta-schema API and do not
see sign-on stages, SDL, filesystem, renderer implementations, or a socket.
The session target receives the existing neutral intent/camera values and
retained driver explicitly; render conversion lives in a separate adapter
target. Every stock usercmd profile fails before usercmd encoding/submission:
accepted stock runs and verified move packets are both zero.
No collision, movement prediction, acknowledged-command replay, or server
reconciliation is part of the M4.6.2 usercmd targets. See [GoldSrc usercmd](GOLDSRC_USERCMD.md) and
the [usercmd transmission lifecycle](GOLDSRC_USERCMD_TRANSMISSION.md).

## M4.6.3.1 collision ownership

```text
verified AssetSource -> one GoldSrcBspParser invocation
    -> WorldAsset / spatial source / collision source attachment
    -> GoldSrcCollisionWorldBuilder -> immutable CollisionWorldPackage
    -> caller-owned CollisionQueryScratch -> reentrant point and hull queries
```

`hlclient_collision_api` is renderer-, network-, filesystem-, SDL-, and
OpenGL-neutral. `hlclient_goldsrc_bsp_collision` adapts only canonical owning
BSP records; it accepts no raw byte source. Hull 0 preserves the node/leaf
domain, hulls 1–3 preserve clipnode/contents, and all four exact compiler
profiles are explicit. `hlclient_goldsrc_collision_scene` adds only explicit
rigid brush instances and stable tie ordering. The stock role provider opts no
brush into solidity; dynamic doors/platforms remain future runtime work.

The production `collision-world` route aliases the already established
approved-asset network boundary, then builds and probes collision CPU state.
It changes no packet, ACK, `new`, `sendres`, opcode-5, or usercmd behavior and
does not place clipnodes in render state. See [collision package](COLLISION_WORLD_PACKAGE.md)
and [trace API](COLLISION_TRACE_API.md).

## M4.6.3.2 local movement ownership

```text
hlclient_movement_api (immutable state/touches/statistics)
    <- hlclient_goldsrc_local_movement
       (MoveVars environment + pure kernel + collision adapters)
    <- hlclient_local_player_controller
       (fixed synthetic scheduler/input adapter + player camera +
        renderer-neutral BSP entity-transform parser for spawn metadata)
    <- offline movement checker / world-viewer player-walk composition
```

The kernel receives const previous state, typed synthetic command, immutable
environment, collision interface and caller-owned scratch. It reads no clock,
input, renderer, file or network state. Success publishes one complete
successor; error leaves the previous state unchanged. Collision targets do not
depend on movement.

The local controller receives caller-supplied monotonic time and composes the
existing scheduler/adapter without usercmd network history. The renderer sees
only the normal camera/scene boundary. Production player-walk collision is
world-only; explicit synthetic static brushes are opt-in test/scene metadata,
and dynamic/stock brush solidity is not inferred. Full `PM_Move`, liquids,
ladders and stuck recovery remain outside this ownership graph.

## M4.6.3.3 prediction ownership

```text
immutable input intent
    -> transactional LocalPlayerMovementPreparedUpdate
    -> immutable bounded LocalPredictionHistoryState
    -> typed in-memory AuthoritativePlayerState
    -> LocalPlayerPredictionReconciler
       -> same GoldSrcLocalMovementKernel for exact retained commands
    -> immediate corrected physical state
    -> PredictionVisualCorrectionState (camera sample only)
```

Prepare performs simulation and allocation without controller mutation.
Preflight plus non-allocating commit publishes movement and history together;
an abandoned plan retains accepted one-shot input and the previous physical,
camera, scheduler, and history publications. Reconciliation rejects session,
environment, movement-config, collision-world, authority-order, and history
gaps. Replay never samples input, emits effects, resends a packet, or mutates an
old publication. Small correction residuals decay linearly for 100 ms on the
camera only; large, discrete, teleport, and hard-reset corrections snap.

`SyntheticAuthoritativePlayerSimulator` owns an independent state/scratch and
bounded command/update queues. It is a tool/test authority, not evidence for a
stock server. The stock authority adapter returns `stock_evidence_pending`;
neither netchan ACKs nor generic entity field names are treated as
player-command acknowledgement or authoritative player state.

## Module and plugin policy

The current product is a modular monolith: one executable, separate static
CMake targets, narrow public contracts, and explicit construction and
registration in `apps/hlclient`. This avoids committing to a C++ DLL ABI while
the APIs are still changing.

After stabilization, optional runtime plugins may use a versioned C ABI. That
future ABI must not pass STL containers, `std::string`, iterators, C++
exceptions, C++ virtual objects across different toolsets, or memory without an
explicit allocator and ownership contract. M0.1 does not implement a loader or
that ABI.

## M4.7.1/M4.7.1.1 stock runtime research boundary

The production pending catalog and the isolated research sidecar remain
independent. Captured bytes never become production runtime state directly:

```text
exact PostResourceResponseBoundary
    -> standalone pending catalog (neutral category, exact cursor)
    -> unresolved identity + tagged ACK candidates
    -> partial authoritative observation + provenance
    -> optional immutable runtime frame/history
    -> fail-closed authority/visual adapters

explicit token + elevated/capable Windows host
    -> signed binary/App/build observer
    -> temporary dynamic-WFP loopback-only guard
    -> Job-owned relay -> HLDS -> stock client
    -> ignored raw datagrams + observed/delivered journal
    -> bounded corpus loader + existing pure protocol codecs
    -> exact post-resource byte/bit boundary
    -> neutral unconsumed first candidate
    -> exact restoration + external drift compare
    -> checker/walker agreement -> one final run manifest
```

Active capture is off by default, creates no persistent firewall rule and
remains unavailable without the isolation/canary capability. PowerShell owns
snapshots, restoration and final publication; the C++ orchestrator owns every
stock process/socket/WFP lifetime through retained handles and a kill-on-close
Job. Offline replay follows peer-delivered transport ordinals and generates no
packet. The catalog is not yet composed into production `PostResourceSignon`;
M4.7.1.2 owns runtime grammar and M4.7.2 owns stock usercmd. No protocol library
depends on prediction or a renderer, and reserved confirmed profiles remain
unreachable while this checkout has zero accepted real runtime runs.

### M4.7.1.1.3 external-target provenance boundary

The research-copy boundary adds a separate read-only topology review and an
explicit local, expiring approval artifact for exact eligible external
directory targets. Review is not approval; approval is not a generic bypass.
Only non-executable, non-mutable content named by the exact review may be
materialized, and it becomes ordinary unlinked destination data. Arbitrary or
changed external links, executable/mutable targets and every unsupported
topology remain fail-closed.

All new materializations publish preparation manifest v3. Materialization
binds an approved copy to the exact local review/approval digest and verified
source/target state. Active preflight verifies the closed, path-free v3
contract and live prepared destination. For a reviewed profile it also
requires exactly one bounded, ordinary, single-link/no-ADS repository-local
approval artifact whose fresh SHA-256 equals the v3 approval digest;
canary/run/campaign/verifier metadata retains only the sanitized
external-target profile and count. The boundary provides no runtime semantics
and no evidence. No real review, canary or campaign success is claimed by this
architecture entry.

### M4.7.1.2H/I functional capture and explicit local-asset scene

H completed offline replay of the specific functional capture
`ededd068a0444ff497d98204eacec8a4`; old acquisition failures are historical.
Its campaign-evidence eligibility remains false. I retains owning resource
context from the same sign-on parser and resolves committed ordinary entity
model indices only in the captured sparse model namespace. Approved rooted
readers/importers prepare the BSP/WAD/lightmap world and Studio/Sprite assets
once from an explicit read-only local game root. The existing session, scene
source, frame composers, `ClientWorldState` and renderer paths remain shared.
The renderer owns neutral packages/frames, not packets, schemas or paths.

The executable local-asset profile is separate from both G's project-generated
diagnostic binding and the strict evidence-pending authority adapters. The
verified scene slice has 16 supported Studio instances; eight inline-brush and
five material variants remain unsupported. Details, exact provenance and
commands: [captured local-asset replay](STOCK_CAPTURE_LOCAL_ASSET_REPLAY.md).
This is neither a new active capture nor live/universal stock compatibility.

### Manual wall-clock stalls and optional test-server startup

The scheduler defaults to strict bounded catch-up. Only the manual host opts
into discarding unsampled wall-clock slots after a stall: one neutral command
on the retained grid, unchanged duration and identities, no input replay.
Scripted evidence remains strict. The standalone test-50 helper is never a
core/client dependency; its loader/configuration and health confirmation are
separate bounded stages of the existing restoration transaction. See
[corrective contract and evidence](MANUAL_STALL_TEST50_CORRECTIVE_20260927.md).

### D4 reference ground and committed brush collision

The live host's immutable current-server-frame collision context binds decoded
neutral solidity/movetype fields and sparse model slots to the existing BSP
brush hull library. Seed, movement, duck clearance and camera collision use
the same scene; rendering remains an independent consumer of committed state.
The Half-Life module continues to select the unchanged movement/input policy.
No carry or pusher extrapolation is inferred from render transforms. See
[D4 scope, precision, lifecycle and verification](GROUND_BRUSH_PREDICTION_D4.md).

The D4 vertical-lift continuation adds an explicit Half-Life module policy for
bounded **derived** vertical support motion from two coherent server-time/brush
observations, never from renderer matrices or receipt time. Shared
`simulate_reference_movement` serves both append and replay; the existing kernel
and collision library remain generic. A server seed includes prior pusher
displacement, so only each command's future interval is carried. The sampled
collision scene also supplies render-only D3 transforms at the player's
presentation time; committed observations are untouched. See
[vertical-lift time, ownership, bounds and tests](VERTICAL_LIFT_PREDICTION_D4_CONTINUATION.md).

E32 retains that bounded support profile while preserving an unchanged pusher
trajectory across same-svc-time updates of unrelated collision membership.
Exact sampled command deadlines replace presentation-only reconstruction from
command ID distance after scheduler stalls; obsolete carrier boundaries cannot
rewind the retained prediction anchor. Canonical record publication and wire
commands remain independent. Optional application-owned movement incident
reports use bounded numeric observations and nonblocking identity-bound GPU
queries, not a new telemetry/physics/network service. See
[LAN movement/lift evidence and limits](LAN_MOVEMENT_LIFT_STABILITY_E32.md).

The optional whole-session continuation aggregates accepted, sanitized frame
observations in the same application receipt owner. A separately accounted
512 KiB maximum heap allocation retains histograms, context-separated counts
and 512 adaptive time bins after the detailed incident windows fill. Focus,
capture and committed lifecycle are observations, not new movement policy.
Delayed GPU samples retain their original frame identity and context. Normal
close embeds this evidence in the existing bounded movement report; collection
does no file I/O and does not change command, prediction or render timing.

### E2 local weapon audio

The Half-Life B1 controller emits owning GameClientAPI audio cues; imported
Studio metadata reaches a generic bounded marker traversal and HL1 5004 policy.
The host drains the selected module, then a neutral local presentation owner
uses the E1 approved loader/cache/mixer/output. Rendering, transport and movement
replay emit no local weapon sound. Local cancellation scopes never reset world
voices. See [E2 ownership, references and verification](LOCAL_WEAPON_AUDIO_E2.md).

### E5 local Glock static-world impact presentation

The existing accepted B1 Glock identity and exact submitted command angles
cross `GameClientAPI` with an owning local-compatible shot context. The
Half-Life module alone selects Glock eligibility, `{SHOT1` from the approved
same-root `decals.wad`, and `weapons/ric1.wav`. The shared application owner
performs a bounded static BSP trace, refuses a nearer committed brush blocker,
uniquely matches the visible world surface and clips the decal polygon to its
triangles. `RenderScene` carries neutral masked world geometry; OpenGL draws it
with depth test and offset. The existing E3 mixer receives one separate
world-origin sound cue. No wire, ammo, damage, HUD or movement authority is
added. See [E5 contract and limits](LOCAL_GLOCK_WORLD_IMPACTS_E5.md).

### E9.1 neutral server event framing corrective

The shared RuntimeControlDecoder accepts bounded Protocol48 svc_event and
svc_event_reliable observations through the normal mixed dispatcher, using the
advertised event_t delta schema and an explicit null/default arguments base.
Owned immutable decoded values survive RX buffer release. Queued entries keep
packet-list references distinct from entity IDs and delays as unscaled wire
ticks. Entire queues align once; per-object and complete-payload budgets apply
before publication. Unknown framing is never skipped or scanned past. The
existing record transaction/duplicate fingerprints remain authoritative for
publication; canonical world state and GameClientAPI semantics are unchanged.
This corrective does not execute .sc callbacks or create remote weapon effects.
Early managed A failure retains B's bounded redacted diagnostic/exit observation
after exact owned cleanup. See [E9 corrective evidence](REMOTE_PLAYER_MOVEMENT_AUDIO_E9.md).

### E10 remote player visual policy

Normal application assets now sample committed player observations through the
existing entity interpolator and the selected `GameClientAPI` module. Half-Life
owns gait/pitch/torso/skeleton partition/transition rules; the shared Studio
evaluator owns bounded numeric local-pose composition and skinned bounds. Existing
world/render packages carry the result; there is no second player renderer.
Public server seconds and exact source-record identities are kept separate from
synthetic interpolation evidence. Only players interpolate here; D4 brush and
E9/audio/RX ownership stay unchanged. The same host/API accepts a test-only
alternate module; lower targets never link the concrete HL library. See
[E10 fields, lifecycle, limits and verification](REMOTE_PLAYER_VISUAL_ANIMATION_E10.md).

Remote gait clocks survive bounded gait switches, equal-time updates and skipped
render records; genuine continuity boundaries still reset them. The API carries
committed anchor clocks separately from held renderer metadata. Shared Studio
rotation channels interpolate endpoint quaternions (with controllers applied
to both), not Euler scalars; this remains a neutral pose mechanism without a
Half-Life dependency in lower targets.

### E11 selected remote attachments and neutral impact particles

Half-Life selects the committed sampled `weapon_model_index` via GameClientAPI;
the same approved asset materializer resolves it and merges the evaluated player
pose through a generic cached Studio correspondence. Primary/attached draw
ordinals share the original entity ID. The renderer never chooses a weapon or
searches bone names. Lower targets remain independently buildable.

Half-Life also selects a bounded numeric Glock-impact profile. Local commands
and committed remote events converge on the same existing validated static BSP
impact owner and fixed-memory particle pool; neutral world billboards reuse the
normal GL pass. No asset, audio, collision authority or new wire path is added.
See [E11 contracts and verification](REMOTE_PMODELS_BULLET_PARTICLES_E11.md).

### E12 evaluated remote weapon sockets

The selected game module plans narrowly bounded numeric sockets from a
synchronous approved immutable model view. The existing asset host evaluates
them against the **same final merged p-model pose** and entity matrix used by
the normal renderer, then publishes owning samples only after the scene commit.
The game module correlates committed events with exact emitter/lifetime/weapon
and source-order evidence and freezes one-shot outputs. Transport framing,
server animation, trace/impact and renderer/effect execution stay unchanged.
The generic O(1) socket helper lives in `hlclient_entity_render_api` and depends
on neutral `hlclient_studio_pose`, never the Half-Life library; an independent
test module uses the same seam in the core-only configuration. See
[E12 profile, correlation and verification](REMOTE_GLOCK_PMODEL_FIRE_E12.md).

### E13 server-owned player proxies and visual continuity

Exact optional neutral render-effect/amount observations cross the existing
transaction; selected-module policy classifies a server-owned entity proxy and
selects a current approved player model slot. Generic assets/Studio/renderers
execute the owning corpse's numeric pose/transform without a second corpse pool
or concrete game dependency. A bounded post-commit notification gates queued
old-life remote effects and clears numeric player presentation across observed
inactive/re-arm or slot boundaries, never network/prediction/assets. Corpse
sampling is stateless/current; living-player EF_NOINTERP and game-selected
discontinuities step instead of flying between death and spawn. See
[E13 reference, ownership, limits and verification](REMOTE_PLAYER_DEATH_CORPSE_RESPAWN_E13.md).

### E14 multiplayer display state

The owning protocol userinfo parser exposes only a bounded display-name
projection and a private same-occupant comparison. Replay stages a fixed-size
occupant cache and neutral owning display updates at the existing atomic record
boundary; initial signon identities and pre-baseline updates follow the same
path. Only the selected Half-Life module interprets registered ScoreInfo,
DeathMsg and team messages, owns scores/notices, and chooses sorting/layout.
DeathMsg is never E13 visual death authority. Existing input supplies held TAB
and numeric viewport context; no platform handle or new wire button crosses the
API. The generic host/GL pass share explicit neutral HUD command/glyph bounds,
not Half-Life policy. Core-only and an independent module use the same seam.
See [E14 contracts and verification](MULTIPLAYER_SCOREBOARD_DEATH_NOTICES_E14.md).

### E15 firearm profiles and neutral tracers

Half-Life extends existing weapon presentation with an MP5 primary profile,
bounded command-driven pending actions and per-emitter tracer cadence. API
owning sequence labels/opaque socket keys select numeric output; the asset
host evaluates the same merged pose. Existing world-impact storage expires
fixed-memory lines and GL renders neutral depth-tested flat or solid geometry. No gameplay
policy moves into renderer/protocol/core. Both mp5.sc and mp52.sc bind exact
advertised names; E16 adds secondary presentation without primary effects.
See [E15 contracts](MP5_PRIMARY_FIRE_E15.md) and [E16 contracts](MP5_M203_SECONDARY_E16.md).

The MP5 tracer profile now selects a short moving streak (48 world units,
12,000 units/second, 78 ms maximum age), not a full muzzle-to-hit line. The
neutral world-impact owner captures the first valid presented local muzzle
once, or the committed remote origin, and advances a bounded segment toward
the immutable trace endpoint. Post-launch camera/weapon movement cannot drag
it. Endpoint clipping, fading, dedup, reset and expiry remain generic; profile
values and cadence remain Half-Life-owned. This is presentation-only, not
projectile physics or hit/damage authority. Zero motion fields preserve the
stationary neutral-line mode for other callers.
The neutral `head_taper_fraction` selects the tapered portion (90% for MP5);
the retained flat variant tapers its leading end, while the current solid
variant puts the taper at the rear and the rounded cap at the front. MP5 also
selects `taper_end_diameter_fraction=.20`: its rear frustum ends at 20% of the
body diameter, then a small four-ring rounded cap closes it within the same
interval. The neutral default zero preserves the original sharp solid cone.
Zero taper preserves other rectangular flat lines. This finite [0,1] value crosses
API/owner/scene and affects only the shape in the shared GL pass. The muzzle-
flash and particle shapes, line endpoints and render states are unchanged.

MP5 now selects `WorldLineGeometry::solid_3d` in both Half-Life profile sites.
The appended API/scene field retains `flat_billboard` as the neutral default;
the original quad builder and shader remain unchanged and independently tested.
`renderer/render_line_geometry.*` builds a camera-independent closed twelve-sided
tube/frustum/caps with a fixed maximum of 648 vertices, diameter equal to selected width
(local first-person 0.65, remote 1.30), with taper starting 10% behind the rounded
front and continuing to the narrowed rounded rear. The existing owner alone drives flight/fade/endpoint/
glow. GL draws outward faces additively with a mild radial emission falloff and
restores blend/cull/front-face/depth-write state; no camera-facing geometry or
asset/material/game logic is added. Geometry stays between the presented start
and clipped end. Core-only tests include the same pure mesh builder. See the
E15 3D follow-up for preservation and offline versus manual evidence.

Half-Life chooses a stable per-action MP5 forward start distance in [0,16]
world units, independently of render sampling and without global RNG. The
owning neutral API scalar `start_offset_units` defaults to zero, is finite and
bounded to [0,1024]. The shared owner adds it to travelled distance from the
actual captured local muzzle or committed remote muzzle toward the original
hit, once, without changing the wire/trace or restarting flight. A close hit
at/before the selected start emits no line/glow, never an overshoot. Static
neutral lines can also shift their start toward the same endpoint. Diameter
selection and variation remain in the HL layer; renderer and retained flat
variant share the same neutral output. Other flight/tip/glow values are unchanged.

Opt-in manual `-DebugFreezeTracer` forwards through the existing launcher/runner
and native orchestrator to `--debug-freeze-tracer` in the normal application.
Only the shared `WorldImpactPresentation` host owner changes presentation: it
materializes one owning solid moving-line sample at its actual first valid muzzle
and profile offset, with unchanged diameter, clipped length, taper and opacity.
It does not advance/fade/expire that sample; the latest valid tracer replaces it
(submission serial handles pool wrap/delayed anchors), map/network reset clears
it. Camera/viewmodel switches do not move it. No retained head billboard/light
obscures the inspection mesh. Flat/static lines and normal default flight remain
unchanged. Neither game eligibility/cadence nor wire/physics/audio authority is
changed; no extra renderer, assets, loop, or unbounded retained effects are added.

The solid-line leading end is now a smooth four-ring hemisphere within the original
start/end interval, with the optional narrowed rounded frustum at the rear. Front cap length is the
smaller of radius and half the existing body length; short clipped bodies use
an axial half-ellipsoid. The equator meets the unchanged-diameter body with
matching radial normals. The private mesh-generation axis is reversed (end to
start), keeping the owning start-to-end flight, clipped interval and front glow
unchanged. Historical `head_taper_fraction` still selects the pointed length
fraction; it does not relocate the source line or change the flat variant.
An explicit full-taper standalone cone has no body space and keeps its closed
flat base; the MP5's 90% taper uses a rear equator diameter of 20% followed by
a hemisphere. Rear cap length is min(rear radius, half taper length), so very
short clipped lines use a compressed half-ellipsoid without extending flight
endpoints. The appended owning diameter field is finite [0,1], validated by
the host and mesh builder, copied with the existing frozen sample; flat and
zero-taper shapes ignore it. The closed CCW mesh is bounded at 216 triangles,
with no new pass/material or change to flight,
fade, debug retention, flat billboards, glow, audio, or hit authority.

### E16 server-owned grenade and committed temporary effects

HalfLifeClientModule selects the explicit Attack2 input/movement carrier policy,
interprets MP5 secondary ammo/timers/water and exact advertised `events/mp52.sc`,
and owns launch animation/recoil/audio and explosion/smoke presentation profiles.
The generic host carries Attack2 unchanged in validated command history; movement
and replay treat it as an inert weapon bit. No client grenade simulation, contact,
ammo decrement, explosion trace or damage authority is introduced.

Strict svc_temp_entity framing publishes owning `CommittedTemporaryEffect`
observations only after the existing atomic record commit. The optional API callback
and bounded output batch work with alternate modules and core-only builds. Exact
generation/record/cursor/entry identity replaces proximity or selected weapon/life
identity. Half-Life profiles produce neutral imported-sprite, light and particle
intents. `TemporaryEffectPresentation` executes these through the existing particle
pool, shared scene light batch and `RuntimeReplayLocalAssets` server-slot materializer;
OpenGL/Null consume a separate transient sprite entity batch in the same scene.
Additive/index-alpha render-derived materials preserve immutable source pixels and
canonical asset identity. Flag suppression is per component; main explosion audio
remains evidence_pending rather than inventing a sample or duplicating svc_sound.

Ordinary ready Studio entities with a confirmed generic kinetic movetype opt into
the existing bounded snapshot interpolation; unknown/static ordinary entities retain
discrete behavior. No interpolation creates a projectile between unobserved snapshots.
Water explosion sprites use the received server index, while exact sparks/bubbles
grammar is consumed with explicitly deferred visuals. See [E16](MP5_M203_SECONDARY_E16.md)
for bounds, lifecycle, evidence and acceptance limitations.

#### 2026-10-04 corrective presentation boundary

The preceding E16 evidence-pending audio statement is historical. Half-Life now
adds an owning optional spatial sound intent to the same committed temporary
effect batch. Its fixed three-sample explosion profile is prepared through
`GameClientHost::temporary_sound_preparation()` at approved asset readiness.
The normal application delivers that batch exactly once through generic
`goldsrc::TemporaryAudio`, the existing local-resource loader and audio mixer.
Sprite absence and sound absence remain independent. NOSOUND suppresses only
the main boom; existing server debris is not duplicated. Sample variation,
gain and attenuation are explicitly local-compatible, not stock-binary parity.

TemporaryAudio has a separate world voice namespace from local weapons, remote
weapons and server entities. A neutral bounded mixer `stop_group` command clears
only that namespace at map/session boundaries. Weapon switches and player death
do not stop world explosions. Its 256 exact recent identities plus an evicted
monotonic commit-ordinal floor prevent duplicate/stale playback without imposing a total
256-event lifetime cap; pending cues retain the existing 250 ms expiry and
own their sample reference. No file I/O occurs in the audio callback.

Glock secondary now starts the existing neutral bullet-fire presentation from
accepted Attack2 commands in the HL controller, with explicit secondary pose
and shared .2/.3-second secondary/primary cycle selection. The same existing
weapon audio, flash/light, shell and world-impact mechanisms execute it; an
assigned glock2 event confirms without replaying those effects. Game rules and
sample names remain in the concrete module; ammo and damage stay server-derived.

`RuntimeOrdinarySnapshotPresentation` owns at most sixteen immutable committed
snapshot references and bounded ordinary-entity visual lifetimes. Its stable
application/server clock samples transforms only, after the unchanged player
sampler. The delay is one first positive observed cadence, capped at the selected
100 ms bound and available history (normally20 ms at50 Hz), not a fixed100 ms
new-grenade freeze. Birth floors prevent rewinding after a latest-pose fallback;
each ordinary lifetime also retains immutable birth server/application anchors.
A per-life lower target bound advances after one cadence and is capped at the
latest committed pose, so a late spawn cannot inherit session-wide clock phase
as a prolonged frozen first pose. It never changes the global/player clock or
extrapolates a trajectory. Repeated/backwards frames retain the monotonic target;
intervening absence, slot reuse, model/sequence/movetype/schema changes, NOINTERP,
teleport and clock gaps break continuity. Current removal and visibility remain
authoritative immediately. A newer untimed publication resets that history,
so it cannot hide an intermediate removal and bridge reused entity slots.
Updates later than that buffer can still cause a
brief endpoint hold: this bounded mechanism deliberately never extrapolates or
invents grenade velocity, collision or damage.

### E18 Python and neutral projection/edge-input policy

Half-Life's validated `v_357` roles, WeaponList aliases/ID 3, assigned
`events/python.sc`, MP viewmodel body 1, conventional cylinder reload,
shot samples and recoil live only in `games/halflife`. Remote `p_357` uses
the existing named-bone merge plus its separately validated Cylinder01
socket; the server body sequence is never forced by a fire event. The same
bounded audio, muzzle/light and static-world impact mechanisms execute the
intents; Python emits neither a shell nor a tracer.

Generic delta projection optionally exposes exact unsigned `clientdata.fov`.
Half-Life owns strict one-byte SetFOV interpretation, owning source provenance,
latest-committed-source reconciliation and reload/holster/life/map reset floors.
It outputs a neutral optional horizontal-degree `CameraIntent`; the application
camera adapter converts to vertical radians using the actual viewport aspect.
Rendering has no weapon/message identity, and projection never changes
translation, orientation, physics or command history. Missing overrides preserve
the existing generic host camera. Normal HL FOV 0 means horizontal 90 degrees;
Python's server-confirmed 40 is not a directly assigned vertical 40.

The 2026-10-15 zoom corrective derives a neutral mouse-sensitivity multiplier
from that same resolved FOV in the Half-Life module (default90 restores1;
other values use FOV/90 times the pinned SDK default ratio1.2). The normal
application composes it once into a copy of the base mouse configuration
before immutable input construction. Shared input validates the finite (0,4]
scalar and applies it before the existing frame cap; camera pitch bounds and
history/replay remain unchanged. No weapon/FOV formula enters the engine,
and default/alternate modules retain scale1. Existing source/reset floors
therefore govern projection and sensitivity together. See the E18 corrective.

The subsequent weapon-sensitivity audit of the same pinned SDK confirms that
unzoomed weapon selection has no independent input multiplier. All 15 selected
weapon IDs preserve the same physical mouse delta at default FOV; zoom alone
uses the existing module policy. No new per-weapon input rule was added.

The Studio-lighting corrective extends the existing retained-world RGB probe
from player bodies to ordinary packet models, camera-local viewmodels and
bounded transient models. The app supplies a separate world-eye probe point,
never moves the local mesh, and caches only numeric position/RGB samples.
No resource access or Half-Life identity enters renderer/input mechanisms.
World lightmaps, HUD grading, fullbright flags and effect-light policy are
unchanged. See `OPENGL_STUDIO_RENDERER.md` for scope and stock-parity limits.

An optional module-selected physical edge-button mask passes through the same
host, frame press latch and validated pre-history reference input adapter. It
suppresses repeated held RMB only for a validated selected Python; other weapons
retain their established hold policy. Pending presses are consumed only after
accepted history insertion; movement replay never toggles zoom or emits effects.
Alternate-module/core-only tests use these neutral seams without an HL fallback.
See [E18](PYTHON_357_FIRE_ZOOM_RELOAD_E18.md) for evidence and explicit limitations.

### E19 Crossbow authority and bounded fixed Studio intents

Crossbow ID/catalogue/model roles, assigned crossbow1/crossbow2 event meanings,
post-shot integer parameters, two launch cues, reload and MP FOV20 remain in
`games/halflife`. Generic committed event projection owns optional exact signed
integers; framing/atomic publication and canonical state contracts are unchanged.
Normal fire creates no local projectile/trace/explosion: the existing ordinary
packet Studio entity and generic kinetic interpolation present the server bolt.
Generic kinetic continuity has an explicit displacement guard independent of
remote-player teleport policy (currently 512 versus 128 units). Both paths
retain bounded observation gaps, current-list authority and no extrapolation;
the ordinary guard is local-compatible presentation recovery, not physics.
Sniper static hits emit neutral sound/particle/fixed Studio intents, not network
entities or damage. The same approved Studio materializer and transient renderer
pass execute a 32-slot, map-scoped 5 s fixed-model pool; weapon/life changes do
not erase published world effects. Map/session teardown does.
TE_SPARKS uses the existing particles, TE_EXPLOSION the existing explosion path;
exact TE_BUBBLETRAIL grammar is safe while water visualization remains deferred.
Remote p_crossbow uses named-bone merge and server animation, never forced poses.
The existing typed projection/RMB-edge seam now also accepts the validated
selected Crossbow. Core-only retains these neutral APIs without the HL module.
See [E19](CROSSBOW_PROJECTILE_ZOOM_SNIPER_E19.md) for resources, verification and
dynamic-hit/water/local-compatible limits.

### E20 RPG and generic server entity effects

RPG roles, catalogue eligibility, launch cues, recoil and exact optional auxiliary
state interpretation/reload/holster/secondary policy belong to `games/halflife`.
Core projects negotiated `vuser2` components without their game meaning. No
client projectile or laser trace is manufactured. Server ordinary entities and
the existing kinetic snapshot presentation own their visible poses.
TE_BEAMFOLLOW takes an owning bounded generic host outbox, bypassing concrete
game semantics. The application `EntityFollowPresentation` samples presented
poses and entity lifetimes, produces bounded textured ribbons and generic
EF_LIGHT intents. Rendering uses neutral glow/tint/ribbon execution only.
Glow entities persist by server presence/NODRAW; follow history decays by its
wire lifetime after removal and never reattaches to reused slots. Positive
audio source lifetime proof retires server loops on removal/reuse independently
of player life or visual culling. All resources remain approved manifest opens;
no game-specific file access occurs inside rendering/audio callbacks.
See [E20](RPG_LASER_GUIDED_ROCKET_E20.md) for exact ownership and limits.

### E21 Gauss and neutral command/voice/beam sidecars

`games/halflife/GaussPresentation` owns ID9/uranium/no-clip semantics, model
roles, fresh negotiated charge fields, charge timing, sample names, assigned
event meanings, reflection/one-punch policy and provisional energy-to-impulse.
The existing `HalfLifeClientModule` owns one controller per game session.

The same `GameClientHost` accepts a typed submitted command and returns an
optional finite velocity delta. The host stores that delta in the immutable
prediction command before simulation. Wire encoding is unchanged; the tagged
nonzero sidecar participates in the prediction history signature. Rebase uses
the saved delta exactly once per command, never calls the game module again,
and never emits audio, shots or TX. Zero-delta legacy signatures are unchanged.

The audio outbox adds bounded owning persistent voice controls (start/change/
source/stop); cursor-preserving pitch changes and whole-sample repeat use the
existing mixer/device. A new charge gets a new lifetime identity, so a delayed
old stop cannot stop it. Session reset clears pending controls/voices; the
module explicitly stops charges on weapon/life/source boundaries.

A borrowed synchronous effect-query interface exposes neutral hit data from
the nearest world or committed solid inline BSP. The existing approved BSP
owner and per-publication brush cache implement it. Unavailable reconstruction
does not fall through to a farther world surface. Exact brush hits can carry
an owning generation/lifetime/model identity plus local point and normal.
Stationary contact sprites resolve these values through the current presented
brush transform; removal/reuse suppresses stale attachments. Free particles
and the shot path retain world coordinates. Game policy outputs bounded
owning beam/glow profiles and clipped impact requests; the existing solid-line,
textured ribbon/sprite renderer and decal owner execute them. Solid-line gain
is a neutral bounded [0,4] RGB emission multiplier, default1 for old effects.
An optional first-materialization clock samples immutable moving segments;
causal action expiry and static hit publication stay unchanged. Stationary
surface effects carry an owning unit normal, never a borrowed collision owner.
The app derives a finite oriented transform and small outward bias. The neutral
frame accepts an explicit surface-aligned override only for additive non-ribbon
instances of view-parallel assets; ordinary source orientation identity remains
strict. This override is signature-tagged and does not disable depth testing.
Moving glows retain their existing billboard behavior. No Gauss condition enters
renderer, collision or movement kernel. Dynamic-hit trace/damage parity is not
claimed. Core-only uses the same seams with `TestGameClientModule`, not HL.
See [E21](GAUSS_PRIMARY_CHARGE_BEAM_E21.md) for bounds, evidence and limitations.

### Optional entity-follow smoke profile

The game API accepts borrowed virtual model/sprite names and returns an optional
owning `EntityTrailProfile`. The session host forwards only while active; default
and alternate modules keep generic ribbons. Half-Life selects the approved RPG
pair. The existing application follow owner latches the profile at committed
event consumption, samples server presentation poses and retains bounded smoke
history. Existing sprite materialization executes animation/scale/fade and the
server-bit-gated head glow. No second physics/renderer loop or asset loader is
introduced; lower targets still have no concrete Half-Life dependency. See the
2026-10-06 E20 follow-up for exact lifetime, bounds and verification.

The subsequent E20 exhaust correction adds an owning optional manifest sprite
name and model-axis offset to that same neutral profile. The app resolves the
existing approved sprite once and publishes a bounded32 attached-sprite list
using the ordinary Studio transform. Smoke/history and head lifetimes remain
separate; absent sprite resources do not reject smoke. Half-Life names/tuning
stay in its module; no alternate loader or game-specific renderer is introduced.

### Verified flat-sky backdrop

The world package builder can retain `WorldFlatSkyBackground` for a validated
uniform opaque special-surface sky, matching across decoded mips. Rendering
executes that neutral RGB backdrop, with no file access or game-name test.
Nonuniform sky textures do not receive a guessed color. Visibility uses PVS
leaf references to authorize candidates, then surface bounds for frustum
rejection; a marksurface need not fit inside its referring leaf's AABB.

### Application preparation and connection clock

`HandshakeSession` construction owns transport/coordinator setup but does not
send a challenge or start its deadline. Its first owning `update(now)` starts
the idle coordinator and pumps it at that same timestamp. Normal visual startup
thus completes SDL/window/OpenGL/audio setup before the bounded protocol clock
begins; slow device initialization cannot consume the entire challenge deadline
without RX updates. Subsequent updates never restart the deadline. This changes
application sequencing, not transport timing rules or the single-loop/module seam.
Early handshake failures publish their exact state in the existing outcome/error
fields instead of being hidden by scripted or prediction coverage. See the E25
2026-10-07 follow-up and the no-stock normal-application startup fixture.

### E27 living-projectile presentation

`GameClientAPI` adds only owning optional command origin/duck pose and neutral
two-component temporary-sprite tint. The normal host freezes the corresponding
prediction-entry pose at immutable command submission. HalfLifeClientModule
owns Snark ID/catalogue/exhaustible eligibility, event mapping, point feasibility
trace20→64/duck correction, THROW/UP/recovery and bounded echo reservations.
Transport/history/collision/Studio/audio/rendering remain generic. No client AI,
monster timer, spawn, bite/damage or score rule crosses into those mechanisms.

Strict TE_BLOODSPRITE decoding now stages both signed sprite tokens, palette
byte, scale and committed provenance atomically. The HAL profile selects a
bounded yellow/red tint; the application resolves approved session sprites,
applies neutral tint and expires its existing temporary pool. No inferred
grenade explosion, fireball or extra sound is produced. Server sounds retain
entity/lifetime/position/channel/pitch through the existing mixer. World Snarks
use ordinary server Studio/Bounce interpolation and current-list removal,
independent of local weapon/thrower life. See
`docs/SNARK_SQUEAK_LIVING_PROJECTILE_E27.md` for exact reference and limitations.

The subsequent E27 pop-visibility correction keeps that same two-component
pool. `TemporarySpriteIntent` adds bounded neutral downward acceleration and
an independent animation-fade switch (defaults0/true preserve old effects).
HAL alone selects the large yellow blood profile; the approved materializer
executes `z = origin.z + velocity*t - gravity*t*t/2`, material coverage and
animation expiry. No sprite filenames, Snark identification, local fuse,
collision or damage rules enter the renderer/host. Alpha blending does not
automatically require smoke fading. Pool32, committed identity, map/session
ownership and strict generation checks remain unchanged.

### E30 committed surface decals and blood-size contract

The protocol adapter publishes existing decal TE subtypes only after atomic
record commit. Neutral observations retain the exact position, server index,
explicit world/brush target and optional brush pose from that same record.
The host forwards the existing ResourceList index/name metadata; the concrete
module alone maps its compiled named dependencies. SDK enums and WAD directory
positions are not network indices, and blood sprites/sounds never infer a decal.

The module exposes a bounded neutral surface-decal preparation recipe. The
GoldSrc asset adapter opens one verified WAD in the selected world's exact root
and converts only explicitly selected palette-tint/index-coverage dependencies.
Generic straight-alpha materials retain tint and static surface lighting;
ordinary masked textures and bullet modulation keep their existing contracts.
The renderer knows no blood names, weapon identity or filesystem paths.

The existing impact owner shares its bounded64-entry pool with committed marks.
Surface selection/clipping uses the received target/position, not a camera shot
trace. Frozen birth pose, existing brush transforms and entity lifetime/birth
floor attach a mark without carrying it into an observed reused slot. World
marks persist across player-life, short-sprite and HUD resets, retiring on
map/resource change or eviction. Unsupported/ambiguous targets are typed skips.

The E30 module profile supersedes the historical large-yellow/guessed-red size
math above, while preserving E27.1 event ownership and TE/sound fallback dedup.
Actual TE size, sprite scale, world bounds, density and lifetime stay separate;
approved SPR/palette data remain unchanged. The renderer applies current-frame
extents and one neutral scale only. Numeric conversion/timing are documented
local-compatible behavior, not a claim of proprietary GoldSrc parity. See
`docs/ORIGINAL_BLOOD_DECALS_SPRITE_SCALE_E30.md` for evidence and limits.

### Bounded per-player presentation evidence

The existing remote snapshot sampler emits neutral numeric decision/reason
metadata beside its result. Logical/resolved continuity floors and actual
position-source ordinals are distinct from the raw pose anchors. Recording
never changes the selected position, clock, alpha or pose. The application
optionally joins these results to immutable RX coordinates and the successfully
published parent Studio instance transform, not a presumed rendered position.
Unknown capture, absent actor, missing materialization and culled instance are
different states; no pixel visibility is inferred from submission.

The existing movement receipt owner hosts a separate opt-in bounded heap
recorder. Every presented frame is analyzed, while a decimated recent ring,
exact before/after event pairs, aggregates and independent manual captures
limit retention. Numeric entity/lifetime/generation keys never carry names,
asset locators or server secrets. JSON encoding happens at receipt publication,
not during rendering; the existing external report remains the only durable
output. Core-only builds use the same neutral diagnostic records without HL.
Evidence describes observed motion and decisions, not causality inferred from
healthy global timing. See the E10 per-player diagnostics follow-up.

The E32 attack-pulse corrective adds exact-source animation-only effect masks
selected by the concrete module from synchronous approved model metadata and
committed observation clocks. The application transports a sorted numeric
table, bounded16x32 and reused on its asset owner. Generic sampling masks only
the qualified row for motion; raw effect fields and animation anchors remain
immutable and visible to the pose policy. A qualified previous endpoint does
not create a second motion floor when its flag clears. All independent position
and lifetime discontinuities, ordinary entities and default/core behavior remain
strict. The renderer knows no effect-bit meaning, attack label or weapon. The
HL module retains separate gait continuity and main-animation restart handling.
