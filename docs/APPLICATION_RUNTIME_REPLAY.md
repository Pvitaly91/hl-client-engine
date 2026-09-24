# Application-owned runtime replay and diagnostic visuals

M4.7.1.2F connects the reference runtime replay session to the ordinary
`hlclient.exe` application update loop. It adds no wire grammar. The input is
still an ordered sequence of owning, reassembled, decompressed server service
payloads, and the session still invokes the A/B/C/D dispatcher documented in
the protocol-specific runtime files.

M4.7.1.2G optionally projects each successfully committed entity observation
into the existing neutral entity package/frame contract. It uses that same
session and `ClientWorldState`; it does not decode a second copy for graphics.

## Explicit offline startup

The positive built-in fixture is selected only by:

```powershell
.\build\bin\Debug\hlclient.exe `
  --renderer null `
  --runtime-replay-fixture basic-mixed
```

`missing-entity-base` is a separate negative fixture. The bytes are our own
public-reference literal fixtures, not a stock capture. The fixture provider
is shared by the E checker, application source and tests, and obtains the
baseline registry by running the B decoder.

Non-visual replay requires `--renderer null`. The explicit visual fixture can
exercise the same projection with NullRenderer:

```powershell
.\build\bin\Debug\hlclient.exe `
  --renderer null `
  --runtime-replay-fixture visual-entities `
  --runtime-replay-visuals diagnostic
```

or present it through the ordinary SDL/OpenGL application path:

```powershell
.\build\bin\Debug\hlclient.exe `
  --renderer opengl `
  --runtime-replay-fixture visual-entities `
  --runtime-replay-visuals diagnostic
```

The fixture and visual mode require one another. Connect, authentication,
basedir/resource, stop-point, view, visibility, brush and camera options are rejected by the
command-line parser before `run()` can construct a socket, authentication
provider, asset manager or filesystem. Without the replay option, existing
application startup behavior is unchanged.

M4.7.1.2H adds a second, mutually exclusive offline input selection:

```powershell
.\build\bin\Debug\hlclient.exe `
  --renderer null `
  --runtime-replay-capture <exact-functional-run-root>
```

The path must name one complete `functional-runtime-capture` publication. It
is not a strict-campaign input and cannot be combined with a built-in fixture,
diagnostic visuals, OpenGL, live connect, authentication or resource modes.
Before the application loop begins, the adapter reuses the structural corpus
loader, delivered-datagram transport replay and captured sign-on replay. It
then transfers the retained server context, capture-owned delta registry and
decoded baseline registry into the existing `RuntimeReplaySession`. Remaining
owning S-to-C service payloads retain their netchan sequence and their original
replay-payload ordinal; no socket, ACK, request or packet is generated.

The bounded final line identifies `source_mode=functional_stock_capture` and
reports the run/corpus identity, transport/reassembly/decompression counts,
sign-on cursor, decoded schema/baseline counts and the source payload ordinal,
netchan sequence and exact bit range of each final neutral substate. Raw bytes,
userinfo and tickets are never printed. A missing, incomplete, altered or
strict-only corpus fails before renderer initialization.

## Application ownership and update scheduling

`RuntimeReplaySceneSource` implements the existing `IClientSceneSource`.
It owns one `ClientWorldState`, one `RuntimeReplaySession`, the ordered owning
records, scheduling counters and terminal result. `world_state()` returns the
same state into which the E session publishes; there is no checker-only or
renderer-side copy.

In diagnostic mode the source additionally owns one
`RuntimeReplayVisualProjection`. Its immutable package is constructed once.
After an entity-bearing record commits, the projector reads the committed
neutral observation, builds and validates a complete frame candidate, then
publishes package and frame atomically into that same world state. A
clientdata-only record advances `presented_runtime_revision` but retains the
same frame pointer, revision and signature. Failure leaves the previous frame
published and terminates the source without rolling back already committed
decoder history.

The existing NullRenderer loop calls `update(FrameTime)` once per application
frame. A call applies at most both configured application limits:

- `--runtime-replay-record-budget`, default 2, range 1..1024;
- `--runtime-replay-byte-budget`, default 1048576, range 1..16777216.

These are project scheduling policy, not protocol maxima. A record that fits
the per-record decoder limits but not the remaining byte budget stays pending
for the next update. A single record larger than the byte budget fails with
`record_too_large`; it cannot remain at the queue head forever. Records are
never reordered, coalesced, silently dropped, or sorted by service opcode,
server time, or transport sequence.

