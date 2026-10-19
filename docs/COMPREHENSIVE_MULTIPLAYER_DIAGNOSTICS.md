# Comprehensive multiplayer diagnostics

## Scope and use

Normal `hlclient` uses the existing `movement_diagnostics` opt-in, default false.
The single launcher checkbox is **Комплексна діагностика гри**. Previously saved
user settings are preserved. In-game F8 keeps its existing meaning. It marks a
bounded four-second pre / one-second post window; exact events are also retained
independently of F8. No per-weapon switches, overlay, second gameplay simulation,
packet parser, server timer, local damage or server settings are introduced.

After normal close, **Відкрити звіт** runs the existing offline analyzer and opens
`multiplayer-analysis.html` beside `external-movement.json`. It uses PowerShell 7
when installed in its standard location; otherwise the original report opens.
The same read-only analysis is available directly:

```powershell
pwsh -NoProfile -File scripts/analyze_movement_session.ps1 -RunDirectory <run-directory> -Json
```

`-PublishSummary` explicitly writes the local HTML summary; `-Open` opens it.
No analysis operation connects to a server. Missing reports/fields and bounded
omissions remain unavailable, not clean gameplay verdicts.

## One ownership chain

| Producer / owner | Facts captured | Join / limits |
| --- | --- | --- |
| Sole SDL pump and application routing | native SDL timestamp, actual steady dequeue, input kind/repeat/mouse delta, focus/capture, console routing and HUD consumption | SDL since-init clock stays separate; no text, console key identities masked; normalized revision joins command input |
| Normal command builder and immutable history | sampled input, due/build/history times, quantized angles, movement, buttons, `msec`, actual `lerp_msec` | actual command ID; scheduler owner time is not an observed clock |
| Transmission and netchan | immutable plan membership, encoding, queued/sent carrier, read/decode/commit bracket, rollback, advanced ACK | explicit command IDs per bounded carrier; ACK means transport only |
| Game host and actual HL controllers | intent, exact accepted action, local rejection gates, charge/beam/reload/throw/deploy/holster lifetime, committed event and cosmetic drains | command/action identity where actually supplied; compatible server state is only a candidate action match |
| Existing local prediction recorder | raw server origin, predicted physical origin, shown eye, replay/correction/support/trace witnesses; base and punch angles separate | exact correction source/query/history witnesses preserved, not reconstructed from snapshots |
| Existing remote presentation recorder | raw source pair, selected sample/time/alpha, final rendered origin, sequence/gait/weapon, support and lifetime boundaries | no local prediction substituted for remote source interpolation; missing packet/pose stays unknown |
| Ordinary Studio evaluation owner | raw sequence/frame/animtime/rate, selected animation input and in-frame pose reuse for non-player models | source clock and shown endpoint remain separate; bounded per-entity observation cache, no repeated pose evaluation |
| Scene submission and swap owner | render frame, submit, swap begin/end, CPU/present timings, framebuffer extent, prior completed frame camera | swap completion is a CPU presentation proxy, not scanout or photon time |
| Read-only action geometry | prior completed frame view ray, immutable command ray, approved MDL hitboxes with the submitted CPU pose, current world/inline BSP obstruction | local proxy only; stale >250ms, absent model, singular transform or exhausted query gives explicit unknown/omission |

Detailed schemas, bounds, clocks, source IDs, enum values and typed records are in
[MULTIPLAYER_TIMELINE_SCHEMA.md](MULTIPLAYER_TIMELINE_SCHEMA.md).
Wire and public server-source semantics are in
[MULTIPLAYER_USERCMD_TIMELINE_CONTRACT.md](MULTIPLAYER_USERCMD_TIMELINE_CONTRACT.md).
The production/fixture/unknown coverage matrix is
[COMPREHENSIVE_DIAGNOSTICS_COVERAGE.md](COMPREHENSIVE_DIAGNOSTICS_COVERAGE.md).

## Geometry and evidence semantics

The action diagnostic sink is observational. It receives accepted actions from
their existing controller, never invokes the action again. The last **completed**
swap retains one immutable CPU scene/frame, not a per-frame scene history. Queries
inspect at most 32 visible primary Studio instances, 1,024 model hitboxes and
4,096 source-record probes, with at most two world/inline-BSP point traces per
accepted action. Hidden/culled entities and attached weapons are not targets.
Bone-space boxes are tested through the actual model and bone affine transforms.
The 8,192-unit ray is an aim-alignment diagnostic horizon, not a claim about
weapon reach, projectile trajectory, server spread, melee hull or damage range.
The source-model result is that same approved MDL / shown pose reference. It is
**not** a source-server rewind pose or proof that the server selected that model.
This distinction matters for customized model replacements.

The retained proxy is refused above 128 Studio instances/poses or 8,192 bone
matrices; those cases retain only the camera and yield unknown model geometry.
The single retained scene shares existing immutable CPU objects; it makes no GPU
readback, asset load, pose re-evaluation or dynamic allocation per observation.
It may extend the lifetime of one already bounded render frame/package until the
next completed frame or generation invalidation. This reference lifetime is
separate from the fixed diagnostic event-buffer byte cap. Reported buffer bytes
must not be described as the total size of those shared imported assets.

