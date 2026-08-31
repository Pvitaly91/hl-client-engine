# Stock runtime research-copy topology and materialization

## Boundary

`hlclient_stock_research_copy` is the Windows-only source topology inspector
and materializer used by `prepare_stock_runtime_research_copy.ps1`. It is a
filesystem safety boundary, not a stock protocol component. It opens no
network connection and does not launch Steam, `hl.exe`, or `hlds.exe`.

Inspection is bounded and read-only. It creates neither the requested
destination nor a staging directory or marker. Its public output contains only
topology categories and counts—never source/target paths, account names, file
contents, or binary hashes.

## Handle-based identity

Each sensitive entry is opened with `CreateFileW` and
`FILE_FLAG_OPEN_REPARSE_POINT`. `GetFileInformationByHandleEx` supplies the
volume serial, full 128-bit file ID, attributes, reparse tag, size, timestamps,
directory state, and hardlink count. A second followed handle plus
`GetFinalPathNameByHandleW` identifies a supported reparse target. Security
containment uses the handle-derived volume serial plus its normalized
`VOLUME_NAME_NONE` volume-relative path, rather than a DOS drive spelling, so
drive aliases cannot change source/link or destination/library overlap
decisions. Reparse data is queried with `FSCTL_GET_REPARSE_POINT` to distinguish
ordinary directory junctions from volume mount points.

Root and ancestor junction/symlink aliases are allowed only when the final root
is a stable directory on a local fixed volume. An internal directory junction
or directory symlink is traversed only when its followed handle is physically
inside that canonical root on the same volume, the target identity is not in
the active recursion stack, and the reparse-depth bound is not exceeded. Every
link/target witness is checked again during copying and final source inventory.

The following categories remain fail-closed by default:

- external link target;
- link cycle or excessive link depth;
- internal file symlink;
- volume mount point or unsupported reparse tag;
- alternate data stream;
- UNC, remote-volume, or substituted-drive source;
- entry/byte bound exhaustion.

M4.7.1.1.3 defines one narrow exception for an exact escaped directory target
that has passed the separate local review and explicit approval workflow. It
does not make arbitrary external links safe. Only eligible non-executable,
non-mutable directory content can be approved; every other category above
remains non-overridable. See
[external-target review](STOCK_RUNTIME_EXTERNAL_TARGET_REVIEW.md) and
[external-target approval](STOCK_RUNTIME_EXTERNAL_TARGET_APPROVAL.md).

M4.7.1.1.4 places an exact no-follow provenance layer before the exceptional
review path. Mount-point and symbolic-link payloads are validated literally;
other tags stay bounded opaque observations and are not followed. A readable
dangling or unsupported link can complete a diagnostic review as ineligible,
but it cannot be approved or materialized. `ERROR_PATH_NOT_FOUND` is not
eligibility, and counts unavailable because no complete target inventory was
possible are reported as unavailable rather than zero. See
[Windows reparse provenance](WINDOWS_REPARSE_PROVENANCE.md) and
[source eligibility](STOCK_RUNTIME_SOURCE_ELIGIBILITY.md).

## Verified materialization

The complete source inventory is measured before mutation. Each regular file
is reopened by its verified physical path and compared with the inventory
identity. While that handle remains open, the helper copies bounded chunks to
a randomly named new file, flushes it, verifies its SHA-256, size, file ID,
single-link state, ordinary-file state and stream inventory, then renames the
open file without replacement. Source hardlinks therefore produce independent
destination files; link topology is never preserved.

After all files are copied, the helper independently inventories staging and
requires exact entry/byte/content-inventory agreement, zero reparse points,
zero hardlinks and zero ADS. It then repeats the source inventory. Any metadata,
identity, target, stream, byte, or content change yields
`source_changed_during_materialization` and removes staging. For a reviewed
external target, approval/source/target drift instead yields the combined typed
failure `source_or_external_target_changed_during_materialization`.

Only a new destination is accepted. Its parent chain must consist of ordinary
directories on a local fixed, non-substituted volume. Source/destination and
Steam-library overlap are rejected using physical handle identity before any
destination directory is created. Configured Steam roots are discovered from
bounded registry values plus bounded `libraryfolders.vdf` parsing; an embedding
host may add explicit roots for deterministic policy tests. The verified
staging directory is renamed to the destination without replacement while it
contains a typed `.hlclient-research-pending` record but not the authorizing
isolation marker. A recursive parent change witness, identity-pinned
directories, and per-file Filter oplocks then protect final inventory and byte
revalidation. The exact isolation marker is created by verified temporary-file
rename as the final success operation. A failed tree therefore cannot pass
active preflight even if an external no-delete-share handle prevents immediate
quarantine. Cleanup is bound to the
retained no-follow handle and expected file identity. If an untrusted actor
substitutes the staging or publication path, the helper refuses recursive
path-based deletion: the unproved replacement is deliberately preserved and
treated as quarantined for operator inspection while the operation fails
closed. It is never mistaken for a verified destination.

## Preparation manifest compatibility

New copies contain:

- `.hlclient-research-pending` with bounded metadata-only schema
  `hlclient.stock-research-copy-pending.v1`; it never authorizes use;
- the unchanged `.hlclient-research-isolated` marker text
  `HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1`, published only as the final commit
  record;
