# Authentication provider boundary

## Purpose and non-goals

M2.2 separates acquisition of authentication material from the stateless
GoldSrc connect codec. The provider API does not give an implementation access
to the renderer, world state, filesystem abstraction, or UDP socket, and the
codec does not discover files or call platform authentication services.

The repository currently supplies only
`ExplicitFileAuthenticationProvider`, a development/manual adapter for a path
the user names explicitly. There is no Steam authentication provider, ticket
generator, account discovery, credential recovery, authentication bypass, or
fallback search. Supplying bytes does not prove that a stock server will accept
them or authorize their use.

## Asynchronous contract

`IAuthenticationProvider::begin` receives an
`AuthenticationRequestContext` containing:

- the requested remote IPv4 endpoint;
- the GoldSrc protocol version and bounded compatibility profile;
- an optional challenge token.

It returns either a typed start error or one owned
`IAuthenticationOperation`. The operation is deliberately poll-based:

```text
begin(context)
    -> operation
       -> update(): pending
       -> update(): succeeded(AuthenticationSession)
       -> update(): failed(AuthenticationError)
       -> cancel() noexcept
```

This permits a future platform provider to complete asynchronously without
blocking the frame loop. The returned operation owns pending work and must not
depend on the caller retaining the provider or request-context object.
`pending`, `succeeded`, and `failed` results are explicit; failure categories
distinguish unavailable input, configuration, provider failure, invalid
material, oversized material, and cancellation.

Result fields remain public, so consumers validate the state together with its
required session/error payload. The generic API does not impose a deadline,
thread-safety model, exception boundary, or single-update policy. It also does
not automatically bound or redact `AuthenticationError::context`; every
provider must keep diagnostics bounded and free of credentials, ticket bytes,
file contents, and other secrets.

The current explicit-file operation is synchronous but still implements the
same interface: its first `update()` loads once and completes, cancellation
before that update yields a typed cancelled result, and later updates return a
typed provider error. Each new `begin()` creates a fresh operation and rereads
the configured file; material is not cached.

## Session and sensitive lifetime

`AuthenticationSession` is move-only and owns two things:

1. one move-only `AuthenticationMaterial` value;
2. an optional provider-specific `IAuthenticationSessionLifetime` guard.

`take_material()` transfers the wire material at most once from that session
into connect-request preparation. Moving the bytes out does not release the
lifetime guard. This relationship is a caller-side ownership contract rather
than a type-level coupling: the caller must retain or move the emptied session
after taking its material. A connect coordinator can be constructed without a
session guard.

The current application composition moves the remaining session into the
handshake coordinator. Connect-request mode releases it at terminal
`request_sent`; connect-response mode retains it through accept, reject,
timeout, cancellation, or error. For the M2.3.3 netchan stop, the client-layer
composition moves the emptied session behind an authentication-agnostic
`INetchanDriverLifetime`. The coordinator/stage-owned driver retains that guard
after `ACCEPT` through unfragmented or reassembled bootstrap success, timeout,
cancellation, network/protocol failure, backpressure, close, or another driver
terminal outcome.
Member and move-assignment ordering ensures old material is destroyed before
its provider lifetime guard.

The driver clears channel state and destroys its opaque lifetime exactly once
on every terminal path. Netchan code sees neither `AuthenticationSession` nor
authentication bytes. That ordering lets a future provider keep an external
ticket or session handle valid through the complete selected handshake stop,
including fragment reassembly and its final ACK. The lifetime object's
destructor must not log authentication data.

The boundary minimizes accidental exposure but is not secure-memory storage:
current strings/vectors/stack arrays are not memory-locked and do not promise
explicit zeroization, secure allocation, or crash-dump exclusion. The API does
not enforce freshness, replay prevention, audience/endpoint/challenge binding,
revocation, or cryptographic single use. GoldSrc sends the resulting bytes over
plaintext UDP; log redaction is not network confidentiality.

