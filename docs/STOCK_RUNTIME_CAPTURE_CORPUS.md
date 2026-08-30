# Stock runtime capture corpus

## Ignored run layout

Every local run is an exact child of ignored
`manual-artifacts/stock-runtime/`:

```text
<32-lower-hex-run-id>/
    capture-metadata.json
    research-run-metadata.json
    version-observation.staged.json
    isolation-attestation.staged.json
    restoration-attestation.staged.json
    version-observation.json
    isolation-attestation.json
    restoration-attestation.json
    reconnect-transport-observation.staged.json   # reconnect only
    reconnect-orchestration.staged.json           # reconnect only
    reconnect-observation.json                    # accepted reconnect only
    transport-journal.jsonl
    raw/
    logs/
```

Files are bounded, no-follow/unlinked, atomically published and never
overwritten. Publication holds exact run/raw/log directory capabilities with
private random delete-on-close child locks; temporary files use random
`CREATE_NEW`, are flushed and identity-checked through the same handle, and
rename without replacement. `capture-metadata.json` retains
`hlclient.stock-runtime-capture-metadata.v1` compatibility. New schemas are
`hlclient.stock-runtime-research-run.v1`,
`hlclient.stock-runtime-version-observation.v1`,
`hlclient.stock-runtime-isolation-attestation.v1`,
`hlclient.stock-runtime-restoration.v1` and
`hlclient.stock-runtime-transport-journal.v1`.

`research-run-metadata.json` is the final transactional publication. The C++
orchestrator never marks acceptance. PowerShell creates the manifest once,
after process cleanup/restoration and prepublication checker/walker agreement.
An incomplete run is explicitly `accepted_evidence_run=false` with a typed
`failure_category`.

Prepublication loading requires only the three `*.staged.json` attestation
leaves and rejects final leaves or a final run manifest. Accepted publication
retains those staged leaves, requires the final trio plus the run manifest, and
requires every staged/final pair to have the same exact structural SHA-256.
This prevents a mixed or partially published attestation generation from
being treated as one accepted corpus.

Reconnect leaves are scenario-dependent and fail closed. A non-reconnect run
rejects all three. Reconnect prepublication accepts only the atomic staged
transport/orchestration pair. Published accepted reconnect requires that pair
plus strict `hlclient.stock-runtime-reconnect-observation.v1`; incomplete
publication cannot contain the final leaf. The two staged document hashes bind
the corpus structural hash. The final post-replay leaf is exposed separately,
avoiding a manifest/hash publication cycle. These schemas contain bounded role
tokens and counters, never endpoints, ports, PIDs, paths, candidate bodies or
semantic names.

`capture-metadata.json` is the immutable relay source for scenario identity;
only the three documented relay-to-campaign aliases are canonicalized.
`version-observation.json` independently records the exact stock
`map_category` selected by the owned orchestrator. An accepted final manifest
must match both sources before any campaign counter is aggregated.

## Transport journal

Each observed datagram has one JSONL record: 0-based global observed ordinal,
1-based direction ordinal, bounded relative microseconds, payload size, exact
raw filename, source/destination roles, transport action/hold state, zero to two
0-based emitted ordinals, delivered/wrong-source state and lowercase SHA-256
for local integrity only. Endpoint addresses are never stored.

Observed and peer-delivered streams are distinct. A drop has no emitted
ordinal; a duplicate owns two; delay/reorder publication records the exact
release/emission order. Offline replay consumes emitted order, so it cannot
decode a dropped datagram as peer-visible. Perturbations are transport-ordinal,
not semantic runtime/entity/usercmd categories.

The independent PowerShell walker does not call the production checker. It
checks JSON properties, ordinal/action/hold/emission geometry, raw cardinality,
size/hash, observed and delivered connectionless/sequenced counts, low-30-bit
netchan headers, flags, transformed fragment descriptors/ranges and final
manifest structural counters. It prints no bytes.

The checker publishes both populations explicitly. `delivered-sequenced-c2s`,
`delivered-sequenced-s2c` and `delivered-fragment-datagrams` count every
peer-delivered journal emission, including duplicate and reordered-old
datagrams. Its existing `sequenced-c2s`, `sequenced-s2c` and `fragments`
values are the replay-accepted-new subset after sequence suppression. Final
run manifests name the peer-delivered population as
`delivered_sequenced_c2s_count`, `delivered_sequenced_s2c_count` and
`delivered_fragment_datagram_count`. The checker also publishes aggregate
`duplicate-packets` and
`old-packets`, and verification requires accepted-new plus those two
suppressed classes to equal the total delivered sequenced population. Accepted
replay fragments remain a subset of delivered fragment datagrams because a
suppressed packet may itself carry a fragment flag.

Campaign totals and the 100-S2C per-run floor bind peer-delivered counts for
normal slots. For reconnect they instead bind the `sequenced-*` sum of
independently replayed generation A and B. Exact-HLDS sequenced traffic after A retirement and before
B's fresh ACCEPT remains byte-preserved as
`retired_generation_a_server_tail_packet_count`, but is excluded from B replay,
generation proof and every campaign packet threshold.

For a published first-observation check, the loader exposes the final replay,
cursor, candidate, reassembly/decompression and replay-hash claims as one
typed value. The standalone checker reconstructs the same value after replay
and requires exact equality before it can print `accepted-run=true`; earlier
diagnostic scenarios deliberately defer acceptance and print false.

## Acceptance and hygiene

Acceptance requires verified isolation/version/process ownership, bounded
transport completion, exact restoration, no external drift, valid corpus,
successful offline sign-on replay, exact post-resource boundary, an unconsumed
first observation and at least 100 threshold-eligible S2C packets.
Transport completion alone is insufficient.

Accepted reconnect requires two controlled generations, two exact boundaries,
two matching neutral candidates, distinct process/endpoint roles, continuous
guard/server/relay, exact cleanup/restoration and a fresh B ACCEPT before B
sequenced traffic. Its manifest adds exact claims `2/2/2/true/false` for
generation/boundary/candidate counts, distinctness and conflict; other
scenarios reject those fields. Normal runs record `single_observation`.
Reconnect records `stable_observation` only from its two independent replays;
campaign-wide stability remains verifier-owned.

Raw datagrams, logs and private binary fingerprints stay under ignored local
paths. CI does not upload them. Authentication bytes, player/Steam identity,
ports, native paths and raw payload hashes derived from authentication-bearing
packets are forbidden in committed evidence.
