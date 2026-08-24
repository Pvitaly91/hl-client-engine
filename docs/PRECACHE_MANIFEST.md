# Immutable metadata-only precache manifest

`PrecacheManifestState` is the bounded engine-owned index built by M3.2.2 after
the existing opcode-5 response has been acknowledged and the next server
payload boundary has been retained. Despite its name, it is not an asset cache:
it owns metadata only and performs no file-content read, parser dispatch,
download, cache write, GPU upload, or renderer integration.

## Shape

The state owns:

- manifest entries in exact resource-list wire order;
- independent sparse slot tables for sound, model, generic, event-script, and
  metadata-only decal namespaces;
- exact world-resource selection metadata;
- readiness and impact summaries;
- full-profile completeness and separate world-geometry readiness;
- bounded source geometry and compatibility/evidence profiles.

Each slot stores an ordered-entry offset, not another resource object. For each
type, `slot_count` is checked `max_index + 1`; holes remain empty and indexes
are never compacted. The same numeric index in different types is valid.
These are project safety limits, not stock-engine maxima:

| Limit | Default | Hard cap |
| --- | ---: | ---: |
| `maximum_readiness_entries` | 1,024 | 4,095 |
| `maximum_manifest_entries` | 1,024 | 4,095 |
| `maximum_slots_per_type` | 4,096 | 4,096 |
| `maximum_total_slots` (`maximum_manifest_total_slots` policy) | 20,480 | 20,480 |
| `maximum_manifest_events` | 2,048 | 8,192 |
| `maximum_locator_virtual_name_bytes` | 1,024 | 1,024 |

All limits are validated before reserve or resize.

The state contains no native or absolute path, file handle, socket, file bytes,
digest, parsed resource, `AssetManager`, `ClientWorldState`, or renderer object.
Owning values and slot offsets make the snapshot stable after its list,
inventory, and server-info inputs are destroyed.

## Completeness

Completeness is deterministic and retains all detailed counts. The published
outcomes are:

- `complete_for_supported_local_profile`;
- `world_ready_but_incomplete`;
- `incomplete_missing_resources`;
- `blocked_unsafe_resources`;
- `unsupported_profile`;
- `local_io_failure`.

When several non-fatal problems coexist, the deterministic precedence is
`blocked_unsafe_resources`, then `local_io_failure` (including ambiguous local
matches), then `unsupported_profile`, then a missing-resource outcome. Missing
resources select `world_ready_but_incomplete` when the exact map is ready and
`incomplete_missing_resources` otherwise. If no higher-priority condition
applies, readiness must explicitly report a complete supported local profile to
select `complete_for_supported_local_profile`; otherwise the manifest falls
back conservatively to `incomplete_missing_resources`.

Structural list/inventory or ServerInfo/list correlation failures are fatal and
publish no manifest; their typed classification is
`invalid_server_resource_correlation` where applicable. Metadata-only decals
do not block completeness. An exact locally available BSP can make world
geometry ready even when other file-backed entries keep the full profile
incomplete.

## Same-session stop point

`--stop-after precache-manifest` requires
`--resource-consistency-provider local`, explicit `--basedir`, and the existing
authentication inputs; `--game` defaults to `valve`. The continuation retains
the same transport, netchan driver, authentication lifetime, response state,
and local-resource environment. After the already implemented response and
covering ACK, it builds local metadata and queues no new network message.

The application logs aggregate counts, slot counts, world status/index, and
completeness only. It does not log resource names, native paths, stable identity
bytes, digests, file contents, or authentication material. Exit status is zero
only for a complete supported local profile; a published incomplete, blocked,
unsupported, or local-I/O snapshot is a typed nonzero result.

## Approved continuation

The direct `--stop-after precache-manifest` contract remains metadata-only and
performs zero asset-source opens. M3.2.3's separate
`--stop-after asset-dispatch` continuation may consume only the exact selected
world locator through verified exact-root reopening. It may proceed when
`world_geometry_ready()` is true even if unrelated resources make the full
manifest incomplete. M4.1's `asset-dispatch` route then imports a valid BSP v30
with the production world importer, while `world-geometry` additionally
requires a non-empty owning CPU result. Neither route mutates this immutable
manifest or sends a packet after its publication. See
[approved asset sources](APPROVED_ASSET_SOURCE.md),
[asset importer dispatch](ASSET_IMPORTER_DISPATCH.md), and
[CPU world geometry](CPU_WORLD_GEOMETRY.md).
