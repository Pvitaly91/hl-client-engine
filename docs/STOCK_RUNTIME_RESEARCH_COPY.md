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

The following categories remain fail-closed:

- external link target;
- link cycle or excessive link depth;
- internal file symlink;
- volume mount point or unsupported reparse tag;
- alternate data stream;
- UNC, remote-volume, or substituted-drive source;
- entry/byte bound exhaustion.

## Materialization v2

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
`source_changed_during_materialization` and removes staging.

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
  `hlclient.stock-runtime-research-preparation.v2`.

The v2 manifest records metadata only: topology profile, anonymized source-root
identity fingerprint, entry/byte counts, materialized link/hardlink counts,
inventory digest, private client/server identity references, unlinked
destination status, unchanged-source status, and preparation result. It stores
no paths. Existing v1 marker consumers remain valid; consumers that inspect the
manifest must continue accepting the prior v1 schema and apply stricter v2
checks when v2 is present. Capture preflight performs those stricter checks: it
validates the v2 field contract and pending record, requires the separate
commit marker, and binds the live prepared-tree inventory and binary identities
to the manifest before active launch. The pending and commit records are
metadata and are excluded from the source inventory digest.

## Commands

```powershell
.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -InspectSourceTopology `
  -SourceHalfLifeRoot "D:\Steam\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life"

.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -SourceHalfLifeRoot "D:\Steam\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life"
```

Inspection exits zero after a complete safe or unsafe diagnostic. Materialize
exits zero only for `exact-materialized-copy-verified`; typed topology,
mutation, destination, publication, or cleanup failures exit nonzero without a
verified published destination. Ordinary helper-owned staging is removed on
failure; an identity-mismatched replacement path is deliberately preserved
rather than deleted by name.
