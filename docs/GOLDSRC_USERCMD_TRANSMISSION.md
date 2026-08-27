# GoldSrc usercmd transmission lifecycle

## Runtime gate

`GoldSrcUserCmdTransmissionStage` is a caller-driven composition of the
synthetic scheduler, input adapter, bounded history, packet planner,
client-move codec, checksum, and the retained `NetchanDriver`. Full operation
requires an explicit `GoldSrcUserCmdSessionPrerequisite` with profile
`synthetic_runtime_ready_v1` and `runtime_ready=true`.

The default `stock_runtime_ready_evidence_pending` prerequisite fails closed in
`signon_evidence_pending`, emits metadata only, and sends no move packet. No
production CLI or composition-root route exposes the synthetic prerequisite to
an arbitrary server. Stock runtime-ready sign-on, checksum, carrier, and
server acceptance remain pending with zero accepted stock runs and zero
verified move packets.

The production `--stop-after usercmd-boundary` route follows the normal stock
challenge, connect/`ACCEPT`, sign-on, resource-list, and opcode-5 client-response
path on one socket and driver. At the explicit post-response handoff it retains
that driver and the authentication lifetime, but exposes no synthetic
runtime-ready prerequisite. It reports sampled/history/transmitted usercmd
counts of zero, labels carrier and checksum pending, and exits nonzero without
constructing or sending a usercmd message.

The stage has no background thread. Its caller supplies the `NetchanDriver`
time point, immutable input intent, and camera state on every update. The
configured stalled-progress timeout is positive and at most 300 seconds. A
newly sampled command or committed move send resets it, so it is not an
absolute cap on a healthy session lifetime. The metadata-event queue is
bounded to at most 1024 entries, and configuration is accepted only when it
can hold the worst configured sampling batch plus one complete move-metadata
transaction.

## Command sampling and history

`GoldSrcUserCmdScheduler` under `synthetic_fixed_step_v1` separates render
updates from command cadence. It can emit multiple bounded sample requests when
the caller advances across several command intervals, carries sub-millisecond
duration remainder, and rejects time reversal, overflow, excessive catch-up,
and project-local sequence exhaustion. The synthetic default interval is 10 ms
(100 Hz) with at most 8 commands per update; the hard catch-up cap is 64 and an
individual configured interval may not exceed 255 ms. None is a stock cadence
claim.

Each successful sample is adapted to a validated immutable
`GoldSrcUserCmdState` and inserted in strictly increasing sequence order into
`GoldSrcUserCmdHistoryBuilder`. The default history holds 64 entries; its hard
cap is 256. A full history may evict only an already-submitted entry outside
the protected backup window. It never evicts an unsent command or a protected
recent command merely to make progress; lack of a safe victim is typed history
backpressure.

Each history entry owns its command and records separate new/backup submission
counts plus the last associated outgoing packet sequence. These are project
submission facts, not server acknowledgements. A queued impulse is attached to
one sample and its `GoldSrcUserCmdOneShotPlan` is committed only when that exact
command identity has entered history. Later packet backpressure therefore does
not silently discard the command containing the one-shot value.

All commands due in one `update()` form one sampling transaction. The stage
advances a staged scheduler, builds into a staged history, and preflights the
bounded sampling events together with the worst-case packet/terminal metadata
reserve. Scheduler state, history insertions and evictions, sample counters,
events, and pending-impulse consumption become visible together only after
every due command succeeds. If retained events temporarily leave insufficient
room, typed nonterminal event backpressure publishes none of those staged
mutations; the caller can drain events and retry the one-shot exactly once.

## Backup and packet planning

`GoldSrcUserCmdPacketPlanner` executes only `synthetic_backup_v1`. `prepare()`
starts at the first command whose new-submission count is zero, selects a
bounded contiguous history range, and walks backward from it to prepend up to
the desired number of earlier commands that were already submitted. Commands
remain oldest first.

This is proactive synthetic redundancy. A dropped move datagram is not queued
reliably or retransmitted byte-for-byte. A later packet is newly encoded for
its own outgoing sequence and can include earlier commands as backups. The
exact stock loss-recovery, history depth, backup count, and new-count policies
remain evidence-pending.

