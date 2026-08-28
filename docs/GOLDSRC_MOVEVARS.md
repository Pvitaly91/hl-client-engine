# GoldSrc movement-environment state

M2.4.4 continues from the exact opcode-44 cursor retained by M2.4.3. It
decodes one owning movement/environment metadata state, advances only through
confirmed simple service messages, and stops before the body of the next
unsupported complex message. It does not apply the values to movement,
prediction, rendering, audio, assets, or the filesystem, and it sends no
resource response.

## Evidence and semantic gate

The primary wire evidence is the accepted private IPv4-loopback stock corpus
captured from the signed Valve `hl.exe` launcher (`VERSIONINFO 1.1.1.1`) and
signed Valve `hlds.exe` launcher (`VERSIONINFO 4.1.1.1`) with the Protocol
48/build 10210 server profile. All 16 accepted canonical service payloads from
M2.4.3 reach opcode 44 at the exact delta-parser cursor and independently
produce the same fixed field grammar and length-derived body end. Raw payloads
stay below ignored `manual-artifacts/`; only metadata projections and
independently authored synthetic fixtures may be tracked.

The fresh set contains 28 accepted runs with exact loopback endpoints: six
clean-restart baselines; two runs each for gravity `400`, maximum speed `320`,
acceleration `12`, air acceleration `15`, friction `6`, step size `24`, and
maximum velocity `3000`; two accepted `mp_footsteps=0` runs; two fresh
`stalkyard` runs; two fresh `crossfire` runs; and two deliberately same-process
map-change observations. The controlled cvar variants use RCON set-and-query
confirmation before each client, and each changes only its named body field,
which decodes exactly to the queried value. `sv_footsteps` was not queryable and
was rejected as evidence; the supported `mp_footsteps` control is the accepted
zero/one gate. Rejected/incomplete runs do not count.

The two same-process ordinal-2 runs requested `crossfire` and `stalkyard`, but
their captured ServerInfo map and MoveVars profile both remained
`boot_camp`/`desert`. They are therefore lifecycle/retention observations, not
target-map differential evidence. Only the fresh-process `crossfire` and
`stalkyard` runs support the map-dependent field comparisons below.

After the capture grammar was fixed, the pinned public Valve Half-Life SDK
`pm_shared/pm_movevars.h` supplied a separate semantic cross-check. The 24
captured floats, footstep flag, and sky name correspond exactly to the public
`movevars_s` member set. This satisfies the movement-variable semantic gate,
but the SDK struct is not a wire-layout definition: the captured flag is one
byte rather than an in-memory `qboolean`, and the captured sky string follows
the eight tail floats rather than preceding them. Production code therefore
uses explicit reads and never a packed struct, `sizeof(movevars_t)`, aliasing
cast, or SDK runtime dependency.

The tracked verifier `scripts/verify_stock_movevars.ps1` works only below the
ignored manual-artifact root. It strictly walks the canonical payload from byte
zero through opcodes 8, 11, 54, and all seven opcode-14 schemas, derives the
exact opcode-44 cursor, validates the bounded continuation in memory, and emits
a metadata-only field/evidence projection. It does not copy stock packet bytes,
authentication material, identity data, or game files into the repository.

## Exact opcode-44 wire grammar

Opcode 44 is named `new_move_vars` for the confirmed profile. Its body is:

```text
u8     opcode = 44

f32le  gravity                 body offset  0
f32le  stop speed              body offset  4
f32le  maximum speed           body offset  8
f32le  spectator maximum speed body offset 12
f32le  acceleration            body offset 16
f32le  air acceleration        body offset 20
f32le  water acceleration      body offset 24
f32le  friction                body offset 28
f32le  edge friction           body offset 32
f32le  water friction          body offset 36
f32le  entity gravity          body offset 40
f32le  bounce                  body offset 44
f32le  step size               body offset 48
f32le  maximum velocity        body offset 52
f32le  z maximum               body offset 56
f32le  wave height             body offset 60
u8     footsteps (0 or 1)      body offset 64
f32le  roll angle              body offset 65
f32le  roll speed              body offset 69
f32le  sky color red           body offset 73
f32le  sky color green         body offset 77
f32le  sky color blue          body offset 81
f32le  sky vector x            body offset 85
f32le  sky vector y            body offset 89
f32le  sky vector z            body offset 93
char   sky name + NUL          body offset 97
u8     exact next opcode       not consumed by the single-message parser
```

