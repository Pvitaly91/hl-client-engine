# Local prediction history

`LocalPredictionHistoryState` is the immutable publication boundary for local
movement prediction. It retains one anchor plus the exact ordered commands
that have not yet been acknowledged by synthetic authority:

```text
PredictionHistoryAnchor after acknowledged command N
    + PredictedCommandEntry N+1
    + PredictedCommandEntry N+2
    + ...
    = current predicted movement state
```

It is independent of `GoldSrcUserCmdHistoryState`. Network transmission history
owns sent/new/backup planning metadata; prediction history owns movement
pre/post states and the authority/replay lifecycle. Neither history controls
the retention policy of the other.

## Anchor

`PredictionHistoryAnchor` owns a shared immutable movement state, a typed
`AuthoritativeCommandAcknowledgement`, optional accepted authority ordinal and
authority-state signature, its deterministic movement-state signature, and the
full `PredictionSessionIdentity`.

`LocalPredictionHistoryState::create_initial` creates a command-zero anchor
with `AuthoritativeCommandAcknowledgement::none()`. After successful normal
reconciliation, the anchor is the authoritative state after the exact
acknowledged command. A hard reset replaces it with a command-zero state in a
new prediction generation.

## Entries

Each immutable `PredictedCommandEntry` retains:

- one shared immutable `GoldSrcUserCmdState` and its exact sequence;
- shared immutable pre-command and post-command movement states;
- deterministic pre/post state signatures;
- movement simulation statistics;
- a bounded `PredictionTouchSummary`;
- prediction generation and stable entry ordinal.

The touch summary contains only count, first/last hit category, solid flags,
deterministic signature and accounted byte size. Full touch arrays remain
frame-local. Repeated wall contact therefore cannot create an unbounded touch
history.

`PredictedCommandAppend` is the staged input to
`append_local_prediction_commands`. Success publishes a replacement history
and final predicted state; failure leaves the input history unchanged.

## Ordering and exact lookup

For the synthetic acknowledgement profile, retained entries are strictly
ascending, unique and contiguous. Every entry must belong to the same
prediction generation and profile. Its command sequence must equal its
post-state source command sequence, and each pre-state must equal the previous
post-state. `find_exact` performs lookup by one exact
`GoldSrcUserCmdSequence`; reconciliation never chooses a nearest command.

Malformed ordering fails with typed categories such as
`prediction_command_gap`, `duplicate_predicted_command`, or
`out_of_order_predicted_command`. Synthetic sequences are nonzero, strictly
increasing 32-bit values. Wrap is unsupported and exhaustion is typed.

## Bounds and backpressure

`LocalPredictionHistoryLimits` defaults to:

- 64 entries, with a hard maximum of 256;
- 512 KiB of retained state accounting;
- 128 KiB of retained command accounting;
- 16 KiB of retained touch-summary accounting;
- 64 commands of maximum authority delay;
- 64 replay commands, with a hard maximum of 256;
- a non-wrapping bounded history revision.

Entry count, each byte budget, replay depth, authority delay and revision are
validated independently. A required unacknowledged command is never silently
evicted. When a new prediction would exceed capacity, insertion returns
`prediction_history_full` or `prediction_history_backpressure`; the current
valid state and history remain available and an interactive viewer can remain
responsive.

Acknowledged entries are trimmed only by a successful reconciliation that
publishes the matching authoritative anchor and all replayed unacknowledged
entries. Missing, future, already-evicted or gapped acknowledgements fail
closed.

## Signatures and statistics

`local_prediction_history_signature` hashes bounded typed history metadata in
deterministic order. It is suitable for repeatability checks; it is not a
server checksum, network acknowledgement, authentication value, or raw input
recording.

`LocalPredictionHistoryStatistics` records total appended, acknowledged and
replayed commands, publication count and high-water mark. Counters are checked
for exhaustion before publication. History owns no filesystem object, network
driver, renderer resource, input source, or collision scratch.

See [command replay](COMMAND_REPLAY.md) and
[prediction reconciliation](PREDICTION_RECONCILIATION.md).
