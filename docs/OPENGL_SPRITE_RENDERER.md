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

## E29 screen-space sprites

The independent HUD pass now consumes neutral image ID/revision/frame/subrect,
destination, tint, blend, clip and ordering commands. Application-approved
SpriteRenderAsset owners reuse the canonical importer/builder; this is not a
world billboard or a second SPR decoder. Palette/masked coverage is retained,
additive black remains neutral, and nearest/clamp/no-mip sampling preserves
adjacent sheet tiles. CPU commands, visible bindings and GPU cache are bounded;
the pass restores the exact prior GL state. The older world-sprite stage notes
above retain their historical scope. Current E29 behavior/evidence is in
[the HUD contract](ORIGINAL_SPRITE_HUD_INVENTORY_CROSSHAIRS_E29.md).
