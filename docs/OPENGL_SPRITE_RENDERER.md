# OpenGL Sprite entity renderer

Sprite render assets retain immutable frame textures, exact source origins,
entry/group metadata, bounds, orientation, and texture format. Synchronized
groups select against cumulative intervals and wrap at the final interval.
Random synchronization remains evidence-pending unless an explicit deterministic
override is supplied.

The supported orientations are computed as renderer-neutral billboard bases;
`view_parallel`, `view_parallel_upright`, and `oriented` are required and
degenerate camera vectors fail with a typed draw error. `facing_upright` and
`view_parallel_oriented` remain evidence-pending, are published as typed
unsupported metadata, and never reach the draw list. Quad corners use the
source origin, width, and height without automatic centering.

All sprites share static quad geometry. Each renderable frame has one cached
RGBA8, clamp-to-edge, linearly filtered, non-mipmapped texture. `normal` is
opaque and `alpha_test` discards transparent pixels, both with depth test/write
and blending disabled. `additive` and `index_alpha` remain typed non-renderable;
neither is silently drawn as opaque.
