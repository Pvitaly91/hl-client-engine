# Stock runtime network isolation

## Dynamic WFP boundary

Active stock research uses Windows Filtering Platform through
`FwpmEngineOpen0` and sessions carrying `FWPM_SESSION_FLAG_DYNAMIC`. Before the
guard or any stock process launches, the orchestrator starts and validates a
temporary provider/sublayer for the exact canonical application identities.
The guard then owns an independent identical dynamic session. Either owner can
crash while the other keeps the fail-closed filters active. Closing both
sessions removes their filters through the OS. Persistent firewall rules,
registry persistence, `netsh advfirewall` and `New-NetFirewallRule` are not
used; the expected
persistent-rule count is zero.

The policy is per approved executable identity. Before filter installation the
project retains canonical path identity, volume serial/file ID, size,
write/change time and private SHA-256 while rejecting reparse points, ADS and
hard links. After launch the process image is reopened and compared to the same
identity. Any replacement terminates the complete owned research Job.

## Allowed and blocked traffic

For the exact approved research executables, the policy permits only:

- IPv4 loopback remote range `127.0.0.0/8`;
- IPv6 loopback remote `::1/128` when IPv6 loopback exists.

It blocks non-loopback IPv4 and IPv6 outbound, IPv4-mapped non-loopback IPv6,
non-loopback inbound accepts where applicable, and unapproved child network
access. DNS, LAN, multicast, broadcast, public Internet and Steam master-server
traffic from isolated research executables are not allowed. `sv_lan 1` and
`-nomaster` are defence-in-depth launch options, never substitutes for WFP.

## Canary and lifecycle

The project-owned canary applies the same dynamic policy to the probe tool and
requires IPv4 loopback success, IPv6 loopback success when capable, an
OS-classified denial for a deterministic non-loopback local connection, then
normal behavior after dynamic-session cleanup. A timeout/no-route result is not
accepted as proof of blocking. If a deterministic canary is unavailable, the
typed result is `isolation_canary_unavailable` and stock processes do not start.

The redundant orchestrator session is attested before guard launch and remains
in outer scope until both campaign and guard Jobs have exact zero-process
cleanup. The guard starts before relay, HLDS or client, publishes one bounded
readiness attestation and stays alive on an owned heartbeat. It inherits
retained handles to the campaign Job and its own guard Job. Normal release
requires the campaign Job's exact zero-process accounting. If heartbeat EOF occurs without the
wrapper's release signal (including simultaneous wrapper/orchestrator death),
the guard independently terminates the campaign, retains WFP while polling
`ActiveProcesses`, and exits nonzero only after observing zero. A failed query
or termination keeps the guard alive and WFP installed while it retries. Only
after exact zero may the research tree be restored and the run marked
incomplete. No weaker ordinary firewall fallback exists.

A reconnect run does not release or recreate isolation between generations.
The redundant orchestrator WFP session, guard session, guard Job, campaign Job,
relay and HLDS remain continuously owned while stock client A exits and stock
client B starts. The relay's private send-only A-tail emitter is loopback-only
and remains inside the already-approved relay process; it neither learns a
remote endpoint nor weakens the executable allowlist. Guard/server/relay loss,
failure to prove A absent, failure of the 250-ms A-source quiet handshake, or a
non-fresh B lifecycle aborts the transaction. The same exact zero-process proof
is still required before either WFP session can be released and restoration can
begin.

Dynamic WFP setup may require an elevated PowerShell. Insufficient privilege
is `network_isolation_privilege_required`; the tools never self-elevate or
bypass UAC. CI exercises pure policy/configuration tests and reports the actual
WFP capability integration as skipped when elevation/capability is absent—it
does not silently claim a canary ran.
