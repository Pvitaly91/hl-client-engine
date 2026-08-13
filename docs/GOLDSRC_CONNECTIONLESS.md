# GoldSrc protocol 48 connectionless challenge

## M1 scope and interoperability reference

M1 implements only the initial IPv4 connectionless challenge exchange. It does
not build or send `connect`, create a netchan, authenticate a client, or enter
sign-on.

The supported compatibility profile is:

- original Steam Half-Life dedicated server on Windows;
- game directory `valve`;
- GoldSrc protocol 48;
- observed executable version 1.1.2.2, build 10210 (October 7, 2024);
- IPv4 connectionless `getchallenge steam` exchange.

The implementation is independent. Observable original-HLDS behavior and a
controlled packet capture are the interoperability reference; no external
engine implementation is copied into this repository.

## Wire profile

All values below are bytes on the wire. Every packet begins with exactly four
`FF` bytes. The M1 encoder and decoder compare those bytes explicitly and do
not rely on host endianness, alignment, packed structs, or C strings.

The client request is 23 bytes:

```text
FF FF FF FF 67 65 74 63 68 61 6C 6C 65 6E 67 65 20 73 74 65 61 6D 0A
```

After the four-byte header its ASCII payload is:

```text
getchallenge steam\n
```

The request terminator is one LF byte (`0A`). There is no request-side NUL.
The builder owns its output and adds neither a second LF nor a hidden NUL.

An observed valid response was 47 bytes:

```text
FF FF FF FF
41 30 30 30 30 30 30 30 30 20
33 36 34 33 33 37 38 38 37 20
33 20
37 32 30 35 37 35 39 34 30 33 37 39 32 37 39 33 36 20
30 0A 00
```

Its payload was:

```text
A00000000 364337887 3 72057594037927936 0\n\0
```

The challenge is dynamic. The normalized response layout is therefore:

```text
A00000000 <challenge> <profile-parameter-1> <profile-parameter-2> <profile-parameter-3>\n\0
```

Fields use canonical unsigned decimal ASCII separated by exactly one space:

- `challenge`: project-owned signed 32-bit type, restricted by this profile to
  `0..2147483647`; negative and overflowing inputs are rejected;
- `profile-parameter-1`: unsigned 32-bit and exactly `3` for this profile;
- `profile-parameter-2`: unsigned 64-bit and exactly
  `72057594037927936` for this captured profile;
- `profile-parameter-3`: unsigned 32-bit and exactly `0` for this captured
  profile.

Only the first field has been independently established as the dynamic
challenge. The remaining three values deliberately retain neutral names in the
project-owned API until their semantics are separately proven. They are matched
exactly to the captured tuple rather than accepting guessed variants.

The response terminator is exactly LF followed by NUL (`0A 00`). A missing,
reversed, duplicated, or followed-by-extra-data terminator is rejected.

The original server tolerated some alternate request endings during diagnostic
probes. M1 deliberately sends only the captured profile above and has no
shotgun fallback.

Known unsupported variants are WON/protocol 47, Xash protocol 49,
Counter-Strike-specific authentication variants, ReHLDS extensions, IPv6, and
any connectionless response shape other than the strict profile above.

## Bounds and parse errors

M1 caps an entire connectionless challenge datagram at 1,024 bytes. This is
well above the 23-byte request and 47-byte observed response while avoiding the
unnecessary 65,507-byte general IPv4 UDP receive allocation. A single update
processes at most eight datagrams.

Typed failures distinguish short packets, bad connectionless headers,
oversized payloads, unexpected response types, missing/invalid/overflowing
challenge values, invalid terminators, unexpected trailing data, and
unsupported profile variants. Diagnostic context is bounded and raw untrusted
bytes are never printed directly.

## Exchange state machine

The controller exposes these states:

```text
idle -> sending_request -> waiting_for_response
                                  |-> challenge_received
                                  |-> timed_out
                                  |-> cancelled
                                  |-> network_error
                                  `-> protocol_error
```

Default policy:

- retry interval: 1 second;
- maximum attempts: 3;
- overall timeout: 5 seconds;
- maximum datagrams per update: 8;
- maximum datagram size: 1,024 bytes.

The caller supplies `steady_clock::time_point` values. The state machine never
sleeps and tests advance a manual clock. Once terminal, later updates, duplicate
responses, retries, and cancellation do not change the result or send packets.

Only a datagram from the exact requested IPv4 address and port can complete the
exchange. Wrong-endpoint traffic is ignored. `would_block` ends polling for the
current update; truncated target traffic is a protocol error and transport
failures are network errors.

## Network trace

`--net-trace` enables TX/RX direction, endpoint, classification, attempt,
elapsed time, packet size, and an escaped preview. The preview is capped at 128
bytes. Printable ASCII remains readable; backslashes and control bytes are
escaped, so terminal escape bytes cannot be emitted verbatim.

## Tests and manual verification

Synthetic tests cover the envelope, exact request, strict response parser,
every valid-packet truncation, retry/deadline/cancellation transitions, endpoint
spoofing, receive-loop bounds, trace escaping, and terminal-state idempotence.
A local fake-HLDS test uses only nonblocking UDP on `127.0.0.1`, validates the
exact request, returns a synthetic response, and verifies that no follow-up
`connect` datagram is sent. A second responder intentionally ignores the first
request and proves the bounded retry. These tests need no Steam, Internet, game
data, display, or GPU.

To verify a user-supplied original server manually:

```powershell
.\scripts\verify_original_hlds_challenge.ps1 `
  -ClientPath .\build\bin\Debug\hlclient.exe `
  -Endpoint 127.0.0.1:27015
```

To let the script start a server, also pass an explicit `-HldsPath`; the script
does not search for or download binaries. It stores logs below ignored
`manual-artifacts/`, and its `finally` block stops only the process it created.

## Deferred work

M2.1 may construct a `connect` request from the received challenge. Steam
authentication, the protocol-48 connect payload, netchan, sequencing,
fragmentation, sign-on, resources, and gameplay remain intentionally absent
from M1.
