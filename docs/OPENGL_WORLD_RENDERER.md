# OpenGL world renderer

The M4.3 OpenGL backend is the first graphical consumer of the neutral
`WorldRenderPackage`. It targets OpenGL 3.3 Core through the committed GLAD2
loader and receives only `RenderScene`; it performs no BSP/WAD parsing,
filesystem lookup, asset dispatch, or network work.

## Resource ownership and cache

The backend owns shader/program, vertex-array, buffer, base-texture, and
lightmap-array objects through internal RAII state. Public asset and scene APIs
never expose GL names. A package's stable resource identity/revision controls
the renderer-local cache:

- the same identity and revision reuse the existing GPU upload;
- a new identity/revision destroys the old world resources and uploads once;
- resources are not uploaded every frame and there is no global cache;
- a partial upload is transactional and never becomes the active world.

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
GoldSrc source coordinates remain Z-up, the model matrix is identity, base UVs
use normalized raw S/T with repeat, and lightmap UVs address atlas texel centers
without a CPU vertical flip.

Every frame sets the viewport, clear color, and depth clear; enables depth
testing with `GL_LEQUAL` and depth writes; disables blending; and selects no
culling or back-face culling from `RenderScene`. The diagnostic preview uses
no culling so an outside-bounds camera can see the closed shell. Geometry
winding is never reversed to accommodate that camera.

For each deterministic package batch, the renderer binds its base texture and
lightmap page/fallback, sets the masked and lightmap uniforms, and calls
`glDrawElements` for the exact checked byte offset
`first_index * sizeof(uint32_t)`. Masked draws keep blending disabled and depth
writes enabled. M4.3 has no translucent pass.

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

M4.3 does not perform PVS or frustum traversal, render brush entities, animate
light styles, implement dynamic lights, implement water/sky/animated-texture
effects, or derive a gameplay camera. Those remain later milestones.
