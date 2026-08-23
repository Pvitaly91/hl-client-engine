# Asset importer dispatch

M3.2.3 connects an approved owning `AssetSource` to the existing typed importer
registries without adding a production format parser. Format recognition
remains importer-owned: signature, version, and structure are primary evidence;
the extension is only a hint.

## Roles and plans

GoldSrc resource-list metadata maps to a dispatch role only through the
immutable manifest and exact world selection:

| Resource evidence | Role | Registries allowed |
| --- | --- | --- |
| Exact selected world model | `world` | world only |
| Other model | `model_or_sprite` | model and sprite |
| Sound | `audio` | audio only |
| Decal | `metadata_only` | none |
| Generic or event script | `unsupported` | none |

No extension changes a `ResourceType`, creates a world role, or introduces an
image mapping. The plan retains the exact approved manifest entry privately,
plus the resource type/index/ordinal, role, allowed categories, exact-world
flag, and manifest compatibility/evidence profiles. Source opening consumes
the plan's retained entry evidence, so a world role cannot be replayed against
a merely metadata-equal entry from another manifest.

## Pure probe and deterministic selection

Each registry exposes a pointer-free pure probe result containing its state,
best confidence, best priority, and bounded, deterministically sorted top
candidates. Probe calls every registered importer exactly once and never calls
`import()`.

Selection preserves the M0.1 policy: confidence first, explicit signed priority
second, and an exact rank tie is ambiguous rather than registration-ordered.
The dispatcher's cached selection invokes only the unique selected importer and
does not probe a second time. For `model_or_sprite`, model and sprite candidates
are compared globally. A lower-ranked category tie cannot block a unique
higher-ranked candidate; a tie at the global best rank is terminal ambiguity.
Candidate diagnostics are category-qualified, such as `model:synthetic-mdl`.

The protocol-neutral assets-layer dispatcher implements only this ranking
mechanism. GoldSrc production composition calls it through
`ApprovedAssetImporterDispatcher`, which accepts the bound
`ApprovedAssetSource` and `AssetDispatchPlan` together and rejects any metadata,
role, or evidence-profile disagreement before a registry probe. The retained
registries must outlive the stage and remain structurally immutable during
dispatch.

No candidates produce `importer_not_registered`, which is an expected boundary
for the production M3.2.3 composition because it intentionally registers no
world importer. Once an importer has been selected, malformed data,
`UnsupportedFormat`, an explicit import failure, or an exception is an import
failure rather than a no-importer boundary.

## Stage and runtime boundary

`PrecacheAssetDispatchStage` privately continues the retained manifest stage.
It may continue when `world_geometry_ready()` is true even if unrelated sounds
or models leave the full manifest incomplete. It opens only the selected world
source, probes the world registry, and publishes either `asset_imported` or
`importer_boundary_reached`. Both are successful outcomes for
`--stop-after asset-dispatch`; missing/unsafe/ambiguous world evidence, stale
locators, read failures, importer ambiguity, and selected-importer failures are
nonzero outcomes.

The local read sends no keepalive, spawn, download, or asset-ready command. The
same socket, driver, endpoint, and authentication lifetime remain retained
during the local operation and are finalized exactly once at its terminal
boundary. The transmitted-packet count cannot increase after manifest
publication. The coordinator drains the bounded child event stream after each
update; direct stage users can poll the same events and receive fail-closed
backpressure if they do not. The CLI asset-dispatch route stops before renderer
work, and the stage never calls `AssetManager`, a renderer, OpenGL, or GPU APIs.
