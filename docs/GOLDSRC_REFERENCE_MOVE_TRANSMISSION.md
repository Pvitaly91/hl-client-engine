# Reference move planner and local transport (M4.7.2B)

## Policy recorded before implementation

Reuse M4.7.2A's wire type/encoder/checksum without simulation conversion.
Extend the existing history, planner and transmission stage with separate
reference profiles. Only an explicit loopback-test prerequisite authorizes
this stage today; the normal live prerequisite stays fail-closed. A fresh
test peer/session supplies readiness, never a historical capture session.

History owns either synthetic states or quantized reference commands, never
both in one profile. Reference local identity and generation are caller-local
metadata, not wire command numbers or server execution acknowledgements.
Insertions are strictly increasing, bounded, and never evict unsent entries or
the protected backup window. Insertion while a stage plan is pending returns
backpressure so the plan's history cannot change under driver ownership.

Selection starts at the oldest unsent command, with up to the configured new
limit. Prepend the most recent submitted backups in oldest-first order. Our
capacity policy removes oldest backups first, then removes new commands from
the selected tail, until exact encoding fits the retained driver's suffix
budget. At least one new command is required. Never skip an unsent command,
truncate bytes or change quantized values. The compatible wire total ceiling
is 62; configured history/batch/byte limits may be stricter. No Valve cadence
is inferred: the reference entry point accepts already-sampled typed commands.

Reserve sequence and reliable composition with the same NetchanDriver's
outgoing context before encoding. Validate planner/history identity, revisions,
counters and metadata capacity before binding/sending. The existing driver
owns a bound packet until actual local send succeeds or it fails terminally.
A typed transport would-block retains that ownership, does not queue a second
copy, and does not increment submission counters. On success, the exact context
receipt authorizes allocation-free history commit. Any impossible accounting
failure is terminal; successfully sent commands are not silently retried.

Stale pre-bind contexts are abandoned and rebuilt, including selection and
checksum. A new packet's backups are proactive redundancy under our policy,
not reliable retransmission and not proof of delivery or execution. The local
peer validates bytes only; stock authentication/sign-on and acceptance remain
separate work. Capture mode remains read-only.

## API and ownership

`GoldSrcUserCmdHistoryProfile::reference_wire_v1` owns `GoldSrcWireUserCmd`
values under a nonzero generation and explicit `GoldSrcUserCmdSequence` local
identity. Synthetic entries remain available through the original API. A
reference history entry has no synthetic state pointer. Its immutable snapshot
retains profile/generation and a history-owner token.

`GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1` uses the overload of
`prepare()` taking `NetchanOutgoingContextPlan`; the sequence-only overload
rejects this profile. `GoldSrcUserCmdPacketPlan::encoded_bytes()` is the shared
carrier view; `reference_message()` exposes its owning reference result. The
old synthetic `encoded_message()` accessor remains synthetic-only. Every
reference retry reruns selection and M4.7.2A encoding, not a checksum patch.

`GoldSrcUserCmdTransmissionStage::queue_reference_command()` and
`update_reference()` are the typed entry points. They require
`reference_loopback_test_ready_v1`, runtime_ready, an active retained driver,
and both local/remote IPv4 addresses exactly 127.0.0.1. There is no CLI endpoint
or ready/sequence/checksum override. The stage does not run the synthetic
input adapter or scheduler for this path and rejects queue_impulse; impulses
must already belong to one immutable typed command identity.

Reference history-pressure errors are returned by insertion without making
the stage terminal. Pending-plan insertion is rejected before history mutation.
Event capacity is reserved before prepare; planner/history counter preflight
is repeated immediately before binding. History commit uses bounded stack
indexes and existing owning entries, with no allocating work in noexcept commit.

`DatagramSendStatus::would_block` is mapped from the existing socket native
error result. Contextual driver sends retain their packet and return before RX
can change its reserved reliable composition. Other legacy send paths retain
their previous terminal-failure behavior. Temporary send retry is not a new
command or datagram queue insertion. Terminal failure closes the retained
lifetime without reporting a send; an already-observed successful receipt is
accounted even if later receive work terminates the driver.

## Verification harness

The checker option `--self-test-transport` creates two existing `UdpSocket`
instances bound to OS-assigned loopback ports. Its transport decorator uses the
existing IDatagramTransport seam for one injected would-block, forwarding all
successful sends through UdpDatagramTransport. Production history/planner/stage
and NetchanDriver construct and submit every client move. No separate client
sendto path or packet builder substitutes for them.

The small descriptor fixture uses the existing local descriptor factory and
then explicitly binds the reference schema profile. This is test-peer schema
setup, not synthetic command conversion, capture schema substitution or stock
session evidence. Peer expectations are six own commands (neutral, forward,
negative side, angles, full buttons/impulse, neutral), plus the independently
calculated first move literal `02 04 fc 02 19 50 02` at sequence 2. Short moves
are followed by the existing netchan clc_nop padding: the peer validates the
move cursor separately from the whole walked client-message payload cursor.

The peer deliberately consumes but does not decode one datagram and sends no
delivery/execution ACK. Its later backups must still contain the original
commands. Additional in-memory cases cover capacity/history pressure, stale
and foreign plans, generation restart, accounting preflight, terminal failure
and repeated would-block. The wrap case uses the existing NetchanSession initial
state seam and verifies sequence 0x3fffffff -> 0 -> 1 with reference checksums;
it does not add a driver/CLI sequence override or claim a billion-packet run.

## Next client-to-HLDS dependencies (local-code audit)