There are no captured reserved or padding bytes. Body size is
`98 + sky_name_length`; the accepted `desert` profile therefore consumes 104
body bytes and 105 bytes including opcode 44. All multi-byte numeric fields
are little-endian IEEE-754 binary32. The parser reads an explicit `u32le`, uses
`std::bit_cast<float>`, preserves the exact finite value (including positive
and negative subnormal encodings), and rejects NaN, positive/negative infinity,
and values outside the broad protocol safety bound of 1,000,000 in magnitude.
Signed zero remains valid and is preserved. The flag accepts only the
independently confirmed boolean values zero and one.

## Field evidence table

Every semantic getter below has two independent evidence sources: its exact
captured field geometry/value and the matching public Valve SDK member.
Controlled single-cvar projections, where available, provide the stronger
`controlled_single_field` basis. Crossfire changes the six sky numeric fields;
stalkyard changes those six fields and `sky_name`. No semantic name is inferred
from struct position alone.

| Body offset | Public semantic getter | Width / encoding / endian | Accepted observation and scenario | Confirmation basis / variability | Confidence / exposure |
| ---: | --- | --- | --- | --- | --- |
| 0 | `gravity()` | 4 / IEEE-754 binary32 / little | baseline `800`; gravity variant `400` | `controlled_single_field`; dynamic by cvar | confirmed / yes |
| 4 | `stop_speed()` | 4 / IEEE-754 binary32 / little | `100` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 8 | `maximum_speed()` | 4 / IEEE-754 binary32 / little | baseline `270`; max-speed variant `320` | `controlled_single_field`; dynamic by cvar | confirmed / yes |
| 12 | `spectator_maximum_speed()` | 4 / IEEE-754 binary32 / little | `500` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 16 | `acceleration()` | 4 / IEEE-754 binary32 / little | baseline `10`; acceleration variant `12` | `controlled_single_field`; dynamic by cvar | confirmed / yes |
| 20 | `air_acceleration()` | 4 / IEEE-754 binary32 / little | baseline `10`; air-acceleration variant `15` | `controlled_single_field`; dynamic by cvar | confirmed / yes |
| 24 | `water_acceleration()` | 4 / IEEE-754 binary32 / little | `10` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 28 | `friction()` | 4 / IEEE-754 binary32 / little | baseline `4`; friction variant `6` | `controlled_single_field`; dynamic by cvar | confirmed / yes |
| 32 | `edge_friction()` | 4 / IEEE-754 binary32 / little | `2` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 36 | `water_friction()` | 4 / IEEE-754 binary32 / little | `1` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 40 | `entity_gravity()` | 4 / IEEE-754 binary32 / little | `1` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 44 | `bounce()` | 4 / IEEE-754 binary32 / little | `1` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 48 | `step_size()` | 4 / IEEE-754 binary32 / little | baseline `18`; step-size variant `24` | `controlled_single_field`; dynamic by cvar | confirmed / yes |
| 52 | `maximum_velocity()` | 4 / IEEE-754 binary32 / little | baseline `2000`; max-velocity variant `3000` | `controlled_single_field`; dynamic by cvar | confirmed / yes |
| 56 | `z_maximum()` | 4 / IEEE-754 binary32 / little | `4096` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 60 | `wave_height()` | 4 / IEEE-754 binary32 / little | `0` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 64 | `footsteps()` | 1 / unsigned enum 0-or-1 / n/a | baseline `1`; `mp_footsteps=0` variant | `controlled_single_field`; dynamic by cvar | confirmed / yes |
| 65 | `roll_angle()` | 4 / IEEE-754 binary32 / little | `2` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 69 | `roll_speed()` | 4 / IEEE-754 binary32 / little | `200` in the accepted set | `exact_capture_plus_pinned_valve_header`; stable | confirmed / yes |
| 73 | `sky_color_red()` | 4 / IEEE-754 binary32 / little | boot_camp `360`; crossfire `210`; stalkyard `120` | `exact_capture_plus_pinned_valve_header`; dynamic by fresh map | confirmed / yes |
| 77 | `sky_color_green()` | 4 / IEEE-754 binary32 / little | boot_camp `318`; crossfire `205`; stalkyard `127` | `exact_capture_plus_pinned_valve_header`; dynamic by fresh map | confirmed / yes |
| 81 | `sky_color_blue()` | 4 / IEEE-754 binary32 / little | boot_camp `245`; crossfire `183`; stalkyard `172` | `exact_capture_plus_pinned_valve_header`; dynamic by fresh map | confirmed / yes |
| 85 | `sky_vector_x()` | 4 / IEEE-754 binary32 / little | boot_camp `0.258819`; crossfire `-0.26496`; stalkyard `-0.122788` | `exact_capture_plus_pinned_valve_header`; dynamic by fresh map | confirmed / yes |
| 89 | `sky_vector_y()` | 4 / IEEE-754 binary32 / little | boot_camp `0`; crossfire `0.424024`; stalkyard `0.122788` | `exact_capture_plus_pinned_valve_header`; dynamic by fresh map | confirmed / yes |
| 93 | `sky_vector_z()` | 4 / IEEE-754 binary32 / little | boot_camp `-0.965926`; crossfire `-0.866025`; stalkyard `-0.984808` | `exact_capture_plus_pinned_valve_header`; dynamic by fresh map | confirmed / yes |
| 97 | `sky_name()` | variable + 1 / stock-observed ASCII + NUL / n/a | boot_camp/crossfire `desert`; stalkyard `night` | `exact_capture_plus_pinned_valve_header`; dynamic by fresh map | confirmed / yes |

