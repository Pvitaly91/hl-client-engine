# GoldSrc entity snapshot state

M4.5.1 provides bounded generic full/delta state mechanics under the sealed
`synthetic_neutral_v1` profile. It does not claim the stock packet-entity wire
grammar and does not create render instances.

## Snapshot model

`EntitySnapshotState` owns a typed reference, explicit full/delta kind, optional
exact base reference, server-time metadata, strictly ascending unique entity
states, explicit removals, bounded source geometry, statistics and evidence
profile. Generic `DeltaObjectState` values remain authoritative.

A synthetic full snapshot starts only from caller-supplied typed entries and
exact baselines. A delta snapshot resolves the exact referenced immutable base,
structurally preserves unchanged entries, applies typed updates, adds only from
an exact baseline and removes only through an explicit removal record. The
builder validates wire-order-equivalent ascending input before publication; it
does not silently sort malformed input. Missing/wrong bases, duplicate updates,
schema mismatches, nonexistent removals and absent add baselines fail
transactionally.

Every delta base and history insertion is revalidated against the receiving
builder's entity-count, entity-number, baseline-key and per-entity field limits,
so a state created under looser policy cannot bypass a tighter consumer.
Retained base entities must still resolve to the current exact baseline category
and schema descriptor. An explicitly supplied existing-entity update with zero
actual field differences is a shared no-op and does not inflate the changed
count; an identical new entity is still an explicit add.

An entity-number baseline key is identity-specific: its value must equal the
snapshot entity number. Reusing such a key for another entity fails with
`baseline_identity_mismatch`. Only an explicitly typed alternate-slot key may
be shared across entity numbers.

Stock entity-number width, absolute/delta numbering, end marker, reference
width, add/remove marker and schema-selection bits remain evidence-pending.

## History

`EntitySnapshotHistoryState` is immutable and caller-owned. Exact references are
indexed while chronological order is retained. The default retention bound is
64 snapshots and the hard cap is 256, with a separate bounded total-value-byte
budget. Required bases are retained until released; if capacity cannot be met
without evicting one, insertion fails instead of corrupting a delta chain.

The synthetic profile uses strictly increasing, non-wrapping 32-bit references.
It distinguishes duplicate, old, future-base, missing-base and evicted-base
outcomes. Stock reference width, modular comparison, wrap and half-range policy
return a typed evidence-pending result until captured independently; the netchan
30-bit sequence domain is not reused.

Clientdata and weapondata remain unsupported and evidence-pending because their
initial frame placement and reference-time grammar have not been confirmed; no
decoder, state type or stage event is published for either one. Snapshot state
is not connected to `ClientWorldState`, local prediction, model
loading, asset binding, the renderer or the filesystem.

M4.5.3 consumes this state only through a separate immutable projection layer.
The implemented route takes caller-owned typed synthetic records and typed
seconds; it never interprets `synthetic_raw` as seconds or looks up fields by
name. Production stock projection still returns evidence-pending. See
[entity visual projection](ENTITY_VISUAL_PROJECTION.md) and
[entity interpolation](ENTITY_INTERPOLATION.md).

M4.6.3.3 does not change this boundary. Its `AuthoritativePlayerState` comes
only from the typed in-memory synthetic authority source. It does not infer a
local player by entity number, look up origin/velocity fields by name, reuse an
entity snapshot reference as a command acknowledgement, or treat a netchan ACK
as a usercmd ACK. Stock local-player entity projection and command-reference
mapping remain explicitly evidence-pending for M4.7.1.
