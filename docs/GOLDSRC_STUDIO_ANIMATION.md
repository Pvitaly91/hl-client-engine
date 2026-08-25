# GoldSrc Studio animation profile

M4.5.2 retains and validates GoldSrc Studio v10 animation source semantics. It
does not add clocks, entity sequence state, fractional-frame interpolation,
blend selection, gait, controller application, event firing, root motion, or
pose matrices. Those runtime responsibilities belong to M4.5.3.

Each sequence retains its bounded label, FPS, raw flags, activity and weight,
frame and blend counts, blend types/ranges, motion metadata, bounds, inert
events and pivots, sequence-group index, and ordered animation blends. Each
blend has one 12-byte animation-offset record per bone. Its six unsigned
16-bit offsets are relative to the beginning of that record and select the
translation X/Y/Z or rotation X/Y/Z channel in exactly one source bundle
member. A zero offset means that the channel uses the bone default.

FPS must normally be finite and positive. The evidenced Valve v10 static
profile also permits numeric zero FPS only for exactly one frame with zero
motion type, zero linear movement, and zero automatic-movement offsets.
Negative FPS and zero FPS on multi-frame or motion-bearing sequences are
rejected before animation retention.

A nonzero channel is an ordered series of runs:

```text
u8 valid
u8 total
valid * i16le quantized values
```

`total` must be nonzero and `1 <= valid <= total`. Each run, value array,
relative offset, accumulated frame coverage, and source ownership is bounded.
Advancement is exactly one two-byte run header plus `valid` values, so malformed
input cannot form a zero-progress loop. Coverage must satisfy the sequence's
integer-frame profile without reading beyond the selected main or `IDSQ`
source. The importer stores the compressed runs and never eagerly expands a
sequence into a full pose table.

The supported Valve compiler profile permits at most 2,048 retained animation
blends in aggregate. The parser also caps the derived blend-by-bone track count
at 262,144 and checks both totals before reserving or retaining sequence output;
zero-offset channels therefore cannot bypass the allocation profile merely by
consuming no compressed run/value bytes. Nonzero channel starts have a further
aggregate ceiling of `min(6 * maximum_animation_tracks,
maximum_animation_runs)`, because every accepted stream must consume at least
one run. Physical ranges and stream starts live in ordered indices, so alias,
overlap, and next-boundary checks remain logarithmic rather than becoming
quadratic for a model with many tracks.

The validation sampler locates the run containing an integer frame. An offset
inside the first `valid` frames selects that run value; the remaining
`total-valid` frames reuse the last valid value. The next run begins exactly at
the next coverage boundary. A separate pure helper may calculate
`bone_default + quantized_value * bone_scale`; the retained source value itself
remains signed and exact.

Sequence-group zero reads animation offsets from the validated main `IDST`.
For that group, the physical animation-record offset is checked
`mstudioseqgroup_t::data + mstudioseqdesc_t::animindex`, matching the pinned
Valve v10 consumer; zero remains the common compiler output but is not assumed.
The resolved record and every channel stream must remain in the main source and
must not overlap any fixed or previously retained range. Groups 1 through 15
read `animindex` from the matching, separately length-validated `IDSQ` source
without applying the main-file group-zero base. Header names are never used for
source selection. Events retain their frame, event number, type, and bounded
option bytes as inert metadata; they never invoke audio, a console, gameplay,
or a filesystem operation.
