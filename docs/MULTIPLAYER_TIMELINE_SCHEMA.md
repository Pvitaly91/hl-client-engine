# Shared multiplayer timeline: schema and evidence limits

`external-movement.json` retains `hlclient.external-movement.v2` and all existing
movement, priority-capture, exact-correction, remote-player, and frame-pacing
sections. The additive `multiplayer_timeline` section is independently versioned
`hlclient.multiplayer-timeline.v1`. The offline analyzer explicitly rejects an
unknown timeline version; absence means an older build, not zero events.

## Ownership and clocks

`MovementIncidentRecorder` implements the common core sink. Its timeline is an
owned component, not a second application logger. Producers provide fixed-size
owning DTOs at their existing production boundaries; collection never repeats a
decode, weapon action, physics step, gameplay trace, draw or sound dispatch.
The explicitly isolated geometry observer may issue the documented bounded
read-only diagnostic probes; they never enter gameplay results. A null or
disabled sink does not acquire timestamps or construct producer diagnostics.

`local_steady_ns` is an actual `steady_clock` epoch observation. Input SDL ticks
remain `sdl_timestamp_ns` in SDL's since-initialization domain. The explicit
`application_steady_origin_ns` maps the existing receipt-relative microseconds
and F8 marks by `origin + us*1000`; no server timestamp participates in this
conversion. Logical scheduler `deadline_ns`/`owner_time_ns` are not independent
clock readings. Server, command and presentation seconds remain separately
identified; absent fields are `null`, never invented zero observations.

The optional clock-mapping payload has a revision, local anchor, server anchor
and uncertainty. Recording such a mapping does not establish symmetric network
latency, server rewind time, physical key press or monitor scanout. Socket read
and swap completion are their observed API boundaries only.

The current normal path does not emit an estimated server/local clock mapping.
Mapped one-way latency and mapped remote sample age are therefore unavailable.
For an accepted correction, `simulation_iteration` is the existing nonzero
`after_prediction_epoch`: an owner replay-pass/generation identity, **not** a
new global counter or an individual simulator substep number. Its command and
source identities remain separate. Rejected/unknown/zero epochs and frame-only
movement observations leave this field absent. The existing exact correction
witnesses retain their finer source/history fields; there is no universal
per-simulation-step causal join.

A committed record carries `server_seconds` only when a fresh `svc_time` has
matching generation, record identity/ordinal and transport provenance. A retained
previous server time is not copied to a later sound/control-only record. Even a
fresh clock observation does not synchronize the server clock to local commit
time or establish network uncertainty.

`frame_ms` is the observed interval between successive completed swaps, absent
for the first frame. It is not CPU render time or scanout duration. Async
`gpu_ms` comes only from an already-ready valid renderer query, carrying the
original submitted frame and network/map identity, not the frame that polls it.
An eight-entry numeric ownership table mirrors the existing bounded query pool;
unknown ownership, pending/disjoint/invalid results never fabricate zero GPU
duration. No new query, blocking wait or readback is introduced.

## Identities

The enclosing report owns run/process identity; the terminal publication adds
the executable identity and manifest references. Each event separately records
producer and producer-event ordinal; network/map/local-life generation; actor
entity, occupant/visual lifetime and model revision when actually available;
input sequence; immutable command; action; carrier packet; source sequence,
record identity, record ordinal, message bit offset and entry/event ordinal;
simulation iteration; rendered frame, scene, pose and query identity.

Unknown identities remain absent. A model index is not a model revision, a player
slot is not an occupant lifetime, and a packet ACK is not an executed command.
The producer's ordinal is unique within its producer/network/map lifetime;
repeated or older ordinals do not become new events. Older generations cannot
publish into a newer producer lifetime. Equal positions, timestamps or weapon
types do not deduplicate distinct accepted actions.

Correlation uses explicit generation-qualified IDs. A nearest-in-time server
event or projectile is never promoted to an exact action/shooter association.
Persistent-object visibility changes are not proof of spawn, destruction or
pickup. Their owning IDs may outlive the local player's life.

The normal application's receipt owner supplies the canonical diagnostic
network/map pair to the game host, command and completed-frame producers. The
game module's private map/reset epoch is a different domain and is unchanged.
The host binds the first nonzero canonical pair in its current reset lifetime;
an unavailable owner, different network, or a changed map before host reset
emits an unknown map (zero), not a relabelled old action. Reset clears this
binding; detaching the sink clears the borrowed provider. Diagnostics OFF does
not read the provider. Historical reports with mismatched generations are not
repaired by joining equal command IDs across maps.

## Payloads

