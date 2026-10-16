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
as the §56 skinning contract. The historical M4.5.3 shader did not use it for
lighting; the normal-path chrome/directional corrective below now does.

Opaque and masked materials use depth test/write with blending disabled; masked
pixels are discarded. Chrome is supported in the shared Studio pass (initially
added in M4.7.3A). Additive, alpha, and unknown material combinations remain
typed unsupported. No-sample models retain the diagnostic constant-light
fallback; the normal-path correctives below supersede the historical baseline.

## 2026-10-16 bone-relative chrome and directional Studio lighting

The .357 dullness report is `attribution=user_report`, not a matched stock
comparison. Approved `models/v_357.mdl` has three CHROME|FLATSHADE textures:
`bluesteelchrome.bmp`, `bulletCHROME.bmp`, `RubbergloveCHROME.bmp`; none is
FULLBRIGHT. Model SHA-256 is
`12791F64091AA572AE35AAD40EE74B5BB3EE30F59426E6FFA045D9AD1D731175`.
The world/attached .357 models use ordinary `wrld_python.bmp`. No assets,
palettes, model flags, launcher settings or world gamma were changed.

The previous chrome shader ignored the eye-to-normal-bone vector and used
different signed axes. The shared shader now constructs a camera-relative
basis at the normal bone's posed origin, independently of the position bone.
World-normal dot world-basis is equivalent to rotating the basis into bone
space. Eye/bone coincidence and parallel side-on axes have finite fallbacks.
Ordinary UVs are unchanged. Chrome remains the original texture convention,
not PBR or a reflection of the room; float UVs omit integer quantization.

Sampled Studio models receive hemispherical normal-dependent lighting:
ambient32, shade192, lambert1.5, normalized to the flat-material reference
`32 + .8*192 = 185.6`. This independently authored compatible mapping keeps
FLATSHADE exactly at sampled RGB. Facing/side/away normal factors are
224/185.6, 160/185.6, 32/185.6. Static RGB is clamped to[0,1] before unchanged
additive muzzle/Egon lights. Black remains black; FULLBRIGHT bypasses static
lighting even without a sample; ordinary no-sample fallback remains exactly.85.
There is no per-weapon exposure boost or new gamma setting.

Both first-person branches transform scene-down into the existing camera-local
+X-forward/+Y-left/+Z-up coordinates via `camera_local_direction`. Translation
and FOV do not rotate light; pitch/roll do. Meshes, bones, muzzle sockets and
world-eye probes keep their established spaces. The same shader serves remote
players, attached p-models, pickups and transient models, without game names,
resource opening or an SDK runtime dependency.

Reference: official pinned SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`,
`utils/mdlviewer/studio_render.cpp`, `Lighting`402 and `Chrome`441. This public
viewer reference does not expose the live engine lighting callbacks. The
scene-down mapping is local-compatible, not stock-exact ambient/shade, gamma,
animated-style, shadow or light-direction parity. `manual=not_run` until user
acceptance. Evidence: `manual-artifacts/task-records/studio-chrome-lighting-20261016`.

Verification on the final frozen production source (VS2022/Win32):

- Normal Release all targets built. Full `build_source.ps1 -TestOnly` completed
  all3,738 registrations:3,670 passed,68 explicit capability/asset opt-in skips,
  zero failures,461.94s. Source-only recovery, no-stock launcher/session cases,
  Null startup and transport/summary checks also passed. No old E4.1 count was
  reused. Initial parallel compiles hit C1060 host-memory exhaustion; bounded
  compiler scheduling completed without changing source flags or assertions.
- Focused Studio GPU/math suite passed8 cases/553 assertions, with no skips.
  Patterned64x64 fixtures prove signed chrome axes, independent normal/position
  bones, translated camera/entity invariance, normal rotation and degenerate
  directions. Exact RGB oracles cover FLATSHADE/FULLBRIGHT/fallback, clamping,
  first-person pitch in both renderer paths and independent additive lights.
  Approved materialization of palette(200,160,100) over lightmap48 produced
  RGB(45,36,23), versus muzzle-lit(139,111,70); FULLBRIGHT produced(200,160,100),
  versus muzzle-lit(255,235,147). Existing exact palette assertions retain the
  same two-byte tolerance and explicitly include the new +Z normal response.
- Opt-in actual crowbar/Glock/.357 production Studio binding passed1 case/
  400 assertions, including all three .357 chrome materials, animation changes
  and camera-local geometry invariance. This is offline installed-asset
  evidence, not a user visual comparison or a stock-engine capture.
- Core-only Release (`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`) all targets built;
  CTest44/44 passed, including the actual-target/include architecture guard.
  The neutral camera helper separately passed3 cases/91 assertions. Debug
  `hlclient_renderer_api` built; the complete Debug application/suite was not
  rebuilt for this follow-up. ASan runtime remains unverified, not promoted
  by any successful ordinary build.
- Final normal Release OpenGL one-frame bootstrap passed without game assets
  on Intel RaptorLake-S, OpenGL3.3.0 driver32.0.101.7026. Unchanged normal
  launcher `-Prediction reference -CheckOnly` passed with process-launches0;
  it still selects `build/bin/Release/hlclient.exe`. No new feature flag,
  resource copying or debug executable is required for the actual game.

Final normal Release client SHA-256:
`DCF20FD56F69EA80F7594839D7A82ED5089938AEE49D35EA9D3EAA4816446A07`.
This identifies this new local build, not any earlier user screenshot/session.
Source baseline is `a22150dae3fcbc4c22e1c015d40fd8d5eb1651b7`; scoped pre-edit
source copies are retained under this task's `source/`. No stage/commit/push,
new live sessions, stock client/HLDS launches, Steam initialization or managed
captures were performed. Manual acceptance remains pending: inspect .357
chrome in the same bright/dark locations while turning/animating, then check
ordinary pickups/player models and muzzle/Egon illumination.

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
