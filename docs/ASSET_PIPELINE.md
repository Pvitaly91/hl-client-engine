# Asset pipeline

## Purpose

M0.1 establishes the contracts used to move licensed, user-supplied data from
a virtual filesystem into renderer-independent CPU assets. It deliberately
does not implement GoldSrc BSP, MDL, SPR, WAD, or WAV parsing.

The required flow is:

```text
Virtual filesystem
        |
        v
AssetSource (virtual path + owned bytes)
        |
        v
Typed format importer
        |
        v
Neutral CPU asset
        |
        v
AssetManager
        |
        v
Renderer-specific resource
```

A disk format and a neutral asset are different layers. A disk format contains
the original file's signatures, versions, offsets, packed records, and other
serialization details. A neutral asset contains only engine-owned data needed
by consumers. For example, a future MDL importer may understand a GoldSrc
studio header internally, but the resulting `ModelAsset` must not expose that
header, an SDK pointer, or a pointer into the input buffer.

## Virtual filesystem and `AssetSource`

Importers do not open Windows paths. `AssetManager` asks `IFileSystem` for a
virtual path, receives owned bytes, and moves them into an `AssetSource`.
Physical-root validation and file I/O therefore stay behind the filesystem
boundary:

```text
IFileSystem::read_file("models/example.asset")
        -> AssetSource{"models/example.asset", owned bytes}
        -> importer
```

`AssetSource` owns both its virtual path and byte buffer. Any
`std::string_view` or `std::span<const std::byte>` returned by it is valid only
for the lifetime of that source; an imported asset must copy or transform data
that it retains. Importers must never retain a span into a temporary source.

The rooted disk provider treats virtual paths as untrusted input. Absolute
paths, parent traversal, and paths that resolve outside its configured root are
rejected. Tests use an in-memory provider, so no installed game, disk fixture,
or public network is required.

## Neutral CPU assets

The asset API defines distinct categories:

- `ModelAsset` for movable model geometry;
- `WorldAsset` for map/world data;
- `SpriteAsset` for sprite frames;
- `ImageAsset` for decoded images;
- `AudioAsset` for decoded audio samples.

These are deliberately small M0.1 contracts. They contain project-owned source
names, basic metadata, and category-appropriate containers. They do not contain
OpenGL names, SDL objects, Windows or Winsock handles, SDK pointers, packet
structures, or raw pointers into serialized input.

A BSP is not an ordinary `ModelAsset`. A world/map has world-specific spatial,
visibility, collision, lightmap, and surface relationships and therefore
imports into `WorldAsset`. Model instances can later reference separate model
resources without forcing map semantics into the model contract.

## Probe and selection

Each typed importer has a stable string ID, a non-throwing probe, and an import
operation with an explicit result. A probe can inspect:

- the virtual path and its extension as a hint;
- signature or magic bytes;
- a serialized version;
- enough bounded bytes for structural sanity checks.

An extension alone never identifies a format. Two unrelated formats may share
one extension, and a correctly signed file remains identifiable when its
extension is wrong.

Each registry selects an importer deterministically:

1. discard candidates whose probe reports no match;
2. keep candidates with the highest confidence;
3. among those, keep the highest explicit registration priority;
4. if more than one candidate remains, return `AmbiguousFormat`;
5. if no candidate matched, return `UnsupportedFormat`.

Registration order is not a tie-breaker. Duplicate importer IDs are rejected.
When an importer fails, its ID, the source virtual path, and importer-provided
context remain in the returned error.

`ModelImporterRegistry`, `WorldImporterRegistry`, `SpriteImporterRegistry`,
`ImageImporterRegistry`, and `AudioImporterRegistry` are typed views of the
same registry policy. `AssetImporterRegistries` owns all five registries. It is
a normal object owned by the application composition root, not a singleton;
there is no static self-registration.

M3.2.3 exposes the same selection as a pure `probe()` result. It calls each
registered probe exactly once, never imports, and returns bounded sorted IDs,
confidence, priority, and `no_match`/`selected`/`ambiguous` state without an
importer pointer. `AssetImporterDispatcher` uses that cached selection so the
unique winner imports once without a second probe. For an evidence-authorized
model-or-sprite source, model and sprite candidates are ranked globally; a tie
at the best confidence/priority is ambiguous, while lower-ranked ambiguity
does not block a unique stronger candidate.

The GoldSrc approved-source path is separate from `AssetManager`:

```text
verified locator handle -> owning AssetSource -> evidence-derived role
    -> AssetImporterDispatcher -> imported asset or typed importer boundary
```

World, audio, and model-or-sprite roles permit only their corresponding
registries. Decals remain metadata-only; generic and event-script resources are
unsupported. An extension never creates or changes one of these roles.

## `AssetManager`

`AssetManager` coordinates, but does not parse or upload:

1. accept a virtual path and asset category;
2. read the path through `IFileSystem`;
3. create an owning `AssetSource`;
4. ask the matching typed registry to probe and import it;
5. return the neutral CPU asset or a contextual error.

M0.1 intentionally has no cache, background loader, streaming, hot reload,
dependency graph, GPU upload, eviction policy, or thread pool. Those features
can be added above these contracts without binding importers to OpenGL.

Renderer backends are responsible for translating neutral assets into their
own resources. Such GPU resources must remain renderer-owned opaque handles;
the asset API never stores `GLuint` values and never requires an active window
or graphics context.

## Explicit registration

M0.1 has no production format implementation, so the application creates an
empty registry set. Synthetic importers live only in tests. A future built-in
format module exposes an explicit registration function called by
`apps/hlclient`; it does not use a global constructor:

```cpp
hlclient::assets::AssetImporterRegistries registries;

RegisterGoldSrcMdlImporter(registries.models);
RegisterGoldSrcBspImporter(registries.worlds);
```

To add a model format:

1. create a focused static CMake target for that format;
2. implement `IModelImporter`;
3. validate its signature, supported version, and bounded structure;
4. translate serialized records into a `ModelAsset`;
5. expose an explicit registration function and call it in the composition
   root;
6. add synthetic signature, truncation, malformed-input, and conversion tests;
7. leave renderer, networking, `ClientWorldState`, and other format modules
   unchanged.

Adding a world/map format follows the same steps with `IWorldImporter` and
`WorldAsset`. A future module layout may therefore look like:

```text
modules/formats/goldsrc_mdl
    -> hlclient_asset_api
    -> hlclient_filesystem API
    -> hlclient_core
```

The real `hlclient_format_goldsrc_mdl`, `hlclient_format_goldsrc_bsp`,
`hlclient_format_goldsrc_spr`, and `hlclient_format_goldsrc_wad` targets will be
created only when their parsers contain real implementation code.

## Static modules now, plugins later

The current architecture is a modular monolith: one `hlclient.exe`, separate
CMake targets, explicit interfaces and registration, and static linking. The
C++ API is still evolving, so turning every module into a DLL would create an
unstable compiler- and runtime-specific ABI without delivering useful format
support.

After the API stabilizes, optional runtime plugins may use a versioned C ABI.
That boundary must not pass `std::string`, `std::vector`, STL iterators, C++
exceptions, C++ virtual objects across toolsets, or memory whose allocator and
owner are not explicitly agreed. M0.1 defines no DLL loader and no C ABI.
