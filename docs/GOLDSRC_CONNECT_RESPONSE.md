# GoldSrc Protocol 48 connect response

## M2.2 scope

M2.2 decodes only the immediate connectionless result of the M2.1 `connect`
request. It adds strict `ACCEPT` and `REJECT` value types, a bounded response
wait on the existing UDP transport, and a terminal application stop point:

```text
--stop-after connect-response
```

It does not create a netchan, interpret sequenced traffic, acknowledge a
sequence, retry `connect`, enter sign-on, or claim that the project client has
completed a stock-server connection.

The wire profile below comes from sanitized clean-room observations of stock
Valve components. Raw captures that may contain authentication or identity
material remain ignored local artifacts; they are not documentation, fixtures,
CI artifacts, or Git objects.

The reference client was the signed Valve `hl.exe` 1.1.1.1 from Steam App 70,
build ID 15961492. The reference server was stock Valve `hlds.exe` 1.1.2.2,
Protocol 48, build 10210. Capture ran only on loopback and separate private
ports through a transparent relay, kept one upstream UDP socket for challenge
and connect, validated the exact server source endpoint, bounded receives to
1,024 bytes, and used a three-second research timeout. Owned research processes
were identity-checked before shutdown; no game/server binary is part of this
repository.

## Confirmed `ACCEPT` profile

Six bounded observations used an unmodified stock client, a transparent
same-socket loopback relay, and stock Valve HLDS Protocol 48 build 10210. All six
responses had this grammar:

```text
FF FF FF FF 42
SP <user-id:uint32>
SP DQUOTE <numeric-ipv4>:<nonzero-port> DQUOTE
SP <secure:0-or-1>
SP <server-build:uint32>
NUL
```

The line breaks in that grammar are documentation wrapping only; no CR or LF
appears in an accepted wire packet.

`42` is ASCII `B`. In compact text form, excluding the four-byte header:

```text
B <user-id> "<server-view-of-client>" <secure> <server-build>\0
```

The confirmed observations establish:

- exactly one ASCII space at every shown separator;
- canonical unsigned decimal fields;
- a quoted numeric IPv4 endpoint with a nonzero decimal port;
- one final NUL, with no LF and no trailing byte;
- 34 total bytes when the observed relay source port had five decimal digits;
- user IDs `1` and `2` across the six observations;
- the quoted endpoint matched the relay's actual upstream source endpoint in
  all six observations;
- secure field `0` while the observed HLDS reported VAC disabled;
- server build `10210` in all six observations.

The VAC-disabled correlation does not establish the value emitted by every
server configuration. The project type therefore preserves it as a boolean
wire field rather than using it to make a broader security claim.

The owning decoded value is:

```cpp
struct ConnectAccepted {
    std::uint32_t user_id;
    NetworkAddress server_view_of_client;
    bool secure;
    std::uint32_t server_build;
};
```

## Confirmed `REJECT` profile

Stock HLDS rejection probes used this grammar:

```text
FF FF FF FF 39 <nonempty-bounded-ascii-message> LF NUL
```

`39` is ASCII `9`; there is no separator between the class byte and the first
message byte. LF followed by NUL is mandatory, and no byte may follow it.

Sanitized observations were:

| Condition | Message | Total datagram size |
| --- | --- | ---: |
| Invalid challenge | `Invalid connection.` | 26 bytes |
| Missing required info | `Invalid connection.` | 26 bytes |
| Client protocol 47 against server protocol 48 | `This server is using a newer protocol ( 48 ) than your client ( 47 ).  You should check for updates to your client.` | 122 bytes |
| Client protocol 49 against server protocol 48 | `This server is using an older protocol ( 48 ) than your client ( 49 ).  If you believe this server is outdated, you can contact the server administrator at (no email address specified).` | 192 bytes |

The parser returns an owning `ConnectRejected { std::string message; }`. A
rejection is a valid protocol result and a distinct terminal state, but the
application reports it as an unsuccessful connection attempt.

## Parser and presentation bounds

All datagrams are untrusted. `parse_connect_response` applies these project
limits before exposing a typed result:

- 1,024 bytes maximum for the complete connectionless datagram;
- 512 bytes maximum for a decoded rejection message;
- canonical decimal syntax with explicit `uint32` overflow checks;
- canonical numeric IPv4 syntax and a port in `1..65535`;
- exact quotes, separators, class byte, and terminator;
- no locale-dependent number parsing, repair, or trailing-data tolerance.

Unknown connectionless class bytes have their own typed error. A sequenced
header is not reinterpreted as a malformed `ACCEPT` or `REJECT`.

Stored rejection text and display text are separate concerns. The application
passes the owned message through
`sanitize_connect_rejection_for_presentation`, which escapes control bytes,
ESC, backslash, DEL, and non-ASCII bytes and truncates the resulting single-line
text to at most 256 bytes. Network traces never contain a rejection message or
raw response bytes; they carry endpoint, classification, elapsed time, and
datagram size only.

## Response wait and endpoint policy

`ConnectResponseWaitStage` starts only after the one-shot connect send succeeds.
It reuses the same `IDatagramTransport`, requires the same existing local
endpoint, and accepts a result only from the exact requested remote IPv4
endpoint.

The default wait deadline is 5 seconds. Configuration is capped at 30 seconds,
64 received datagrams per update, and the 1,024-byte response bound. Polling is
nonblocking and driven by injected `steady_clock` time:

```text
idle -> waiting -> accepted
                -> rejected
                -> timed_out
                -> cancelled
                -> network_error
                -> protocol_error
```

Wrong-endpoint traffic, including an oversized datagram whose source is known,
is ignored before parsing. Missing or conflicting receive metadata is a network
error. A truncated or malformed known response from the exact server is a
protocol error. Unrelated connectionless classes from that server are ignored
within the bounded receive budget.

A nonconnectionless/sequenced packet from the expected endpoint is an explicit
terminal `unexpected_sequenced_packet_pending_m2_3` boundary. M2.2 sends no
acknowledgement and does not guess at sequence state. Netchan bootstrap,
sequencing, acknowledgements, reliability, and fragmentation belong to M2.3.

## Verification status

Deterministic tests cover the accepted and rejected capture shapes, strict
grammar mutations, every truncated prefix, decimal and size boundaries,
locale independence, sanitizer bounds, endpoint filtering, deadlines,
cancellation, receive metadata, terminal idempotence, and callback isolation.
Loopback fake-HLDS tests exercise the production UDP transport for both
`ACCEPT` and `REJECT`.

The evidence scopes remain distinct:

- stock client -> stock HLDS response observations: recorded and sanitized;
- project client -> local fake HLDS accept/reject paths: deterministic tests;
- project client -> stock HLDS acceptance: not claimed or established.

See [Connect request](GOLDSRC_CONNECT_REQUEST.md) for the preceding wire stage
and [Authentication provider](AUTHENTICATION_PROVIDER.md) for authentication
ownership and lifetime.