View and command origins/directions are distinct. The completed-frame proxy uses
the renderer's same pure screen-effects camera transform, retaining final up,
FOV, clipping planes and framebuffer extent; visual translation is not applied
to the immutable command ray. Current static/inline BSP
occlusion is an approximate local-world reference, not last-scanout depth or the
unknown server collision world. No marker is drawn through a wall and the query
cannot alter aim, spread, damage, collision, projectile or movement state.

## Weapon and world coverage

The supported catalogue comes from the actual HL module's production profile
registry. The terminal report publishes `game_diagnostic_capabilities` (14 weapon
rows, five modes each) and the timeline accumulates observations per weapon,
mode, kind and phase. The analyzer joins them. Unsupported modes are not failed
tests; supported-but-unobserved modes remain unobserved. No live coverage is
claimed from the capability table or from synthetic tests.

Capability command tokens preserve registry case, including the standard
`weapon_9mmAR`. The analyzer accepts only nonempty ASCII letters, digits,
underscore and hyphen, at most 48 characters, for these command names. Capture
reason enums retain their separate lowercase-only validation. Reading an older
report does not rewrite its payload or manufacture missing causal joins.

`tests/test_weapon_diagnostics.cpp` exercises the same host/controllers and emits
a registry-driven Markdown coverage matrix when
`HLCLIENT_WEAPON_DIAGNOSTIC_COVERAGE=1`. The generated evidence file is recorded
under this task's verification directory. Pellet/emitter counts are local
cosmetic emitters, not invented server pellet IDs or RNG. Persistent object
visibility/lifetime classification uses module-owned exact model tokens; an
owner/action relationship is unknown unless committed metadata supplies it.
Visibility disappearance never implies destruction/detonation. HUD health/ammo,
inventory, damage, death/respawn and use intent use committed observation fields;
use intent does not establish which server trigger activated.

## Interpretation rules

Input intent, local action acceptance, command encoding, actual send, transport
ACK, server event, damage observation and cosmetic feedback are separate evidence
levels. A health change without an exact attacker/action key cannot prove which
shot caused damage. Standard protocol messages do not provide authoritative
per-target hit/miss/rewind/action IDs. Those fields stay unknown. A source-model
calculation is labelled `source_model`; synthetic fixture evidence is not live
server observation. Correlation by time/proximity alone is candidate/ambiguous.

The analyzer highlights wire delay mismatches, retained chain gaps, stale shown
proxies, input/aim discrepancies, potential double punch, held remote samples,
GPU/frame stalls and server-visibility limits. These are bounded observations or
suspicions, not automatically assigned root causes. A high FPS count does not
rule out a held remote sample; a slow GPU frame does not establish physics error.

## Identity, rollout and rollback

The authoritative baseline was the dirty **local worktree**, not historic HEAD
`a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`. Before edits, 1,483 ordinary source
and binary files were copied and SHA-256 verified under
`manual-artifacts/task-records/comprehensive-diagnostics-20261018/before` and
`bin-before`, with `prechange.json`. Existing unrelated edits were preserved.
Nothing was staged, committed, pushed, reset, cleaned, restored or stashed.

Run identity remains the launch/process manifests and existing session receipt.
The external report links these manifests; executable/config hashes are observed
outside the frame loop. Diagnostics do not hash or serialize each frame. Legacy
8MiB local memory and 16MiB publication limits remain; a separate bounded 2MiB
event component is included in the memory accounting and within the publication
budget. Sixteen KiB of the final JSON budget is reserved for capability/manifest
metadata. Every eviction, omitted detail and incomplete post-window is explicit.

No new live client, Steam or HLDS session is authorized or run for this task.
Manual end-to-end gameplay confirmation remains `manual=not_run`. The current
saved endpoint and file-consistency permissions are unchanged. Tests use offline
fixtures, fake peers and production renderer controls only. A manual FPS/impact,
lift/remote-player or server hit-registration root cause is not declared fixed
solely because diagnostics were integrated.

## One-session manual workflow

1. Open the current Release `HLClientLauncher.exe`, enable **Комплексна діагностика гри**,
   and connect using the existing saved endpoint. No per-weapon setup is needed.
2. Observe other players, move through a corner/slope/lift, and use whichever
   bullet, projectile, charge/beam or throw/place weapons are available. Reload,
   switch, use and incoming damage are recorded automatically.
3. After a perceived jump, delay or implausible impact press **F8**. Continue for
   at least one second so the post-window can complete, then exit normally.
4. Click **Відкрити звіт**. Preserve the ordinary run folder, including its
   existing manifests and `external-movement.json`; unused weapon modes remain
   `runtime_not_observed`, not failed hooks or manual passes.

This workflow is for the user. The agent did not start a live client/Steam/HLDS
session. Offline verification and final binary hashes are recorded in
[COMPREHENSIVE_DIAGNOSTICS_VERIFICATION.md](COMPREHENSIVE_DIAGNOSTICS_VERIFICATION.md).
