# OpenGL world renderer

The M4.3 OpenGL backend is the first graphical consumer of the neutral
`WorldRenderPackage`. M4.4 extends the same backend with renderer-neutral
visible draw commands and an optional aggregate static brush package. It
targets OpenGL 3.3 Core through the committed GLAD2 loader and receives only
`RenderScene`; it performs no BSP/WAD/PVS/entity parsing, filesystem lookup,
asset dispatch, or network work.

## Resource ownership and cache

The backend owns shader/program, vertex-array, buffer, base-texture, and
lightmap-array objects through internal RAII state. Public asset and scene APIs
never expose GL names. A package's stable resource identity/revision controls
the renderer-local cache:

- the same identity and revision reuse the existing GPU upload;
- a new identity/revision destroys the old world resources and uploads once;
- resources are not uploaded every frame and there is no global cache;
- a partial upload is transactional and never becomes the active world.

M4.4 gives the composed `WorldSceneRenderPackage` a separate stable resource
identity/revision. That identity controls world/brush GPU resources. A changed
`WorldVisibilitySet`/`WorldVisibleDrawList` revision changes command selection
and statistics only; moving the camera or selecting a different PVS never
causes a geometry or texture re-upload. One aggregate brush VAO/VBO/EBO and
its texture/lightmap resources are shared by all supported instances.

The renderer must be shut down, including failed partial resources, before its
SDL OpenGL context is destroyed.

## Geometry and texture upload

One immutable VAO, one `GL_STATIC_DRAW` VBO, and one `GL_STATIC_DRAW` EBO hold
the package geometry. Attributes are position, normal, base UV, and lightmap
UV; indices are `GL_UNSIGNED_INT`. Stride, offsets, sizes, and conversion to GL
integer types are checked before calls.

Each unique `WorldTextureAsset` becomes one `GL_TEXTURE_2D`. The renderer
uploads exactly the four provided RGBA8 mip levels with no generated mipmaps:

```text
internal/source/type = GL_RGBA8 / GL_RGBA / GL_UNSIGNED_BYTE
base/max level       = 0 / 3
wrap S/T             = GL_REPEAT
mag/min filter       = GL_LINEAR / GL_LINEAR_MIPMAP_LINEAR
```

Varying base-texture dimensions are intentional; base textures are not forced
into an array. Every level's dimensions and byte count are validated first.

Each lightmap atlas page becomes one `GL_TEXTURE_2D_ARRAY` with exactly four
RGBA8 layers and one mip level. S/T use `GL_CLAMP_TO_EDGE`, and both filters
are `GL_LINEAR`. A separate 1x1 white, four-layer array is created only when
the validated package contains an `unlit_white` draw batch; it is never
uploaded for an all-atlas world. Lightmap mipmaps are not generated.

## Shaders

The built-in vertex shader accepts:

```text
location 0: vec3 position
location 1: vec3 normal
location 2: vec2 base_uv
location 3: vec2 lightmap_uv
uniform:    mat4 u_model_view_projection
```

It passes both UV sets to the fragment shader. The fragment shader samples a
base `sampler2D` and a lightmap `sampler2DArray`, with uniforms selecting
lightmap enablement, masked alpha, and the lightmap layer. Masked fragments
with alpha below 0.5 are discarded. A lightmapped draw samples layer 0; an
unlit draw uses a white factor. Output is:

```text
rgb   = base.rgb * lightmap.rgb
alpha = base.a
```

There is no gamma correction, overbright multiplication, fog, exposure,
dynamic light, or multi-style blending. Shader sources are compiled in, not
loaded from a server/game path or arbitrary CLI option. Compile and link logs
are bounded and do not print the full source.

## Matrices, coordinates, and draw state

Format-neutral camera math supplies a right-handed look-at matrix and OpenGL
clip-space perspective projection. The convention and matrix upload transpose
choice are explicit in code and covered by canonical projection tests.
GoldSrc source coordinates remain Z-up. World commands use the identity model;
brush commands use their validated entity-origin-relative model matrix and the
shader receives `projection * view * model`. Base UVs use normalized raw S/T
with repeat, and lightmap UVs address atlas texel centers without a CPU
vertical flip.

Every frame sets the viewport, clear color, and depth clear; enables depth
testing with `GL_LEQUAL` and depth writes; disables blending; and selects no
culling or back-face culling from `RenderScene`. The viewer exposes those
states as `--cull none` and `--cull back`. The diagnostic preview defaults to
no culling so an outside-bounds camera can see the closed shell. Geometry
winding is never reversed to accommodate that camera.

