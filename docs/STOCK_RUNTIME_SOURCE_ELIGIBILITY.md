# Stock runtime source eligibility

## Candidate gate

`hlclient_stock_source_eligibility_check` is the read-only gate used before a
Half-Life installation can be considered for research-copy materialization.
The PowerShell wrapper is:

```powershell
.\scripts\validate_stock_runtime_candidate_source.ps1 `
  -SourceHalfLifeRoot "F:\SteamLibrary\steamapps\common\Half-Life" `
  -AppManifestPath "F:\SteamLibrary\steamapps\appmanifest_70.acf" `
  -ExpectedAppBuild 15961492
```

The validator creates no output directory or approval, launches no process,
does not configure WFP or access the network, and does not modify the source.
Its public records contain no paths or hashes.

Eligibility requires a complete bounded topology on a local fixed volume, no
root/ancestor, escaped, dangling or unsupported reparse topology, no alternate
data streams, and a source that is not already marked as a prepared research
copy. It also requires `hl.exe` version 1.1.1.1 and `hlds.exe` version 4.1.1.1,
both Win32/x86 with valid Authenticode, plus an exact AppID 70 manifest with
build ID 15961492.

The source must be the exact no-alias
`<library>\steamapps\common\Half-Life` root. The manifest must be its matching
ordinary, single-link, no-ADS
`<library>\steamapps\appmanifest_70.acf` on that same proven local fixed-volume
Steam tree. A UNC/device/SUBST manifest, a manifest reached through a reparse
component, or a valid AppID-70 manifest from another installation is rejected
before its bytes are opened. This preserves the validator's no-network
contract rather than using unrelated manifest contents as source provenance.

Only

```text
[stock-source] research-copy-eligible=true
[stock-source] result=success
```

with exit code 0 permits a later materialization attempt. A completed
ineligible assessment uses exit code 1. Invalid invocation uses exit code 2.
Failure to observe a required input also fails closed. `ERROR_PATH_NOT_FOUND`
is not eligibility; a dangling link cannot be treated as an optional or empty
directory.

## Operational decision

Diagnostic completion differs from approval. An ineligible source receives no
approval artifact, destination, preparation marker or campaign evidence. An
unsupported tag remains opaque and is not followed. Inventory counts that
could not be established are unavailable, not zero.

If the primary Steam installation is ineligible, the recommended next step is
a clean secondary Steam installation on a local fixed volume, followed by this
same validator. Only after it passes may the operator materialize a research
copy and run the elevated isolation preflight, canary, reconnect test and
24-run evidence campaign. The source validator does not infer runtime semantics
and does not begin M4.7.1.2 or M4.7.2.