- `.hlclient-research-preparation.json` with schema
  `hlclient.stock-runtime-research-preparation.v3`.

All new materializations publish v3, including sources with only ordinary or
contained-link topology. The exact v3 property set is:

- `schema`, `marker`, `preparation_profile`;
- `source_root_identity_fingerprint`, `source_inventory_entries`,
  `source_inventory_bytes`, `source_inventory_sha256`;
- `contained_materialized_link_count`,
  `approved_external_materialized_link_count`, `source_hardlink_count`;
- `destination_entry_count`, `destination_byte_count`,
  `destination_inventory_sha256`, `destination_reparse_count`,
  `destination_hardlink_count`, `destination_ads_count`;
- `external_approval_sha256`, `external_classification_summary`,
  `executable_target_count`, `mutable_state_target_count`;
- `source_unchanged_status`, `external_targets_unchanged_status`,
  `evidence_eligibility`, `external_target_profile`;
- `client_binary_private_identity_reference`,
  `server_binary_private_identity_reference`, `paths_recorded`,
  `preparation_status`.

For an ordinary/contained copy, `preparation_profile` is
`ordinary-or-contained-v3`, the external approval digest is the all-zero
SHA-256 value, the external classification/profile are `none`, and status is
`exact-materialized-copy-verified`. For a reviewed copy, the exact profile is
`reviewed-external-targets-v1`, classification is
`eligible_non_executable_asset_tree`, public run profile is
`reviewed-non-executable-v1`, and status is
`exact-reviewed-materialized-copy-verified`. Both successful profiles require
`evidence_eligibility=eligible`, `paths_recorded=false`, verified unchanged
statuses and zero destination reparse/hardlink/ADS counts.

The evidence-eligibility enum also reserves
`ineligible_external_code`, `ineligible_mutable_state`,
`ineligible_cross_application` and `ineligible_unknown_external_target`.
Materialization fails before publication for those states; capture rejects any
non-`eligible` v3 value as `research_copy_not_evidence_eligible` before WFP or
process launch.

Materialization validates the exact unexpired approval and current source and
target identities/inventories before publishing v3. Capture preflight then
validates the closed v3 field contract, pending record, commit marker, live
prepared-tree inventory, zero destination links/ADS and private binary
identity references. For `reviewed-external-targets-v1`, it additionally scans
the exact repository-local ignored review layout, accepts exactly one bounded
ordinary single-link/no-ADS `external-target-approval.json` under a
32-lower-hex review root, hashes its current bytes and requires equality with
`external_approval_sha256`. Removing, linking, changing or duplicating the
matching approval therefore blocks active capture before WFP/process work.
This later digest-presence check does not extend approval lifetime: expiration
and live source/target bindings are enforced by materialization before copy
publication.

Preflight retains only path-free `external_target_profile` and
`external_target_count` in run/canary/campaign provenance; the locally checked
approval path, private review paths and identities do not enter evidence. The
pending and commit records are metadata and are excluded from the inventory
digest.

Capture readers retain the prior v1 and v2 preparation schemas for their
original strict contracts. Neither legacy schema can represent or authorize a
reviewed external target.

## Commands

```powershell
.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -InspectSourceTopology `
  -SourceHalfLifeRoot "D:\Steam\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life"

.\scripts\validate_stock_runtime_candidate_source.ps1 `
  -SourceHalfLifeRoot "F:\SteamLibrary\steamapps\common\Half-Life" `
  -AppManifestPath "F:\SteamLibrary\steamapps\appmanifest_70.acf" `
  -ExpectedAppBuild 15961492

.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -SourceHalfLifeRoot "F:\SteamLibrary\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life"

.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -ReviewExternalTargets `
  -SourceHalfLifeRoot "F:\SteamLibrary\steamapps\common\Half-Life" `
  -ReviewOutputRoot ".\manual-artifacts\stock-runtime-source-review"

.\scripts\approve_stock_runtime_external_targets.ps1 `
  -ReviewRoot `
    ".\manual-artifacts\stock-runtime-source-review\<review-id>" `
  -ConfirmExternalMaterialization `
    HLCLIENT_APPROVE_REVIEWED_EXTERNAL_TARGETS_V1

.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -SourceHalfLifeRoot "F:\SteamLibrary\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life" `
  -ExternalTargetApprovalManifest `
    ".\manual-artifacts\stock-runtime-source-review\<review-id>\external-target-approval.json"
```

The validation, approval, and both materialization examples use one source that
already passed the candidate gate. The current ineligible `D:` source is shown
only in the read-only topology command here (and in the separate diagnostic
review guide) and must not be approved or materialized.

Inspection exits zero after a complete safe or unsafe diagnostic. External
review also exits zero after a complete ineligible diagnostic; that does not
permit approval. Materialization exits zero only for
`exact-materialized-copy-verified` or
`exact-reviewed-materialized-copy-verified`; typed topology, approval,
mutation, destination, publication or cleanup failures exit nonzero without a
verified published destination. Ordinary helper-owned staging is removed on
failure; an identity-mismatched replacement path is deliberately preserved
rather than deleted by name.

These review/approval commands are the M4.7.1.1.3 contract; this repository
does not claim that a real installation has completed them or that a real
canary/evidence run has consequently been accepted.
