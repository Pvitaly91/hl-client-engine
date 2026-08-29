# Stock local-player identity

## State model

`StockLocalPlayerIdentityState` keeps evidence domains distinct and supports:

- `unresolved`;
- `single_client_candidate`;
- `multi_client_correlated`;
- `confirmed_for_profile`;
- `conflicting`.

Potential sources include a confirmed server slot, view entity, user-info
index, player entity number, client-local association and two-client
differential attestation. No player name, user ID, Steam identity or raw message
value is retained in committed metadata.

The following shortcuts are forbidden:

- entity 1 is local;
- the first packet entity is local;
- ACCEPT user ID equals entity number;
- user-info index equals entity number;
- an opaque serverinfo slot is already confirmed;
- view entity always equals player entity.

Single-client correlation can produce only a candidate. Universal identity
requires accepted two-client evidence and an exact profile. Reconnect, map
change, removal or generation mismatch invalidates stale identity. Conflicting
sources fail transactionally.

No accepted stock runtime sessions exist for this boundary commit, so the
current result is `unresolved`; both single-client and two-client identity
results are pending.
