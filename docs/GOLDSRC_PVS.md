# GoldSrc PVS decoding

M4.4 decodes BSP v30 potentially-visible-set rows on the CPU before a scene is
given to a renderer. OpenGL never sees compressed visibility bytes.

## Row and bit numbering

For `visleafs` model-0 visible leaves, the row width is:

```text
(visleafs + 7) / 8
```

Bit `n` addresses source leaf `n + 1`. Within a byte, bit zero is the least
significant bit. Leaf zero has no bit. Padding bits in the final byte are
masked and never become leaf references.

## GoldSrc RLE grammar

A nonzero source byte is copied literally. A zero source byte must be followed
by a nonzero count byte and expands to that many zero bytes. Decoding stops
only when the exact row width has been produced. Truncated literals, a missing
or zero run count, a run which crosses the row boundary, an offset outside the
visibility lump, or configured work/memory overflow fails transactionally.
No partial row is published.

A leaf `visofs` of `-1` selects an explicit all-visible row, masked to the
model-0 visible-leaf count. Other negative offsets are invalid. Repeated
non-negative source offsets are decoded once and share a deduplicated owning
row. The all-visible row also participates in deterministic row identity.

## Bounds and fallback

Limits cover row bytes, unique rows, total decompressed bytes, and decode work.
Malformed visibility is a spatial-package build error. A valid package may
still have a camera in leaf zero, a solid leaf, or a leaf without a usable
row; `WorldVisibilityResolver` applies its explicit `fail_closed`,
`frustum_only`, or `all_surfaces` fallback policy and records the reason.

PVS decoding is independent of rendering, filesystem paths, entity behavior,
and network state. Logs and trace events expose only bounded counts and typed
status, never compressed or decompressed row bytes.
