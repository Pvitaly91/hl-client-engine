# Entity asset binding

The synthetic model-slot resolver performs one exact lookup in the immutable
precache model-slot table. The stock resolver is fail-closed while mapping
evidence is pending. Format selection is delegated to the registered
`model_or_sprite` dispatcher; filename extensions are never used as a format
guess.

Bindings report typed resolved, missing, not-ready, unsupported, dependency,
ambiguity, import, and limit outcomes. No placeholder is silently substituted.
Published records own either a `ModelAsset` or `SpriteAsset`, but no native path,
locator, stream, renderer handle, or mutable importer operation.

The library collects unique references from one snapshot pair, imports only the
ready references actually used, and publishes an immutable revision. A later
reference produces a new revision while shared ownership preserves existing
assets. Reuse requires caller-supplied current `EntityVisualAssetReuseEvidence`:
the exact approved-source key (manifest/environment, root, virtual identity,
stable file identity, and size), importer category/ID/profile, total bytes, and
the ordered main/texture-companion/sequence-group content fingerprints must all
match the retained record. Missing or changed evidence schedules a fresh import;
this catches same-size in-place main-file drift and companion-only drift without
exposing native paths. Virtual names alone are insufficient. Count, source-byte,
RGBA, geometry, pending-import, per-update, and event budgets have default and
hard caps.
