# Stock runtime external-target approval

## Separate authorization boundary

An external-target review is not permission to copy. M4.7.1.1.3 requires a
separate explicit approval manifest created from one exact local review:

```powershell
.\scripts\approve_stock_runtime_external_targets.ps1 `
  -ReviewRoot `
    ".\manual-artifacts\stock-runtime-source-review\<review-id>" `
  -ConfirmExternalMaterialization `
    HLCLIENT_APPROVE_REVIEWED_EXTERNAL_TARGETS_V1
```

The approval script never launches Steam, `hl.exe` or `hlds.exe`, never creates
the research copy, and never modifies the source. It publishes a new approval
leaf without replacing an existing one. The approval is explicit, local,
bounded and expiring. It is not a reusable policy switch and there is no
"approve all external links" mode.

The case-sensitive confirmation phrase is exact. Omitting `-LifetimeHours`
uses 24 hours; the optional accepted range is 1 through 168 hours (the hard
seven-day maximum). An expired approval is rejected as
`external_target_approval_expired`. The helper publishes exactly
`external-target-approval.json`, schema
`hlclient.stock-runtime-external-target-approval.v1`, in that review-ID
directory without replacing an existing approval.

The approval binds the exact review schema and digest, source-root physical
identity, full source inventory/topology digest, every eligible escaped-link
record, approval policy version and creation/expiry bounds. Its canonical
serialized digest is recorded by preparation manifest v3. The approval records
no raw source-link or target path, glob or external-root allowlist. Mutation
before materialization, expiry, a different source, an
added or removed link, target retargeting, identity/content drift, an unknown
property or a non-exact artifact set invalidates it. The native validator
rereads the request, summary and every private target record through no-follow
handles before accepting the approval. Application-root and AppID-70 manifest
identity/content evidence is part of each private target inventory binding, so
changing the source or secondary-install provenance also invalidates reuse.

The canonical approval JSON has the exact top-level properties `schema`,
`review-schema`, `review-version`, `review-root-fingerprint`,
`review-digest-sha256`, `source-root-fingerprint`, `source-inventory`,
`review-nonce`, `approval-nonce`, `approval-timestamp-unix-seconds`,
`expiration-unix-seconds`, `approval-count`, `confirmation-profile`,
`implementation-profile` and `approved-targets`. Each approved-target binding
contains only `ordinal`, `link-identity-sha256`, `target-identity-sha256`,
`target-inventory-sha256` and the exact eligible `classification`. Unknown,
duplicate or missing JSON properties are rejected. `review-schema` is exactly
`hlclient.stock-runtime-external-target-review-summary.v1` and
`review-version` is exactly `1`.

## Approved materialization

After explicit approval, the only permitted materialization surface is:

```powershell
.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -SourceHalfLifeRoot "D:\Steam\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life" `
  -ExternalTargetApprovalManifest `
    ".\manual-artifacts\stock-runtime-source-review\<review-id>\external-target-approval.json"
```

The destination must not exist. The native materializer independently verifies
the approval and repeats the source and target inventories; PowerShell alone is
not a trust boundary. Approval permits only the exact reviewed, eligible,
non-executable and non-mutable external directory targets. Their bytes are
copied into ordinary independent destination directories. No symlink,
junction, hardlink or alternate stream is preserved.

Before creating any destination-parent component, the materializer retains
non-delete-share handles for the exact source and approved target roots. It
also holds every provenance manifest and the exact source
`libraryfolders.vdf` membership record read-only without write/delete sharing,
then repeats exact approval validation while those guards are live. Their
identity and content are revalidated immediately before commit, and the guards
remain live through commit publication, preventing a root or provenance
artifact from being moved or rewritten into the destination transaction.

All other external links still fail. Approval cannot waive root/destination
isolation, fixed-volume, Steam-overlap, size/depth, cycle, ADS, reparse-tag,
source-stability, destination-unlinked or no-replacement checks. A failure
publishes no authorizing isolation marker.

Successful approved materialization publishes the exact v3 preparation
manifest, `hlclient.stock-runtime-research-preparation.v3`. Materialization
validates the unexpired approval and current source/target identities and
inventories, then records the approval digest and path-free result attestation
in v3. Keep the ignored review directory and approval artifact locally after
publication: active preflight searches only the repository-local
`manual-artifacts/stock-runtime-source-review/<32-lower-hex-id>/` layout for
`external-target-approval.json`, requires exactly one artifact whose freshly
computed SHA-256 equals `external_approval_sha256`, and rejects an absent,
ambiguous, linked, ADS-bearing, non-ordinary or over-64-KiB artifact. The
review parent/root path must also remain non-reparse.

This happens before any WFP/process work, together with validation of the
closed v3 field set and live prepared destination. The local approval artifact
is an input to preflight but its path and private review directory are not
published or carried into run evidence. The approval and v3 metadata establish
filesystem provenance only: they confer no runtime-message meaning, entity
authority, canary acceptance or evidence status.

Approval schema, expiration and live source/target bindings are validated when
the materializer publishes the copy. The later capture gate proves that the
same local approval bytes still back the v3 digest; it does not reinterpret the
private review or extend the approval lifetime.

Approval never authorizes external executable/code or mod content and never
changes a production protocol codec. Runtime-body parsing remains M4.7.1.2
work; stock usercmd remains M4.7.2 work.
