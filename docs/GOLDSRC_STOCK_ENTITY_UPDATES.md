# Stock Protocol 48 entity updates

## Status

Full and delta entity packet grammar remains evidence-pending. The project does
not infer entity numbers, remove flags, baselines or snapshot references from
public C structures or from synthetic fixtures.

Pending items include:

- distinct full and delta message categories/opcodes;
- entity-number width, delta/increment behavior and terminator;
- player/custom schema selection and baseline identity;
- explicit removal versus omission semantics;
- snapshot-reference width, delta-base relation and wrap/reset policy;
- recovery after loss, duplicate and reorder.

The generic `EntityFullSnapshotBuilder`, `EntityDeltaSnapshotBuilder` and
`EntitySnapshotHistoryBuilder` remain synthetic-only. They demonstrate bounded
immutable add/update/remove and exact-base mechanics but are not stock wire
evidence.

A future confirmed decoder must use exact declared references, reject a missing
base without nearest-base fallback, suppress only evidence-backed old/duplicate
updates and publish the entire snapshot atomically. Ambiguous wrap edges must
fail closed. With zero accepted sessions there are no stock full/delta/removal
counts and no stock snapshot history publication.
