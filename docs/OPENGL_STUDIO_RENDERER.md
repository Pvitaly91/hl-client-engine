# OpenGL Studio entity renderer

The renderer consumes protocol-neutral `StudioModelRenderAsset` geometry. Each
vertex retains bone-local position/normal, raw texture S/T, and separate integer
bone indices. UVs are divided by the texture dimensions selected through the
current skin family, so geometry is not normalized against family zero.

One immutable VAO/VBO/EBO and one RGBA8 texture per source texture are cached by
entity-scene asset identity/revision. Entity-frame changes update transforms and
pose data only. OpenGL 3.3 capability checks cover uniform-block size, vertex
uniform blocks, texture units, and vertex attributes. Up to 128 std140 `mat4`
bone matrices use one bounded reusable UBO; insufficient capacity is a typed
capability failure.

The vertex shader skins each position with its position-bone matrix. It skins
each source normal with the rotation part of its independently indexed normal
bone and then applies the entity rotation. Entity-transform basis columns are
normalized before this multiplication, so the supported positive uniform scale
does not change normal length. The shader publishes the normalized world normal
as the §56 skinning contract, but the M4.5.3 fragment shader intentionally does
not use it for visibility, survival, color, or lighting.

Opaque and masked materials use depth test/write with blending disabled; masked
pixels are discarded. Chrome, additive, alpha, and unknown bits are not drawn
and remain typed unsupported. Lighting is diagnostic base color multiplied by a
constant neutral factor. It is not GoldSrc world, chrome, ambient/directional,
dynamic, or model-vector lighting.