Payload `type` ordinals are stable within v1:

| Type | Owning observation |
| --- | --- |
| 0 | Command: final quantized turns, movement, buttons, duration, lerp, build/history timestamps |
| 1 | Network: plan/send/read/decode/atomic commit/rollback/transport ACK, exact bounded command membership |
| 2 | Action: module-defined weapon/mode/kind, phase, actual available ammo/eligibility, immutable aim context |
| 3 | Lifecycle/state: observed transitions, inventory/health/object visibility and explicitly known owner |
| 4 | Presentation: origin/pose sample clocks, submitted frame and measured CPU/GPU/present scopes |
| 5 | Explicit clock-mapping estimate with uncertainty |
| 6 | Isolated read-only geometry observation; local proxy, never server collision authority |
| 7 | Input ownership: SDL event metadata, dequeue time, focus/menu/HUD routing; no text payload |
| 8 | Local movement/correction: raw physical, predicted and displayed coordinates kept separately |
| 9 | Remote presentation: actual selected source pair, sample, rendered transform, lifetime and support |

Enum declarations are authoritative in
`include/hlclient/core/multiplayer_diagnostics.hpp`. `provenance` distinguishes
unknown, runtime-observed, derived estimate, source model, synthetic and user
report. `knowledge` distinguishes intent, local acceptance, command encoded/sent,
transport ACK, server event/damage/miss evidence, local proxy and cosmetic output.
`edge` distinguishes unknown, exact, candidate and ambiguous associations.

The collector rejects a transport payload claiming server damage/miss/execution
evidence, a cosmetic/proxy payload promoted to server damage/miss, nonfinite
vectors/times, unbounded command membership, malformed reason tokens and a
rollback presented as atomic publication. Rejected diagnostics do not change
gameplay or transport. A valid server fire event alone still does not identify a
damaged target. Missing blood, server echo or DeathMsg does not prove a miss.

## Bounded retention and publication

Ordinary Studio models have a separate `ordinary_studio` producer using type 4.
It captures the existing owning raw `raw_animation_sequence`,
`raw_animation_frame` (network 0..256 coordinate), `raw_animtime_seconds`
(server clock), signed `raw_framerate`, and complete controller/blend bytes when
observed. `sequence`/`animation_frame` are the selected pose-evaluation input,
whose frame coordinate is in the model sequence's keyframe units; `pose_seconds`
is the actual animation sample clock. `cache_hit` records existing in-frame pose
reuse, not a diagnostic cache hit. This receipt does not claim scene admission
or a completed display. Missing fields, model revision and renderer visibility
stay unknown rather than being derived from resource slots.

The ordinary producer uses at most 128 small numeric suppression entries,
separate from the event-buffer cap. Repeated samples are limited to one per
125 ms per retained entity identity; sequence, visual lifetime and pose-reuse
decision changes bypass that suppression. `suppressed_repetitions` describes
omitted observations since that entry's last emission, while cumulative
`diagnostic_cache_evictions` exposes bounded round-robin replacement. This is
sampled metadata, not a lossless animation history, and does not reevaluate poses.

The existing detailed movement-memory cap remains 8 MiB. The shared component
has an additional **2 MiB** compile-time-checked allocation, made once at opt-in.
It owns 1,280 recent events, 384 exact action/lifecycle/query witnesses, 128 exact
command/carrier/input-edge witnesses, eight manual windows of at most 64 events,
bounded sampling and producer identity tables, and 256 action-coverage rows.
Its hot path performs no allocation, serialization, disk I/O, hashing or clock
reads. These limits are not promises of lossless recording at arbitrary rates.

All valid observations update unthinned counters before background sampling.
Normal background sampling is 50 ms per bounded key. Exact accepted transitions
do not wait for F8. Command-history insertion and actual sent-packet witnesses
have a separate pool so held fire/network traffic cannot evict every action.
Each pool uses deterministic oldest-event eviction with explicit counters.
Repeated action rejects are bounded at the producer and expose suppressed counts.

The existing F8 mark also opens a **4 s pre / 1 s post** shared window. Manual
background sampling is 125 ms; exact actions outrank command/carrier witnesses,
which outrank backgrounds. Equal-priority overflow keeps observations nearest
the marker. Requested start/end, actual retained start/end and omissions are
separate. `post_complete` means the collector observed time through the deadline,
not that every intervening event was retained. Normal death/loading never clears
this shared manual window. A complete five-second command chain is not promised
by a 64-event window: missing links remain explicitly unknown in analysis.