There are no opaque body fields in the confirmed opcode-44 profile. The
post-message opcode and all later bodies are separate stream elements rather
than hidden trailing movevars data.

The sky name uses a bounded NUL search into an owning `std::string`. Stock names
were ASCII, while the parser deliberately preserves any non-NUL byte and
sanitizes only at presentation. The default content limit is 64 bytes and the
hard configuration cap is 256. It is not normalized, joined to a local root,
interpreted as an asset path, opened, passed to a shell/URL, or sent to
renderer/audio code. Failed reads preserve the caller's cursor and publish no
candidate state.

## Exact stream continuation

The stock service payloads place the following messages immediately after the
opcode-44 body. Each decoder starts at the exact cursor returned by the prior
decoder; there is no opcode scan or resynchronization.

| Opcode | Confirmed bounded body | Stock observation |
| ---: | --- | --- |
| 32 | two `u8` values | one message |
| 5 | one `u16le` value | one message |
| 39 | `u8` id, `i8` declared size, fixed 16-byte NUL/padded name | 35 or 37 messages |
| 9 | bounded NUL-terminated text | three messages |

Opcode-39 declared size is an explicit signed byte. Stock `0xff` decodes to
`-1`, matching the public Valve `REG_USER_MSG(..., -1)` variable-length form;
it is never surfaced as the unsigned value 255.

