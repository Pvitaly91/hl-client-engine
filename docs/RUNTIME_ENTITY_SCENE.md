# Runtime entity scene

`EntitySceneRenderPackage` owns immutable Studio/Sprite GPU-source assets and an
entity visual asset-library revision. `EntityRenderFrame` owns only ordered
per-frame instances, pose/frame selections, visibility outcomes, interpolation
metadata, and a frame signature. It contains no filesystem path, OpenGL name,
snapshot wire value, or network owner.

The renderer-facing `hlclient::entity_scene_render` target implements only this
immutable package/frame contract, bounded frame validation, visibility, and draw
ordering. Construction that dereferences `EntityVisualAssetLibraryState` and the
pipeline stage that proves exact library/world ownership live in the separate
client-side `hlclient::entity_scene_package_build` target. OpenGL and null
renderers link only the renderer-facing target, so their dependency closure does
not include entity-visual projection, the GoldSrc snapshot codec, or sign-on
parsing. Composition roots that call `EntitySceneRenderPackageBuilder::build`
must link `hlclient::entity_scene_package_build` explicitly.

`hlclient::entity_interpolation_stage` remains a client-side orchestration
target. After pose and Sprite composition, its terminal result retains the
exact `InterpolatedEntityFrame` and resulting `EntityRenderFrame` together and
validates their timeline identities, candidate set, transforms, and aggregate
statistics. The stage depends on the renderer-neutral scene contract only;
OpenGL/null renderers do not depend on the stage, snapshots, or filesystem.

`ClientWorldState` retains the package and frame through neutral shared ownership
with independent scene and frame revisions. `build_render_scene()` only maps
those references into `RenderDynamicEntities`; it performs no decoding, import,
pose evaluation, or I/O. World resources and entity resources therefore have
separate cache identities, and a new entity frame cannot trigger static geometry
or texture upload.

Entity bounds are culled conservatively. With a spatial package, an entity is
PVS-visible when any touched non-solid leaf is visible; otherwise the route uses
frustum-only fallback. This synthetic policy is not a claim about stock leaf
linking. Draw order is world, Studio opaque, Studio masked, Sprite normal, then
Sprite alpha-test. Unsupported entities remain in metadata.

The bounded integration route finishes snapshot history, destroys networking
and authentication exactly once, then performs local asset loading and snapshot
playback without further transmission. It is deterministic synthetic playback,
not a live connected multiplayer client. Dynamic brush snapshots, gameplay
camera/input/usercmd/prediction, server/model event execution, and downloads are
outside M4.5.3.