The shared JSON is bounded to 2 MiB and encoding stops adding events after a
100 ms terminal-work deadline. Manual, exact and recent details are encoded in
that priority order; omission/deadline counters remain visible. Legacy frame
detail reserves room for this section, remote summaries and structural metadata.
The whole report remains bounded to **16 MiB**; recorder output reserves a further
16 KiB for terminal manifest/module-capability metadata. Root publication remains
the existing asynchronous bounded receipt workflow, not a new frame-loop writer.

## Offline interpretation

### Alive-life prediction recovery

Detailed movement frames add `life_wait_started_us`, `life_wait_duration_ms`,
`recovery_wait_reason`, and `candidate_anchor_command`. The timestamp is in the
existing application-relative steady-clock domain. The life wait starts at a
committed respawn boundary, not the initial connection, and freezes at the first
accepted prediction seed;
it is not the legacy `fallback_duration`, which can include death. Unobserved
timestamps/durations/commands remain `null`. No collector calculates a life
boundary or a missing latency from unrelated stamps.

`anchor_command` is populated only after prediction history accepts that seed,
not merely when the carrier ledger binds a candidate. It is cleared at a life
boundary rather than reused for the next life. A candidate can be
bound to an actual sent carrier but still be rejected by the life floor or
missing jump/duck history; its presence is not successful seeding or server
execution. Current reasons are `none`, `waiting_for_carrier`,
`carrier_before_life_floor`, `jump_duck_history_required`, and
`seed_or_collision`, and `session_ended`. If the session ends before a seed is
accepted, the timer freezes at cleanup with `session_ended`; that does not claim
successful recovery. A successful life-wait duration remains frozen across later
ordinary corrections in the same life. Producers reset life-scoped evidence at generation/life
boundaries; the recorder owns each submitted frame without filling absent
fields from the previous frame.

The detailed recorder still has a hard **8 MiB** retained-data budget, a
961-frame recent ring, eight incident slots, and a 480-frame incident upper
bound. `limits.incident_frame_capacity` reports the actual per-incident bound,
derived from the compiled frame size after reserving all sidecars. Added frame
facts reduce that capacity rather than increasing the memory limit. Existing
exact-marker priority, requested F8 time windows, sampling and explicit
omission counters remain unchanged; the hard upper bound is not a promise that
all 480 frames or every requested timestamp is retained.

`scripts/analyze_movement_session.ps1` reads both older and new reports. It builds
action chains only from explicit immutable-command and carrier membership IDs.
Unknown server execution/damage remain unknown even when every client link is
present. Actual module capability rows drive weapon/mode coverage; a mode with
zero observations is `runtime_not_observed`, not a failed hook or manual pass.

Versioned negative controls exercise: transport/cosmetic/proxy authority misuse;
unknown clock domain; wire lerp versus declared effective interpolation; same-
record pose/origin sample mismatch; configured/effective delay disagreement;
immutable-command/action aim mismatch; measured GPU/present intervals; a candidate
prediction hold despite source motion and high render cadence; local world blocker;
and expected lifecycle boundaries that do not suppress unrelated failures.

These are bounded observations and classifications, not universal root-cause
proof. In particular the analyzer does not call a configured/effective mismatch
proof of double interpolation, or an aim mismatch proof of double punch. It
reports those alternatives as missing evidence. Selected event details do not
produce exact whole-session FPS/percentiles; existing unthinned summaries retain
that responsibility. Unknown server internals, physical input/scanout and missing
causal links are explicit limitations, not guesses.

Default analysis is read-only. `-PublishSummary` explicitly writes one bounded,
HTML-escaped `multiplayer-analysis.html` in the selected run directory; `-Open`
requires that switch and opens only that fixed generated artifact. No live client,
Steam, HLDS or server connection is initiated by analysis.

## Verification

`tests/test_multiplayer_timeline.cpp` covers OFF, exact identities, rejected false
authority, generation lifetimes, bounded storage/encoding, F8 survival through
death, schema fields and a warm immutable-command ON/OFF overhead measurement.
The opt-in `HLCLIENT_MULTIPLAYER_TIMELINE_FIXTURE_OUTPUT` writes the actual recorder
JSON once to a non-existing test path. Supplying that file as
`HLCLIENT_MULTIPLAYER_TIMELINE_FIXTURE_INPUT` exercises the real serializer-to-
PowerShell analyzer seam in `scripts/test_movement_session_analysis.ps1`.

Timing output is per observed fixture event and is not a live FPS estimate.
Focused/full suite results and measured numbers belong to the task's build
evidence, not this schema's static claims. New manual gameplay validation remains
`not_run` until the user performs it. No original-HL parity or guaranteed hit
registration follows from successful schema/fixture tests.