Preparation encodes the complete synthetic message and returns a move-only
`GoldSrcUserCmdPacketPlan` carrying:

- ordered command identities and immutable commands;
- explicit backup/new counts;
- history and planner revisions;
- planner-owner identity and a unique one-shot packet-plan identity;
- the exact outgoing Netchan sequence used by the checksum;
- the owning encoded message and expected bit/byte budgets.

`commit()` accepts only an unconsumed plan from the same planner at the same
revision and atomically updates matching history submission counters.
`abandon()` consumes the plan without updating those counters. Foreign, stale,
reused, overflowed, and missing-history plans fail.

## Sequence-bound Netchan context

The stage never accepts a caller-controlled sequence override. It asks the
same retained driver for a move-only `NetchanOutgoingContextPlan`. That plan is
metadata-only and captures one exact future packet context:

- next outgoing `NetchanSequence`;
- driver context revision and plan identity;
- current reliable decision and payload size;
- reliable and fragmented sequence flags plus optional fragment plan;
- maximum remaining unreliable-payload capacity.

The driver prepares this context from its real `NetchanSession`; it is
unavailable before the first channel acknowledgement, while another
unreliable payload is pending, for a reserved fragment-classifier advance, or
when reliable composition leaves no suffix capacity.

After the packet planner encodes/checksums with the plan's sequence,
`commit_unreliable()` verifies driver ownership, context revision, one-shot
consumption, capacity, exact header, reliable composition, fragment metadata,
sequence, and payload size. A stale context abandons the usercmd packet plan
and leaves its commands unsent for a later fresh context. A foreign or
mismatched context is never coerced into a send.

Once accepted, the driver owns the payload and sends the contextual plan before
receiving another acknowledgement can change its reliable composition. The
`NetchanSession` sequence/reliable transaction commits only after the datagram
send succeeds. The transmission stage then commits the matching usercmd packet
plan to history. This path uses the existing socket/driver; it does not create
a second socket or a usercmd-specific Netchan session. The stage owns no
authentication material or replacement lifetime; the retained driver continues
to own its opaque connection lifetime and its normal once-only cleanup path.

## Update sequence

For a synthetic-ready fake/local session, one update performs these bounded
steps:

1. validate stage, monotonic time, timeout, runtime prerequisite and active
   retained driver;
2. sample due commands, adapt them, insert them into history and commit exact
   one-shot insertion plans;
3. return `waiting_for_next_sample` if no command is unsent;
4. prepare a sequence-bound Netchan unreliable context, or retain unsent
   history under nonterminal unreliable backpressure;
5. prepare and encode the backup/new packet against that exact sequence;
6. reject a message larger than the context's remaining suffix capacity;
7. bind the message to the driver context; retry later if the context became
   stale;
8. update the driver and require exactly one successful packet transmission;
9. commit the matching packet plan to history and return to the next-sample
   state.

Cancellation clears an uninserted pending impulse and cancels the retained
driver. Close is idempotent and closes that driver. Timeout, protocol, history,
and terminal network failures are typed. Unreliable-capacity and metadata-event
backpressure remain nonterminal and retryable; neither is silently treated as
success.

## Diagnostics and privacy

`GoldSrcUserCmdTransmissionEvent` is metadata-only. It may report command/input
identities, new/backup counts, encoded byte/bit counts, changed-field count,
outgoing sequence, and history size. It contains no button mask, movement,
view angles, command bytes, raw packet bytes, authentication material, player
name, or identity data.

There is no public raw-usercmd, raw-packet replay, button injection, forced
opcode, forced checksum, or forced sequence option. Usercmd data is an
unreliable suffix only; simultaneous reliable Netchan state remains owned by
the existing driver and is not used as a reliable usercmd workaround.

## Deliberate milestone limits

The lifecycle records local sampling and packet submission, not authoritative
server execution. It contains no player collision, gravity, acceleration,
friction, movement simulation, command prediction, server-time mapping,
acknowledged-command replay, reconciliation, or stock entity projection.
M4.6.3 and later milestones remain unimplemented.