`visual-entities` adds application presentation offsets `0, 1.5, 3, 4.5, 6`
seconds. They expose every intermediate full/delta record without changing its
wire `svc_time`, coordinates, angles or client timers. They are project demo
metadata rather than capture timestamps. `basic-mixed` retains its immediate
bounded-drain behavior.

The literal visual sequence is: a three-entity full packet; an entity-2 origin
change from x=-32 to x=-8; an entity-10 Z rotation from 0 to 90 degrees; removal
of entity 20 plus addition of entity 30 at y=24; and a clientdata-only health
update. Omitted delta entities are retained by the existing snapshot/session
state and explicit removal alone removes an instance.

Application elapsed time advances only `ClientWorldState` presentation time.
It does not replace `svc_time` or modify decoded fields. Non-finite and
negative elapsed values fail. Zero elapsed is valid and still executes one
bounded scheduling update.

## Terminal behavior

The source states are `not_started`, `running`, `completed`, `failed`, and
`stopped`.

- EOF is `completed` only after every record was considered, the queue is
  empty, the last commit completed, and `RuntimeReplaySession::finish()` ran.
- A record error preserves commits from earlier records in that update, rolls
  back the failed record through E, finishes once, and leaves later records
  pending.
- An application frame limit before EOF is `stopped` with
  `application_update_limit_reached`, never successful EOF.
- Explicit stop is distinct from completion.
- Restart requires a strictly newer generation, clears the runtime
  observation/session ownership, publishes an empty visual frame before any
  new record, and starts a new E session. The diagnostic resource package is
  retained rather than rebuilt.

OpenGL visual mode holds the last completed frame for two seconds and then
uses the ordinary application teardown. A window close before EOF is an
explicit incomplete stop; a configured frame limit before EOF is an
application-limit stop. Neither is reported as successful completion. The
headless F EOF behavior has no added hold.

The positive mode exits 0, a replay failure exits 1, command-line rejection
exits 2, and application-limit/explicit incomplete playback exits 3. The
bounded final line reports fixture/profile/generation, input/applied/failed/
pending counts, application updates, session finish count, publication
revision, selected decoded values, independent substate freshness, terminal
state and canonical runtime-state hash.

## Diagnostic binding and renderer evidence

Every replay entity identity is explicitly bound to one project-generated,
asymmetric, textured static-pose Studio asset. The labels are
`visual_binding=diagnostic_fixture`, `asset_source=project_generated`, and
`stock_model_binding=not_verified`. This is demo configuration: it is not a
GoldSrc `modelindex`/precache resolver, does not interpret an index as a path,
and uses no Half-Life installation or native DLL.

Decoded origin and angles are copied exactly into `EntityRenderTransform`;
the existing Studio OpenGL transform convention derives the model matrix.
Material, uniform scale and static pose are project defaults. Missing,
non-finite or out-of-bound decoded transforms are explicit projection errors,
not implicit zero coordinates.

`ClientWorldState::build_render_scene()` forwards the same package/frame as
`RenderDynamicEntities`. NullRenderer validates the contract. OpenGL visual
mode also samples the rendered back buffer before swap and requires a real
context, Studio draws, non-clear pixels, image changes across decoded events,
zero GL errors and a single static Studio asset upload. These checks use no
window-title or text overlay pixels and do not require bit-identical output
between GPUs.

## Scope and evidence

The built-in fixture/visual branch performs no Steam, HLDS, stock `hl.exe`,
network, filesystem, external asset import, HUD, prediction, weapon simulation,
camera tracking, resource download, or live multiplayer work. The
default headless capture-backed branch reads only the explicit, structurally
validated local run root; it does not open sockets, launch processes, inspect
Steam paths, or import game assets. G diagnostic visual mode uses one fixed
project camera. Raw GoldSrc payloads
and delta schemas never cross the client-side projector into the renderer.

H completed functional replay of capture
`ededd068a0444ff497d98204eacec8a4`: 323/323 records and zero failures. Its
historical pre-correction canonical hash was `14031366596970435596`; after
M4.7.2F corrected public signed delta scalars to GoldSrc sign-plus-magnitude,
the same 323/323 replay has canonical hash `7014210005320501317`. The
canonical field set did not change. This is not campaign evidence or
universal stock compatibility. The separate opt-in
`--runtime-replay-visuals local-assets` mode added by I reads only the explicit
local game root and uses a one-time spectator camera; see
[local-asset replay](STOCK_CAPTURE_LOCAL_ASSET_REPLAY.md) for resource ownership,
observed-completion pacing, actual OpenGL results and unsupported categories.
