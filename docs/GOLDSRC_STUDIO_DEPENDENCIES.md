# GoldSrc Studio companion dependencies

Some Studio v10 models are split across an approved main `IDST`, a texture
companion, and external sequence groups. M4.5.2 handles that layout with a
caller-driven, transactional, read-only operation. The pure Studio importer
never accesses a filesystem: importing a valid split main source alone returns
the typed `AssetErrorCode::ExternalDependencyRequired` result.

The general visual operation first performs the central model-or-sprite
selection against immutable approved bytes. It invokes the exact selected
caller-owned Studio instance once with the operation's parser limits. No
bundle capability or exact-root lookup exists for a sprite or a successful
self-contained Studio result. Exact-root main evidence is revalidated and the
bundle operation is created lazily only after the selected Studio invocation
returns `ExternalDependencyRequired`.

Dependency preflight reads only the validated main header and bounded metadata.
Its pure path-free plan records whether textures require a companion, the
ordered unique sequence-group ordinals, and expected source count. Before any
companion is opened, the visual-resource layer turns that into an owning
resolved plan bound to the approved main root ID, virtual name, stable file
identity, content fingerprint, prederived sibling names, and fixed
compatibility/evidence profile. For an approved virtual name `models/foo.mdl`,
the only permitted derived siblings are:

- `models/fooT.mdl` for textures;
- `models/foo01.mdl` through `models/foo15.mdl` for groups 1 through 15.

The directory, stem, `.mdl` suffix, and exact two-decimal group spelling are
derived from the approved main virtual name and revalidated as safe virtual
resource names. Raw `studiohdr_t.name`, sequence-group descriptor labels, or
other embedded bytes are never path input. There is no directory scan, current
directory, Steam-library discovery, caller-supplied companion path, or
extension substitution.

Every sibling is resolved only through the exact root ID selected for the main
model. A miss cannot fall through from a mod root to the `valve` fallback. The
operation captures identity and size from a read-only exact-root handle, closes
it, creates a provenance-bound `LocalResourceLocator`, then uses the existing
verified `LocalAssetSourceOpenOperation`. Only one companion is open at a time.
The effective cumulative byte budget is the stricter of the source-bundle and
Studio-parser limits and is enforced from advertised size before each open and
again from retained bytes before the source is kept, so a later companion is
never opened after the budget is exhausted.

The published `GoldSrcStudioModelSourceBundle` owns the main bytes, optional
texture bytes, ordered sequence-group bytes, source fingerprints, and the
verified root ID, virtual-resource ID, and stable file identity for every
source. Its public views are const-only and it owns no handle or native path.
Duplicate groups,
wrong ID/version/ordinal, declared-length mismatch, stale identity, root
mismatch, source drift, unexpected companion role, a missing dependency, or
aggregate-byte overflow fails without a partial model. Cancellation and
timeout are explicit caller-driven states; there is no worker thread or
production sleep.

The offline `hlclient_goldsrc_asset_check` tool accepts only `--basedir`,
`--game`, a safe `--asset`, and `--kind auto|model|sprite`, with optional
integer animation sampling. It uses the same local sandbox, exact-root bundle,
and production importers. The verifier script runs explicit user-owned assets
twice and checks deterministic summaries plus unchanged file hashes, sizes,
write times, and root inventories. Neither tool writes, scans for arbitrary
companions, renders, or uses the network.
