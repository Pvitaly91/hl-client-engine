# E3 local dry movement audio

State: integrated in normal `hlclient.exe`. The first E3 live/manual attempt
is **failed_audible** by `attribution=user_report`; the corrected Release has
not yet been tested live. This development turn started no new
managed/HLDS/Steam/WFP sessions and performed no staging, commit or push.
Historical manual reports are unchanged. `attribution=user_report`:
the user previously confirmed R1 terrain/ladder/viewmodel fixes, E2 sounds,
smooth lift, Use, pickups, HUD and Fast launch; this is not an E3 audible test.

After the first E3 Release, the user reported that no new sound was audible.
The two retained manual runs at 2026-09-29 00:57 and 01:02 showed active
reference prediction (2086 and 868 local steps respectively), but only two
E2 weapon voice starts in each run. Source inspection found a deterministic
integration error: the live loop set its audio clock before
`RuntimeReplaySession` reset the game host, and `GameClientHost::reset()`
discarded that clock. Every later movement observation was therefore dropped
before the Half-Life module. The corrected host retains the clock for its
lifetime across network/map generation resets; teardown still clears it. An
independent regression sets the clock, resets the host as production does,
and verifies a correctly timed cue. Sample warm-up also restarts when the
approved asset root arrives after pre-asset drains. These changes are offline
verified; a subsequent audible live result is still pending user testing.

## Source-derived policy and limits

Reference: pinned Valve SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`,
`pm_shared/pm_shared.c` (`PM_ReduceTimers`, `PM_UpdateStepSound`,
`PM_PlayStepSound`, `PM_CheckFalling`, texture categorization). This is a
dry Half-Life multiplayer presentation slice, not full PM_Move parity.

| State | Cue gate | Cadence | Material / sample | Volume and channel | Suppression |
|---|---|---|---|---|---|
| Supported dry ground | completed local command, positive player velocity; walk 120/run 210 units/s (duck 60/80); zero timer can make initial low-speed decision | 400/300 ms walk/run, +100 ms duck | exact supporting world/brush render face → normalized `materials.txt` key → concrete/metal/dirt/vent/grate/tile, `player/pl_*.wav` | walk/run: ordinary .2/.5, dirt .25/.55, vent .4/.7; duck ×.35; local body | missing/false MoveVars footsteps, multiplayer horizontal ≤220, invalid surface, replay, focus mute |
| Validated R1 ladder mode | actual nonzero climbing speed | 350 ms (+100 ms duck) | `player/pl_ladder1..4.wav` | .35 (duck ×.35), local body | stopped/unsupported mode, MoveVars disabled/unknown, replay |
| Airborne → new supported dry contact | prior downward speed >350 units/s, same life/session | one transition; landing does not reset the entire command history | supporting texture family; >580 additionally `pl_fallpain3.wav` | .85 or 1 body; heavy fall voice 1 | tiny fall, seed/correction/respawn, invalid/wet contact, multiplayer sound mute |

The source's left/right phase advances when the step decision is made, even if
multiplayer policy mutes it. This module uses a deterministic two-variant
choice per logical occurrence and command, **not** stock RNG parity. Tile's
fifth random variant is not selected by this deterministic policy. Unknown
texture names and unknown material codes use the SDK's explicit concrete
fallback; absent/invalid geometry is **not** treated as confirmed concrete.
No string-substring material guesses are made. `materials.txt` is bounded to
128 KiB, entries to 512 and texture keys to 12 characters; first normalized
duplicate wins. The table is owned by one module/session. The approved resolver
reads it once from the exact world resource root during preparation; it is
not opened by the movement callback. Profile WAV tokens are preloaded in small
batches through the existing E1 worker/cache, with no invented network index.

## Ownership and replay

`SurfaceTextureQuery` in generic GoldSrc takes the **actual** ground hit
identity from the completed movement state. It traces only the supporting
world or exact brush model's imported triangles, in that model's current
simulation-time rigid transform. Collision plane indices are never treated as
render-face indices. The query is read-only and cannot affect movement.

The runtime forwards a bounded `MovementAudioObservation` after a committed
20 ms simulation command, through `GameClientHost` to the selected module.
The host changes the steady-clock timestamp to the audio session clock; the
module owns material/cadence/ladder/landing policy and emits neutral owning
`LocalSoundCue`s into the E2 outbox. E1/E2/E3 use one mixer and shared approved
PCM cache. Local body/voice identities do not replace a server entity voice.
There is no time-window suppression of server sounds, since svc_sound has no
verified command ACK correlation. E1 sounds for other players stay independent.

The module retains only 128 command-boundary timer/phase checkpoints. Rebase
restores the matched phase, recalculates the suffix and forbids audible
side effects for replayed commands. A monotonic command high-water prevents a
repeated logical cue from leaving the module; local audio serials provide the
second outbox/sink duplicate guard. Exhausted checkpoint history is counted
and retains the prior timer/side rather than inventing a new first step.
Corrected sounds already heard cannot be withdrawn. Death/life change resets
the local phase, not world audio. Focus mute consumes cues without a backlog.

Prediction `off` or suspended has no sufficiently completed local command at
this seam, so E3 is deliberately silent there; it does not enable physics
prediction or synthesize steps from keys/server position deltas. Missing WAVs
are bounded audio diagnostics, not a network/gameplay failure. Wet movement,
other-player footsteps, full stock random parity, VOX and fall damage remain
outside E3. The currently available dry contact uses the last pre-contact
vertical velocity and validates the airborne-to-ground transition; it is not
a server-confirmed fall-damage result.

Offline fixtures exercise host/module, all dry material families, cadence,
mute, ladder stop, landing, replay, exact world/brush transforms, PCM starts
and independent E1 voices. They do not prove audible hardware output.

Manual command, for the user only:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```
