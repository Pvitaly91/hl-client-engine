# Stock command-acknowledgement evidence

## Separate candidate domains

`StockCommandAcknowledgementEvidenceState` does not collapse:

- client-to-server netchan sequence;
- server-to-client netchan acknowledgement;
- client move-packet ordinal;
- server runtime-frame reference;
- explicit clientdata field candidate;
- exact usercmd sequence.

Statuses can represent `unobserved`, typed candidate/correlation reports, or
`conflicting`. The current builder accepts bounded caller-supplied aggregate
counters and source-record ordinals for synthetic/reporting tests; it is not an
independent packet/corpus correlator and cannot reach
`exact_usercmd_sequence_confirmed`. Runtime generation, loss, delay, duplicate,
batching and reset observations remain part of any future evidence-backed
promotion decision.

A netchan acknowledgement acknowledges the transport sequence domain; it is
not automatically a usercmd acknowledgement. Snapshot references and move
packet ordinals are likewise not command identities. No candidate is converted
to `AuthoritativeCommandAcknowledgement::for_sequence` unless a one-to-one
stock usercmd-domain relation survives controlled loss/batching/reset tests.

No such accepted evidence exists in this commit. The exact usercmd ACK status
is `exact_usercmd_sequence_pending`; netchan and move-packet correlation results
are `not_observed`. Stock usercmd envelope/checksum and project-to-stock command
acceptance remain M4.7.2 work.
