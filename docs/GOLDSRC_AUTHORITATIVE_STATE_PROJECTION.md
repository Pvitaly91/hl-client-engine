# Stock authoritative movement projection

## Observation before authority

`StockAuthoritativeMovementObservation` is a partial, immutable evidence
container. Every retained candidate field carries bounded
`StockAuthoritativeFieldProvenance` recording semantic target, schema/field
identity, message category and ordinal, exact cursor, scenario, confidence and
whether the value is direct or project-derived. Except for the canonical opaque
server-time observation, these pending provenance records are not yet bound to
frame catalog/source records and are not stock-confirmed fields.

Direct candidate fields include origin, velocity, view offset, hull/duck,
flags, water, maximum speed, gravity/friction, base velocity, opaque encoded
server time, old
buttons and update identity. Names alone are never used to bind runtime fields;
a future confirmed profile requires exact schema fingerprint, wire index/type
and per-field bit geometry.

Future project-derived ground/contents may be calculated only through the
existing collision boundary and must retain explicit derived provenance. The
query is read-only: authority is never nudged from solid and velocity is never
clipped during validation. Hull conflicts, blocking origin, liquids/ladders,
profile/session mismatch and direct/derived contradictions are typed failures.

## Adapter gate

`StockAuthoritativePlayerStateAdapter` returns a partial observation plus one
of the explicit pending/error statuses. It cannot return the existing
`AuthoritativePlayerState` until local identity, required movement fields,
server-time relation, exact command acknowledgement, old buttons and collision
session all agree.

M4.7.1 has no accepted stock grammar or exact usercmd acknowledgement, so the
adapter result remains `stock_evidence_pending` or
`command_acknowledgement_pending`. Prediction/reconciliation remains
synthetic-only and receives no partial stock state.

Modelindex-to-precache mapping is separately pending. The existing
`EvidencePendingStockEntityVisualProjectionProvider` and
`EvidencePendingStockModelResolver` remain fail-closed adapter placeholders;
they do not resolve stock models or produce live draw instances. M4.7.1 adds no
evidence-backed visual binding and no connected renderer loop.
