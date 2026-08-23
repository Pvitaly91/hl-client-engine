# Local resource readiness

M3.2.2 converts the owning M3.2.1 `ResourceListState` and
`LocalResourceInventoryState` snapshots into a strict, metadata-only local
readiness snapshot. Readiness means that a supported file-backed entry had a
safe local candidate when the snapshot was built. It does not mean that the
file is valid, matches the server, is parseable, or is ready for a renderer.

## Correlation before policy

The builder first verifies equal entry counts and, at every position, the exact
wire ordinal, resource type, resource index, and original wire-name byte length.
It also checks the inventory status/metadata matrix. A mapped virtual name is
re-derived from the untrusted list bytes, and its ID, byte length, and component
count must equal the inventory metadata. Resolved entries must have a valid root
ID, stable identity, and bounded local size; unresolved entries may not carry
resolved metadata. Any mismatch is a typed fatal error and publishes neither a
partial readiness state nor a partial manifest.

The opaque 24-bit resource size code and flag bit 0 never influence readiness,
allocation, required/optional policy, or local size.

## Per-entry status and aggregate impact

The immutable state preserves exact wire order and distinguishes:

- `ready_local_file` — a supported mapped file was resolved;
- `metadata_only` — the supported decal mapping carries no local file;
- `missing_local_file` — a safe local candidate was not found;
- `unsafe_name` — the byte spelling failed the sandbox policy;
- `unsupported_name_encoding` and `unsupported_mapping`;
- `ambiguous_local_match`;
- `local_io_error`.

The separate aggregate impact vocabulary is `locally_usable`, `metadata_only`,
`incomplete`, `security_blocked`, `unsupported_profile`, and
`local_io_failure`. Missing never means `download_required`; M3.2.2 has no
download decision or download/cache implementation. Metadata-only decals do
not block the supported local profile.

## Approved locators and shared environment

A ready file-backed entry receives a path-safe owning `LocalResourceLocator`.
It contains only its validated root ID, owning approved virtual name, expected
equality-only identity, expected size, and compatibility profile. It contains
no native path, handle, bytes, digest, or raw server path.

`LocalResourceEnvironment` owns the validated resolver/root handles and their
IDs. Provider preparation, inventory construction, manifest construction, and
future verified reopening share that single environment. A locator can be
reopened only by its associated environment. Verified reopen uses the exact
recorded root, never searches a fallback root, applies the original sandbox and
reparse rules, and compares the newly opened handle's identity and size. A
missing target returns `locator_target_missing`; replacement or size drift
returns `stale_locator`. Reopen returns a move-only `LocalReadOnlyFile` without
reading its contents.

The manifest session retains both the immutable manifest and the environment.
A copied manifest may preserve metadata after its inputs are destroyed, but a
locator is usable only while the associated environment/session remains alive.

## World readiness

World readiness is separate from full-profile completeness. The map spelling
comes only from `ServerInfoState::map_file_path()`, is validated as untrusted
bytes, and must match exactly one resource-list entry whose type is `model`.
The implementation does not normalize case or separators, strip or add
`maps/`, infer `.bsp`, select by extension, or assume a fixed index.

An exact model entry with a missing local BSP publishes an incomplete snapshot
with `local_map_missing`. Invalid map metadata, no exact list entry, duplicate
exact entries, or a sole non-model entry is a typed structural error and does
not claim world readiness. The world can be ready while other local sounds or
models are missing.

## Explicit boundary

M3.2.2 opens files only during the existing sandboxed inventory lookup
and only metadata is retained. It does not hash general resources, validate
asset contents, parse BSP/WAD/MDL/SPR/WAV data, load an asset, download or cache
anything, mutate `ClientWorldState`, or call a renderer. M3.2.3 consumes this
evidence only through a distinct one-source continuation: the exact world entry
must remain ready and its locator is reopened and validated before importer
dispatch. Readiness alone is never treated as parsed-format validity.
