# Stock Protocol 48 entity baselines

## Status

Stock baseline wire grammar is evidence-pending. M4.7.1 does not assign a
baseline opcode, count/terminator encoding, identity domain, schema selector or
bit alignment.

| Claim class | Current result |
|---|---|
| Stock-confirmed | None for the post-resource baseline body. |
| Public-header cross-check | `CreateBaseline` and `entity_state_t` describe semantic source data, not wire encoding. |
| Project-derived | Existing immutable baseline registry mechanics are bounded and transactional. |
| Pending | Message category, identity grammar, schema category, entry ordering and exact end cursor. |

The existing `EntityBaselineRegistryBuilder` remains sealed to the independent
synthetic profile. A pending stock observation is never inserted into it. A
future `StockEntityBaselineMessageDecoder` may publish only after repeated
signed-stock captures establish all body geometry and schema-selection rules.

Baseline identity must stay explicitly tagged as entity number, alternate
slot, model/class domain or another confirmed domain. Parse order is not an
identity. Duplicate or conflicting identities must fail transactionally and
leave the previous registry unchanged.

With zero accepted runs the baseline count is zero and the result is
`evidence_pending`; there is no tracked baseline evidence projection.