- Authentication: `prepare_runtime_connect_request()` currently requires an
  explicit material file via `ExplicitFileAuthenticationProvider`. A fresh,
  legitimate provider/session for the actual endpoint is still required; this
  slice neither reads old material nor supplies a native Steam provider.
- Sign-on: the current usercmd stop point retains a driver only at
  `resource_response_boundary_reached`. Stock post-resource continuation still
  stops at its evidence-pending boundary. Offline A-I decoding is not an active
  authenticated sign-on state machine.
- Receive: the existing driver must feed live runtime observations/registry
  and lifecycle handling while sending, not use an offline capture session.
- Handoff: an explicit production runtime-ready capability must connect that
  fresh completed session to the reference stage. The only new prerequisite
  here is loopback-test-scoped; the application usercmd stop still emits zero
  commands. No auth bypass, prediction or weapon execution is introduced.

## Measured verification (2026-09-20)

Primary result: `reference_client_move_transport_integrated`.

Run from `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`:

```powershell
.\build\bin\Release\hlclient_client_move_check.exe --self-test-transport
```

Release, Debug and ASan Debug each produced the same actual UDP result:

```text
queued=6 new_submitted=6 backup_submitted=9
packets_sent=6 packets_received=6 checksum_matched=5 expected_commands=12
dropped=1 would_block=1 retries=1 stale_contexts=1
history_revision=12 cleanup=1 stock_server_acceptance=not_tested
```

These counts exclude the bootstrap ACK. All six raw transport headers were
checked against independent expected sequence/flag/ACK bytes. One received
packet was deliberately left undecoded, so only five move checksums and their
12 command records were checked. The would-block is controlled injection;
successful sends use real local UDP. Nonzero movement/buttons/impulse belong
only to our fixtures, not the stock capture's coverage.

The three affected targets (`hlclient_client_move_check`, `hlclient`,
`hlclient_tests`) built successfully in Release, Debug and ASan Debug. In each
configuration, all 18 focused cases / 9,064 assertions passed, including 11 new
transmission cases. The selected shared regression set had 351 passes and one
skip out of 352: the corpus path-mutation test could not create a directory
symlink on this host. All eight offline application replay smoke tests passed
in each configuration. No ASan Debug diagnostics were reported. ASan Release
was not investigated or claimed.

Read-only capture control retained 768 moves, 768 checksum matches, zero
mismatches, 768 semantic roundtrips, 742 byte-exact roundtrips, unsupported=0,
failures=0 and network_submissions=0. The same 26 nonexact messages retain
redundant unchanged fields; no original-buffer substitution was introduced.
Its checker hash remains `6979080153849632397`. Server-to-client replay
retained 323 applied updates, zero failures/pending updates and 29 entities.
Its historical pre-M4.7.2F signed-wire-correction hash was
`14031366596970435596`; the current sign-plus-magnitude decoding keeps the
canonical field set and produces `7014210005320501317`.

Evidence logs and the pre-change source snapshot are local ignored artifacts
under `manual-artifacts/research-runtime-capture/move-b-prechange-20260920`.
The recorded base HEAD remains `80107832b8667bd0cfb591446c667ad77d729a73`.

Proposed future commit: `Integrate reference client-move planner and loopback
transmission`. Scope only this slice's history/planner/stage and contextual
would-block changes, checker self-test support, transmission tests, CMake
wiring and documentation hunks. M4.7.2A is a prerequisite, not a reason to
stage the entire already-dirty worktree. Preserve earlier A-I and unrelated
launcher/diagnostic changes. No commit, push, stock process, new capture,
authentication operation or research-copy access was performed in this slice.

## M4.7.2E production handoff extension

The later `live-usercmd-check` mode reuses this codec, history, planner and
transmission stage after the same connection has completed the verified D
handoff. Its production prerequisite is generation-scoped and requires the
retained active `NetchanDriver`, current public `usercmd_t` binding,
acknowledged typed `sendents`, and published live state. It is not the earlier
loopback-test prerequisite and does not convert a generic runtime-ready bit
into production evidence.

`LiveRuntimeStage` remains the sole driver-update owner. The transmission stage
may prepare and bind the unreliable move suffix before that update, but commits
new/backup bookkeeping only after observing the driver's exact matching send
receipt. Receive events remain in the driver for the same enclosing update to
drain into the shared runtime decoder. Would-block retains prepared ownership;
it neither reinserts history nor queues a duplicate suffix. The reliable prefix
continues to come from the retained driver context.

The default bounded E schedule is 350 commands at 20 ms: 100 neutral, 50
forward `+100`, 50 neutral, 50 backward `-100`, and 100 neutral. Side/up,
buttons and impulse are zero, with an explicit fixed test orientation. These
are input values, not predicted server speed or distance. Netchan ACK remains
transport progress only; server execution is evaluated separately from fresh
receiving-client `clientdata_t` origin/velocity observations.

## M4.7.2G button extension

The explicit G reference adapter policy admits only typed project jump/duck
actions and maps them to the pinned Valve wire values `IN_JUMP=2` and
`IN_DUCK=4`. The prior E/F scripted movement sources still use the default
no-buttons policy. Each new command records `upmove=0` and `impulse=0`.
The bounded two-bit press latch clears a pending edge only after the exact
command enters history; would-block retries and backup commands retain the
same command identity. Neither Netchan ACK nor a local send alone establishes
button execution. The fixed G source uses the same live driver and decoder,
then checks fresh receiving-client origin, velocity, and view offset with
one-unit minimum changes defined before the managed run. Missing receiving
flags remain unavailable and camera eye remains server origin plus server
view offset. Valve's pinned
[movement source](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c)
retains prior jump/duck buttons server-side; the client does not synthesize
release/press cycles on landing.
