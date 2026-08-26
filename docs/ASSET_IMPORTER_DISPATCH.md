# Asset importer dispatch

M3.2.3 connects an approved owning `AssetSource` to the typed importer
registries. M4.1 registers the first production format implementation,
`GoldSrcBspWorldImporter`, in the caller-owned world registry. M4.2 begins only
after that importer has published a validated owning `WorldAsset`; it is a
typed GoldSrc texture-resolution stage, not another importer-registry category.
Format recognition remains importer-owned: signature, version, and structure
are primary evidence; the extension is only a hint.

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

`AssetImporterDispatcher::select` exposes that same probe-only global result;
`import_selected` consumes it without probing again, and the ordinary
`dispatch` call is implemented by composing those two operations. A bounded
model-specific callback form keeps the selected registry object private while
allowing a composition root to apply caller-owned constraints. The callback is
synchronous, registry-normalized, and invokes the exact selected instance once.

The protocol-neutral assets-layer dispatcher implements only this ranking
mechanism. GoldSrc production composition calls it through
`ApprovedAssetImporterDispatcher`, which accepts the bound
`ApprovedAssetSource` and `AssetDispatchPlan` together and rejects any metadata,
role, or evidence-profile disagreement before a registry probe. The retained
registries must outlive the stage and remain structurally immutable during
dispatch.

No candidates produce `importer_not_registered`. That historical boundary
remains covered by explicitly empty test registries, but a valid BSP v30 world
now selects production ID `goldsrc-bsp-v30` at named priority `300`. The
reusable `register_builtin_asset_importers` helper in
`hlclient_goldsrc_builtin_importers` registers exactly one BSP, one Studio, and
one sprite importer without a global registry. Once an importer has been
selected, malformed data,
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

`--stop-after world-geometry` uses the same stage and retained session but
accepts only an imported, non-empty `WorldAsset` with valid triangle indices,
surfaces, and finite bounds. It reports bounded geometry/statistics only. A
valid production BSP therefore ends as `asset_imported`, while malformed BSP,
unsupported version, ambiguous importer, empty geometry, or source failure is
nonzero.

The local read sends no keepalive, spawn, download, or asset-ready command. The
same socket, driver, endpoint, and authentication lifetime remain retained
during the local operation and are finalized exactly once at its terminal
boundary. The transmitted-packet count cannot increase after manifest
publication. The coordinator drains the bounded child event stream after each
update; direct stage users can poll the same events and receive fail-closed
backpressure if they do not. The CLI asset-dispatch route stops before renderer
work, and the stage never calls `AssetManager`, a renderer, OpenGL, or GPU APIs.

The BSP target itself depends only on the neutral asset API. Parsing is strict
and transactional; its output owns CPU geometry and texture-reference metadata
without retaining source bytes or native paths. See
[GoldSrc BSP v30](GOLDSRC_BSP_V30.md) and
[CPU world geometry](CPU_WORLD_GEOMETRY.md).

For the explicit `world-textures` route, the parent stage keeps the approved
BSP source alive alongside the imported world, then invokes the separate
texture operation. That operation cross-checks exact BSP texture ordinals,
decodes used embedded records, and opens only required safe WAD basenames
through the retained local environment. It does not register a generic image
importer, re-probe the BSP, route WADs through `AssetManager`, or change the
result of `asset-dispatch`/`world-geometry`.

The texture stage preserves the same session and sends no packet after manifest
publication. It may publish a typed incomplete owning set for missing archives
or textures, but malformed input and policy failures publish no partial set.
The CLI returns success only for complete world-material bindings and exits
before renderer initialization. See
[world texture resolution](WORLD_TEXTURE_RESOLUTION.md).

## Studio and sprite production importers

M4.5.2 places the one caller-owned built-in registration composition in
`hlclient_goldsrc_builtin_importers`. It installs exactly
`goldsrc-bsp-v30` in the world registry,
`goldsrc-studio-mdl-v10` in the model registry, and `goldsrc-sprite-v2` in the
sprite registry. All use named priority 300. There is still no global registry,
test importer, registration-order winner, or extension-only match.

`IDST` and `IDSP` probes are signature/version/structure first; `.mdl` and
`.spr` add only one confidence point. `IDSQ` is never a top-level model. The
existing `model_or_sprite` role compares the two categories globally, selects
IDST as model and IDSP as sprite, and keeps an exact best-rank tie ambiguous.
A valid split Studio main source returns the typed
`ExternalDependencyRequired` error rather than a partial asset or a parsed
error string. The visual operation applies its Studio limits through
`IGoldSrcStudioModelImporterWithLimits` on that exact selected caller-owned
instance. Only that result can start the exact-root bundle operation; sprites
and self-contained Studio models finish from retained approved bytes without a
filesystem re-resolution. The GoldSrc visual operation then opens only its
derived exact-root companions and invokes the pure bundle import. See
[Studio dependencies](GOLDSRC_STUDIO_DEPENDENCIES.md) and
[SPR v2](GOLDSRC_SPR_V2.md).

Entity playback collects only unique model references present in its selected
snapshot pair and invokes this same `model_or_sprite` route on demand. It never
imports the whole precache table, guesses by extension, or reopens assets from
the renderer. Published library revisions deduplicate by exact approved-source
and Studio-bundle identity. See [entity asset binding](ENTITY_ASSET_BINDING.md).
