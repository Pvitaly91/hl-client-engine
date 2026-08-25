# GoldSrc entity baseline state

`EntityBaselineRegistryState` is bounded, immutable-through-public-API and
owning. It is generic structured state for the evidence-pending M4.5.1 boundary;
it is not a stock baseline wire decoder.

Each entry has an exact entity-or-alternate-slot key, an explicit schema
category/name, an owning `DeltaObjectState`, bounded source geometry and an
evidence profile. Sign-on generation is not inferred by this neutral component;
its lifecycle belongs to the enclosing stage. The builder receives the published
`DeltaSchemaRegistryState`; it does not parse descriptions again and never uses
HLSDK struct offsets or packing.

The synthetic-neutral builder validates:

- exact schema presence and full retained descriptor/profile agreement;
- configured entity-number and baseline-count bounds;
- unique baseline identity;
- bounded source geometry and total owned value bytes;
- explicit ordinary/player/custom/alternate category rather than inferring a
  schema solely from an entity number.

Insertion is transactional and publication owns all strings and values. A
duplicate key, unsupported schema/category, out-of-range entity or limit
overflow returns a typed error and leaves the candidate unchanged.

The stock profile returns `runtime_grammar_evidence_pending`. Baseline message
opcode/framing, identity encoding, ordinary/player/custom selection flag,
alternate slots and default/base rules remain pending accepted captures.
