# GoldSrc SPR v2 import profile

M4.5.2 implements a clean-room, CPU-only importer for little-endian,
palette-bearing GoldSrc `IDSP` version 2 sprites. It does not support Quake v1,
other versions, compressed frames, big-endian sources, or external palettes.
It preserves source metadata and pixels without adding billboard, blending,
animation playback, entity binding, OpenGL, or GPU behavior.

The header is exactly 40 bytes:

| Offset | Field |
| ---: | --- |
| 0 | four-byte `IDSP` |
| 4 | signed version `2` |
| 8 | signed orientation |
| 12 | signed texture format |
| 16 | finite bounding radius |
| 20 | signed maximum width |
| 24 | signed maximum height |
| 28 | signed top-level entry count |
| 32 | finite beam length |
| 36 | signed synchronization type |

Orientation values 0 through 4 are retained as `vp_parallel_upright`,
`facing_upright`, `vp_parallel`, `oriented`, and
`vp_parallel_oriented`. Texture formats 0 through 3 are `normal`, `additive`,
`index_alpha`, and `alpha_test`. Synchronization is `sync` or `random`.
Unknown enum values are a typed unsupported profile, not a rendering guess.

Immediately after the header is an unsigned 16-bit palette count. The
supported profile requires exactly 256, followed by 768 RGB bytes. No gamma or
premultiplication is applied. The header count then repeats top-level entries,
each beginning with a signed frame type: zero for a single frame or one for a
group. Any other type is malformed.

A single frame is signed origin X/Y, positive signed width/height, then exactly
`width * height` indexed bytes. A group is a positive signed child-frame count,
that many finite, positive, strictly increasing cumulative float intervals,
then that many single-frame records without nested type tags. Per-frame
durations are derived from adjacent cumulative endpoints. Top-level entry
count, group count, and flattened image count remain distinct.

Header maxima must cover every frame. All counts, dimensions, areas, cumulative
pixel totals, RGBA totals, intervals, frame headers, palette ranges, source
size, and trailing EOF are checked before publication. Indexed pixels, the
palette, origins, source top-level/group ordinals, orientation, format, sync
type, and statistics are owning; no span into the source survives import.
The RGBA allocation limit charges both the authoritative source-metadata
payload and the additive legacy `SpriteFrame` compatibility copy before either
buffer is allocated; the reported derived-payload statistic counts the image
bytes once.

Derived RGBA is evidence-labelled. `normal` uses palette RGB and alpha 255.
`alpha_test` gives index 255 alpha 0 and every other index alpha 255 while
preserving RGB. `additive` may expose an opaque CPU preview but retains the
format as the authority and makes no blend claim. The complete `index_alpha`
indexed source is imported, while its runtime RGBA formula remains
evidence-pending rather than guessed.

The production importer ID is `goldsrc-sprite-v2` at named priority 300. Probe
uses `IDSP`, version 2, the exact header, and bounded structural checks;
`.spr` adds only a one-point hint. An extensionless valid source still matches,
and random `.spr` bytes do not. In the existing `model_or_sprite` role, IDSP
selects the sprite category while IDST selects the model category; an exact
cross-category rank tie remains ambiguous.
