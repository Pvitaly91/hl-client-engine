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

## 2026-10-15 normal-path static model lighting corrective

The historical constant-light description above is superseded for normal
materialized models. E10 introduced an optional finite RGB multiplier from the
retained world/lightmap package, but populated it only inside `player_pose`.
Ordinary pickups, projectiles and camera-local weapons consequently used the
diagnostic 0.85 multiplier even over a dark floor. User screenshot
`HL Client Engine - Player 15.10.2026 21_38_51.png` is attribution=user_report,
not a stock-comparison capture.

`RuntimeReplayLocalAssets::studio_light` now shares that existing downward,
bilinear selected-style probe for all packet Studio models, the bounded
shell/fixed-model pool and viewmodels. The application supplies the current
world eye solely for viewmodel lighting; its mesh, pose and muzzle sockets
remain camera-local. The existing renderer uniform and material selection
perform the RGB multiplication; no global gamma/brightness change, texture
rewrite, second renderer or game-specific shader branch was introduced.
Player bodies, merged p-models and corpse policy retain their existing path.

The cache reprobes after more than eight units along any axis. Entries are
bounded by committed packet entities plus one viewmodel and 64 transient slots;
removed packet slots are pruned, and generation/continuity resets clear it.
No render-time resource opening occurs. Absent/invalid sampling retains the
explicit diagnostic fallback, whereas a valid black sample remains black.
Fullbright metadata still bypasses static darkening. Existing muzzle/effect
lights remain additive and are not dimmed with the ambient term.

Read-only inspection of the actual `w_gaussammo.mdl` and its texture companion
`w_gaussammot.mdl` found `boxside.bmp`, `boxtop.bmp`, `hinge.bmp`, all flags 0:
the reported pickup is not a fullbright material. Assets were not modified.
Pinned official SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`,
`cl_dll/StudioModelRenderer.cpp` (ordinary model lighting at 1238-1246 and player
lighting at 1780-1787), requests engine lighting for both categories. The
engine implementation of those callbacks is not part of this SDK. This fix
restores environment-dependent brightness, not a claim of stock-exact ambient/
directional lighting, gamma, animated-style accumulation or dynamic shadows.

Evidence is retained under
`manual-artifacts/task-records/studio-lighting-sensitivity-20261015`.
Mandatory project-owned tests cover black/dim/bright floor samples, local pose
invariance, viewmodel movement, transient models, invalid/no-light fallback and
reset, actual OpenGL RGB values, fullbright preservation and muzzle response.
New Release manual comparison remains pending until the user runs it.

Final verification (VS2022 v143 Win32): Debug and Release focused checks each
passed 13 cases / 2,525 assertions, including the actual OpenGL path. For source
RGB (200,160,100) above a floor sample of 48/255, the rendered ordinary material
was (38,30,19), explicit fullbright remained (200,160,100), and the ordinary
material with a muzzle light became (131,105,66). The full Release offline suite
after the last production edit registered 3,712 tests: 3,646 passed, 66 explicit
optional skips, zero failures. Core-only Release passed 44/44 and the architecture
guard; launcher source-only CheckOnly/no-stock checks and `git diff --check`
passed. Debug's broader regression run passed 110 cases with 13 explicit external
asset skips. ASan runtime was not revalidated and remains previously unconfirmed.

Normal Release `hlclient.exe` SHA-256:
`C2857AA240D8327777776B5BE385331EFC9E8ADFC1894F23ED46535CC2970BC8`.
The launcher and user settings were unchanged. Preservation audit confirms all
1,420 pre-edit source files retained in the task snapshot, only the ten intended
source/document files changed, and no source removed. `verification.json` in the
evidence directory records exact results and limits. New live sessions, stock
clients/HLDS/managed captures and staging/commit/push: zero; manual=not_run.
