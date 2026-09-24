# Functional stock capture to application runtime replay

M4.7.1.2H defines a local functional compatibility path from one owned stock
client/server session into the already existing application replay path. It is
not the strict 24-run campaign, a live connection from `hlclient.exe`, or a
claim of general stock build 10210 interoperability.

## Capture role

The explicit PowerShell parameter set `FunctionalRuntimeCapture` requires the
case-sensitive token `HLCLIENT_FUNCTIONAL_RUNTIME_CAPTURE_V1`. Its native relay
output role is `functional-runtime-capture`, rooted below ignored
`manual-artifacts/research-runtime-capture/<run-id>`.

The mode keeps the existing research-copy, binary identity, map, owned-process,
dynamic WFP isolation, byte-preserving relay, bounded journal and exact
restoration gates. It launches one `hl.exe -steam` and one `hlds.exe` through
the existing native orchestrator, enables server logging before the client,
uses the existing userid/map-entry observation and retains a 15-second stable
idle runtime interval. Once that functional interval has passed, the
orchestrator proceeds directly to the relay's stop boundary; both owned peer
sockets and dynamic isolation remain alive until the relay has flushed its
journal and metadata. Only then are the owned client and server terminated.
This ordering avoids turning a closed client UDP endpoint into a relay receive
failure before finalization. The larger maximum duration is a safety bound, not
an additional functional observation interval. It does not run the campaign
runner, perturb delivery, inspect ETW writers or assert that global Steam state
is unchanged.

A successful run publishes two dedicated local documents:

- `functional-runtime-capture.json`, with
  `purpose=functional_runtime_capture` and
  `campaign_evidence_eligible=false`;
- `restoration-attestation.functional.json`, binding exact owned-process
  cleanup and equal before/after research-copy inventories.

The functional manifest binds the exact byte length and SHA-256 of capture
metadata, the transport journal, version observation and isolation
attestation. It also binds the monotonic map-entry, interval-complete,
shutdown-request, relay-stop and relay-finalization offsets, the requested
maximum and the exact `functional-interval-complete` stop reason. It marks
`recorded_by_stock_pair=true` independently of
`validated_by_our_decoder=false`. It neither creates nor accepts a strict
`research-run-metadata.json`.

The owning session is unperturbed. A stock client may emit the exact 25-byte
connectionless A2S_INFO query from a second loopback endpoint while its netchan
endpoint remains active. Functional capture records that bounded auxiliary
observation and its raw bytes, does not deliver it into the owning session, and
accepts no other unexpected-source shape. Strict campaign publication remains
fail-closed for every unexpected source.

The functional complete-journal policy recognizes the same exact observation
at publication time. It is separate from the strict complete-capture policy,
checks the fixed length and SHA-256, caps the count, and still requires every
owning-session delivery invariant. This prevents a relay that has correctly
continued the owning stream from failing only when it serializes the final
journal.

The no-stock `ValidateFunctionalPublicationRoundtrip` mode exercises the
production raw/journal/metadata writer, the wrapper's production final
publisher, `StockRuntimeCaptureCorpusLoader(functional_capture)` and
`StockRuntimeTransportReplay` in one temporary producer-to-disk-to-consumer
transaction. It also proves that strict policy rejects the auxiliary shape,
missing or changed artifacts do not load, a failed finalization leaves no false
complete manifest, and a later semantic replay failure does not delete a valid
corpus. The process-level branch uses the real controller, relay stop handle,
writer and loader with project-owned loopback fake peers. A separate low-limit
branch proves that a limit reached before the required interval retains the
exact accepted raw/journal prefix and incomplete metadata without publishing a
complete manifest. It starts no stock process, Steam component or WFP session;
the only sockets are those of the project-owned loopback fake peers.

## Offline ownership path

`RuntimeReplayCaptureLoader` composes existing components in this order:

```text
functional run root
  -> StockRuntimeCaptureCorpusLoader(functional_capture)
  -> delivered datagrams in recorded peer-visible order
  -> StockRuntimeTransportReplay
  -> owning reassembled/decompressed service payloads
  -> StockCapturedSignonReplay
  -> retained ServerInfoState + DeltaSchemaRegistryState + exact cursor
  -> GoldSrcEntityBaselineDecoder at framed svc_spawnbaseline
  -> RuntimeReplayInitialization + ordered RuntimeReplayRecord values
  -> RuntimeReplaySceneSource
  -> RuntimeReplaySession A/B/C/D dispatch and ClientWorldState publication
  -> ordinary NullRenderer application loop
```

Client-to-server payloads establish observed connection/sign-on context only.
The adapter never opens a socket or emits an ACK, sign-on request or usercmd.
It does not scan for opcode bytes. Before `svc_spawnbaseline`, only messages
whose framing is established by an existing decoder may advance the exact
cursor. Unknown framing returns the source payload ordinal, byte/bit cursor and
opcode as a typed unsupported result. A decoder failure leaves the last
committed `ClientWorldState` revision unchanged.

Runtime records preserve delivered order, source netchan sequence and payload
provenance. The public non-zero replay ordinal is the original zero-based
transport payload ordinal plus one, so omitted C-to-S and padding payloads are
not silently renumbered into a dense server-only history.

## Historical capture command and offline replay

The active command below documents H's separate opt-in capture workflow. It is
not part of I or its test suite; I must not run another capture or isolation
campaign.

An elevated capture host uses:

```powershell
.\scripts\capture_stock_runtime_state.ps1 `
  -FunctionalRuntimeCapture `
  -ConfirmFunctionalRuntimeCapture 'HLCLIENT_FUNCTIONAL_RUNTIME_CAPTURE_V1' `
  -ResearchHalfLifeRoot 'D:\DEV\HLCLIENT-RESEARCH\Half-Life' `
  -ClientPath 'D:\DEV\HLCLIENT-RESEARCH\Half-Life\hl.exe' `
  -HldsPath 'D:\DEV\HLCLIENT-RESEARCH\Half-Life\hlds.exe' `
  -CaptureToolPath '.\build\bin\Release\hlclient_stock_runtime_capture.exe' `
  -NetworkIsolationGuardPath '.\build\bin\Release\hlclient_stock_runtime_isolation_guard.exe' `
  -AppManifestPath 'D:\Steam\steamapps\appmanifest_70.acf' `
  -Game valve -Map boot_camp `
  -RelayPort 27140 -ServerPort 27141 `
  -OutputRoot '.\manual-artifacts\research-runtime-capture' `
  -MaximumDurationSeconds 90 `
  -ServerProfileId 'steam-hlds-10210-no-mode-banner-v1'
```

After a successful publication, the ordinary application command is:

```powershell
.\build\bin\Release\hlclient.exe `
  --renderer null `
  --runtime-replay-capture `
  '.\manual-artifacts\research-runtime-capture\<run-id>'
```

Both commands are explicit and inert by default. The second is offline: it
does not start Steam, HLDS or the relay and is mutually exclusive with live
connect. Diagnostic OpenGL assets are intentionally outside this H validation
boundary.

## Completed H and local-asset continuation

H is complete for functional capture `ededd068a0444ff497d98204eacec8a4`:
1184 delivered datagrams, 1169 replay payloads, seven schemas, 212 baseline
entities and 323 applied runtime records with zero failures/pending records.
The historical pre-signed-wire-correction result reproduced 29 entities,
health 200, slot-2 clip 34 and canonical hash `14031366596970435596` before
and after I. The M4.7.2F protocol correction keeps 323/323 records and the
same canonical field set, but correctly interprets public GoldSrc signed
scalars as sign plus magnitude; the current capture result is health 100 and
canonical hash `7014210005320501317`. Earlier capture failures describe
historical attempts, not this final functional result.
This capture remains explicitly ineligible for campaign evidence.

H's NullRenderer result did not establish original-asset rendering. I now
retains the existing parser's owning resource list and uses the same replay
session with explicit local assets and the existing OpenGL path; see
[verified local-asset scene slice](STOCK_CAPTURE_LOCAL_ASSET_REPLAY.md).
Neither milestone claims live multiplayer or universal stock compatibility.