For the explicit-file path, bytes first occupy one fixed 246-byte loader buffer
(245 accepted bytes plus the one-byte oversize probe), then the protected and
suffix regions are copied into the owning `AuthenticationMaterial`. Subsequent
session/request transfers are moves. During `ConnectRequestBuilder::build`, one
additional bounded wire-datagram copy necessarily coexists with the owning
material until the synchronous `send_to` returns; both are destroyed after the
one-shot send while only the provider lifetime guard remains during response
waiting and an explicitly selected M2.3.3 bootstrap/driver. These milestones do
not add a custom allocator or claim that destroyed storage has been securely
erased.

Users must protect the source file with appropriate filesystem ownership and
permissions, avoid committing or sharing it, and remove it according to their
own security policy. The file path may also remain visible in process arguments
or shell history. Neither diagnostics nor tracing is a permitted channel for
the material.

## Explicit file provider

The explicit provider consumes exactly one 245-byte local record:

```text
32-byte protected ASCII-hex region || 213-byte binary suffix
```

This is the currently supported captured stock profile, not a general ticket
format. The file loader first validates the protected region's permitted
printable characters; the default connect preparation then requires all 32
protected bytes to be ASCII hexadecimal and the suffix to be exactly 213
bytes. The underlying owning type has broader hard allocation ceilings for
tests and future reviewed profiles, while the production connect profile still
requires exactly 32 plus 213 bytes.

The loader reads at most one byte beyond the accepted record to distinguish an
exact record from an oversized one. On Windows it opens the final path once,
denies sharing during validation/read, rejects a directory or reparse point in
the final component, and reads through that same RAII handle. This does not
exclude parent-path reparses or UNC/network locations, and the provider does
not enforce file ownership or permissions. Non-Windows status/open operations
do not provide the same one-handle path-swap protection. Filesystem access is
byte-bounded but may still block.

Diagnostics contain neither the path nor bytes. Errors are mapped into the
provider categories; notably, a record larger than 245 bytes becomes
`material_too_large` rather than a generic invalid record.

The provider performs no discovery, persistence, output-directory copy,
fallback, network access, or Steam integration. The recommended explicit CLI
form for any connect stage is:

```text
--auth-provider file --auth-material-file <explicit-local-path>
```

`file` is the only accepted provider name. `none`, `steam`, `bypass`, and every
other name are rejected; there is no silent fallback. For M2.1/M2.2 command-line
compatibility, `connect-request` and `connect-response` still accept the legacy
spelling with `--auth-material-file` alone and infer the same file provider.
The netchan documentation and examples use the explicit provider spelling.
Provider/file settings are invalid for challenge-only mode and require an
explicit `--connect` endpoint.

## Composition and shutdown

The current response path is:

```text
ExplicitFileAuthenticationProvider
    -> IAuthenticationOperation
    -> AuthenticationSession
    -> take AuthenticationMaterial once
    -> PreparedConnectRequest
    -> GoldSrcHandshakeCoordinator retains session guard
    -> challenge -> one connect send -> bounded response wait
       |-> selected connect-response terminal outcome
       `-> accepted + selected same-socket M2.3.3 netchan bootstrap
           -> move emptied session behind INetchanDriverLifetime
           -> stage/coordinator-owned NetchanDriver
           -> first unfragmented or reassembled slot-0 opaque payload
           -> required transport acknowledgement(s)
           -> driver terminal cleanup releases guard exactly once
```

Connect request traces expose only sizes/counts and a redaction marker.
Connect-response traces are metadata-only. A rejection message is remote text,
not authentication material, and is separately escaped and presentation-capped
before logging.

The provider boundary is in-process modularity, not a security sandbox. No part
of M2.2–M2.4.1 authorizes bypassing Steam, server policy, VAC, access control,
or third-party terms. The absence of a production Steam provider is why a
project-client-to-stock-HLDS bootstrap remains pending. A legitimate future
provider requires its own platform, legal, storage, cancellation, and teardown
review.

The retained provider guard does not give netchan access to authentication
bytes. Reliable queues, retransmission, fragmentation/reassembly, and the
persistent driver do not alter this authentication ownership boundary. The
`netchan-bootstrap` CLI mode still stops at the first complete opaque payload.
The explicit `signon-boundary` mode transfers the same provider guard into
`InitialSignonStage` at `ACCEPT` and releases it exactly once on boundary,
timeout, cancellation, network/protocol error, backpressure, or secondary
stream pending. Neither mode gives netchan/sign-on codecs access to auth bytes.