Under M4.4.1, BSP v30 uses the explicit
`valve_qbsp_clockwise_wire_to_counter_clockwise_render` compatibility profile.
The shared CPU world/brush builder validates Valve QBSP's clockwise raw wire
against the side-adjusted normal, preserves a deterministic anchor, and emits
counter-clockwise renderer triangles whose winding agrees with the emitted
normal. Back-face culling consumes those canonical indices without any global
normal, shader, index, or cull-state inversion. The detailed evidence and
tolerance policy are in
[GoldSrc BSP geometry compatibility](GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md).

For each deterministic package batch, the renderer binds its base texture and
lightmap page/fallback, sets the masked and lightmap uniforms, and calls
`glDrawElements` for the exact checked byte offset
`first_index * sizeof(uint32_t)`. Masked draws keep blending disabled and depth
writes enabled. M4.3 has no translucent pass.

When an M4.4 visible draw list is present, the backend instead issues its exact
checked per-surface ranges in stable opaque-world, masked-world, opaque-brush,
masked-brush order. The historical M4.3 full-batch path remains the fallback
when no draw list is supplied. Unsupported or nonzero brush rendermodes never
enter the draw list; blending remains disabled.

## Bounded cull-mode proof

First run `hlclient_bsp_compat_check` or its read-only stock verifier through
`spatial-scene`; that CPU gate requires neither SDL nor OpenGL. On a host whose
actual current context is OpenGL 3.3 Core or newer, validate both cull states
with two bounded frames each:

```powershell
.\scripts\verify_local_world_render.ps1 `
  -ViewerPath .\build\bin\Debug\hlclient_world_viewer.exe `
  -Basedir "<Half-Life-root>" -Game valve -Map maps/<name>.bsp `
  -Frames 2 -Visibility pvs-frustum -BrushSubmodels static `
  -Camera spawn -CullModes @('none', 'back')
```

The wrapper launches a separate bounded viewer process per mode and validates
the exact `cull-mode` report. Each process must render two frames, retain one
world/scene upload, produce non-clear pixels and nonzero draw/triangle counts,
and report `gl-error=none`. BSP, relevant WAD, and search-root inventories must
remain unchanged across both processes. `-CullModes` defaults to the M4.4.1
acceptance set `none,back`. One value may be supplied for a focused rerun while
retaining the same bounded and drift checks. Hosts without the required
graphics capability may skip this optional proof, but the CPU compatibility
verifier remains mandatory.

## Failure and diagnostics

Invalid packages/cameras/ranges, shader or program failures, buffer/texture/
lightmap upload failures, GL operation failures, and retention failures are
bounded typed renderer errors. Critical creation and upload operations are
checked without an unbounded `glGetError` loop. Failed replacement leaves no
active partial world.

Metadata-only statistics can report the package revision, upload count,
rendered frames, draw calls, triangles, and base/lightmap binds without
copying the package or exposing texture pixels. Resize updates the viewport.
The null renderer consumes the same expanded scene but uploads nothing.

Actual-context frame tests inspect the bounded `GL_VERSION` result after a
context is current. They execute on real OpenGL 3.3-or-newer contexts and may
skip only when the context is unavailable or actually older; the requested SDL
version alone is not treated as capability evidence. Production still rejects
an actual context below OpenGL 3.3 Core. This gate is not an unconditional
headless-host skip.

PVS decompression, BSP traversal, frustum resolution, and entity interpretation
remain CPU responsibilities upstream of the renderer. M4.4 renders only
supported static initial opaque brush instances. It does not animate light
styles, implement runtime doors/platforms/rotators, blending, dynamic lights,
water/sky/animated-texture effects, gameplay input, or a gameplay camera. See
[world visibility](WORLD_VISIBILITY.md) and
[brush-submodel rendering](BRUSH_SUBMODEL_RENDERING.md).

M4.5.3 composes protocol-neutral Studio and Sprite instances after the existing
world pass. World and entity scene resources use independent identity/revision
keys: changing an entity frame updates only transforms and the bounded pose UBO,
never world or entity static geometry. Entity material support and diagnostic
lighting are documented separately in
[runtime entity scene](RUNTIME_ENTITY_SCENE.md).

M4.6.1 camera updates alter only the validated view/projection matrix and
upstream CPU visibility selection. A moved or mouse-look camera must not change
world, brush, Studio, or Sprite resource revisions or upload counters. Actual
OpenGL camera tests use project `InputEvent` scripts rather than OS input and
retain the existing real-context 3.3 Core capability gate.