For the common short-prefix baseline, opcode 44 begins at service-payload
offset 6,388, its exact next opcode 32 is at 6,493, and the neutral boundary
opcode 13 is at 7,273. The historical first-client reference begins opcode 44
at 6,393 and reaches the same relative boundaries five bytes later. In the 28
fresh accepted projections, 26 reach opcode 13 at offset 7,273; the two
`night` projections reach it at 7,272 because their sky string is one byte
shorter. Every fresh projection retains exactly 201 bytes after the boundary
opcode. Its body is deliberately unconsumed because it is the first complex
message whose grammar is outside the confirmed M2.4.4 continuation.

Opcode 44 as the final byte, truncated simple controls, a duplicate opcode 44,
and any other unknown opcode before the boundary fail transactionally. A byte
that merely resembles a supported opcode inside an unknown body is never used
to recover the stream.

## Resource-list status

No accepted first stock service batch contains opcode 43. M3.1.1 now parses the
opcode-13 sequence after this layer and proves exact first-batch end. A bounded
stock relay separately completed the later six-fragment transfer: its
decompressed payload starts with opcode 45, has eight bytes before the next
exact cursor, and reaches opcode 43 at offset 9 with the opcode-43 body
untouched. Stock-client traffic requests that later continuation with the
independently confirmed nine-byte `sendres` request; it is not the next server
message in the retained first batch. The pinned public Valve SDK supplies no
numeric service constant mapping opcode 43, so the strict resource-list
semantic gate remains unsatisfied. The M2.4.4 stop itself is still explicitly
forbidden from sending the request.

M2.4.4 therefore exposes an honest `PostMoveVarsBoundary` at opcode 13 rather
than pretending that the first-batch cursor is a `ResourceListBoundary`. The
boundary owns the numeric opcode, exact byte offset, and remaining byte count;
its category/evidence-status accessors identify the fixed supported profile.
The enclosing `MovementEnvironmentSignonState` owns the prior source metadata.
`MoveVarsState` stores the compatibility profile and exposes the fixed evidence
profile for this supported capture/header combination. Neither state owns or
parses the boundary body. The separately observed numeric opcode-43 candidate
body also remains entirely unparsed and is not exposed as a
`ResourceListBoundary`.

M3.1.1 therefore exposes neutral `Opcode43Boundary`, not
`ResourceListBoundary`, after the separately typed request/control lifecycle.
No resource count, entry, consistency data, local resolution, download,
precache, or resource response exists there or here. See
[GoldSrc user info](GOLDSRC_USERINFO.md) and
[GoldSrc resource transition](GOLDSRC_RESOURCE_TRANSITION.md).

## Ownership and isolation

The movement/environment state is immutable, owning protocol metadata. It has
no pointer to the service payload, socket, driver, renderer, filesystem,
`ClientWorldState`, or SDK struct. The same retained UDP transport,
`NetchanDriver`, decompressed service payload, exact server endpoint, and
optional authentication lifetime are carried from the delta stage; no second
socket, second `new`, packet rewrite, decompression pass, or server-info/delta
reparse is introduced.

All parse and stage publication is transactional. Event capacity is preflighted
before the state, controls, and boundary become visible. Failure, timeout,
cancellation, driver error, or backpressure publishes no partial state and
closes the retained driver/authentication lifetime exactly once.

## Typed API and stage

`MoveVarsParser::parse()` accepts a bounded payload span and the exact opcode
byte offset; the stage retains the owning source payload while decoding.
Success returns an independently owning `MoveVarsState`, `bytes_consumed`, and
the exact next byte offset. Failure returns a typed diagnostic and zero
consumed bytes. `MoveVarsStreamDecoder::decode()` accepts the M2.4.3
`PostDeltaBoundary`, validates its opcode/bit/remaining-byte geometry, invokes
the single-message parser once, and builds an owning ordered vector of
`PostMoveVarsControl` plus `PostMoveVarsBoundary`.

`MovementEnvironmentSignonState` owns the prior
`DeltaDescriptionSignonState` and complete movevars stream state. The
`MovementEnvironmentStage` nests `DeltaDescriptionStage` in a private
retention mode and publishes only three bounded event kinds:

- `movement_environment_ready`;
- one `post_environment_control` for each confirmed control;
- `post_environment_boundary`.

Consistent with the earlier sign-on stages, this queue contains successful
owning metadata only. Unsupported messages, timeout, cancellation,
backpressure, network failure, and protocol failure are distinct typed terminal
states/errors and trace classifications; they do not enqueue a partial or
second failure record. Thus every failed publication leaves the metadata event
queue empty.

The terminal success state is `post_environment_boundary_reached`; the
coordinator maps it to `movement_environment_boundary_reached`. Unsupported
exact opcode, timeout, cancellation, network/protocol failure, secondary
stream, and event backpressure remain distinct terminal outcomes. Trace events
contain confirmed numeric fields, a callback-lifetime sky-name view, and
cursor/count metadata; opcode-9 text and raw payload/body bytes are never
included. Presentation of the sky name is terminal-sanitized.

Project safety limits are configuration bounds rather than stock maxima:

| Limit | Default | Hard cap |
| --- | ---: | ---: |
| sky-name bytes | 64 | 256 |
| post-control string bytes | 1,024 | 4,096 |
| post-movevars controls | 64 | 256 |
| stage events | 64 | 256 |

The stock first-batch profile needs 42 or 44 events depending on the 35/37
opcode-39 definitions, so the default event capacity covers both observed
forms. Exact-limit, limit-plus-one, and transactional backpressure paths are
project tested.

The explicit composition-root route is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after movevars `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

There is no CLI option to set, override, apply, skip, or inject raw movevars,
and the existing earlier stop points preserve their behavior.

## M3.1.1 continuation

The separate `--stop-after user-info` route retains the exact M2.4.4 payload,
parses one or more consecutive opcode-13 messages, requires exact first-batch
end, and sends nothing. `--stop-after resource-list-boundary` may then retain
the same socket/driver/authentication lifetime, queue only the exact typed
nine-byte transition request, decode the later `BZ2\0` payload and fixed
opcode-45 control, and terminate at neutral opcode 43. The CLI spelling does
not imply that a typed resource-list body exists. Opcode 43 is validated at
the exact cursor and remains unconsumed; all body bytes remain unread and
unparsed, and no response is sent.

## M4.6.3.2 movement environment

`GoldSrcMovementEnvironmentBuilder::from_move_vars` is the first explicit
consumer of validated `MoveVarsState`. The executable
`movevars_dry_walk_subset_v1` environment copies and validates:

- executed: gravity, stop speed, maximum speed, acceleration, air
  acceleration, friction, step size, maximum velocity and entity gravity;
- retained but deferred: water acceleration, water friction, edge friction,
  bounce, z maximum and wave height.

All fields must be finite and within the configured safety magnitude. Gravity,
maximum speed, maximum velocity and entity gravity must be positive; the other
executed coefficients must be non-negative. Invalid captured values receive no
fallback defaults. Source MoveVars compatibility/evidence profiles remain
attached to the immutable environment.

The offline checker/viewer instead asks explicitly for
`project_owned_offline_baseline_v1`, with its own evidence profile. That path
does not claim that a server supplied the values. See
[GoldSrc local movement](GOLDSRC_LOCAL_MOVEMENT.md).

## Deliberately absent

- full `PM_Move`, client prediction, reconciliation, water/ladder movement or
  server-authoritative physics;
- renderer clipping/sky application, audio/environment application, or world
  mutation;
- a resource-list body parser, response producer, download, cache, or precache;
- filesystem, asset-manager, URL, shell, SDL, OpenGL, or game-data coupling;
- raw payload/body logging or a CLI that injects/skips/overrides movevars.

M4.6.3.1 added the separate immutable BSP collision package. M4.6.3.2 now
combines that package with the validated dry-walk MoveVars subset inside a
local pure kernel. The original sign-on stage remains metadata-only and does
not mutate movement, camera, renderer or network behavior.
