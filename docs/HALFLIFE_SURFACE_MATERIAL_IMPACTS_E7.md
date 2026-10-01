# E7 — Half-Life static BSP material impacts

Status: normal `hlclient.exe` implementation; new E7 Release manual=not_run.
The opt-in third-party [manual test-map workflow](EXTERNAL_MANUAL_TEST_MAPS.md)
does not change E7 semantics or count as a manual E7/E7.1 validation.
The prior E6 manual report is recorded, with its exact scope, in
`LOCAL_CROWBAR_WORLD_IMPACTS_E6.md`.
The pre-edit source snapshot is under
`manual-artifacts/task-records/hlc-e7-surface-material-impacts-20260929/source/`.

The generic `WorldImpactPresentation` continues to trace the BSP collision
world, reject nearer brush blockers and map the hit to exactly one opaque
render-surface triangle. Its result now owns the source surface ordinal,
source material ordinal and bounded canonical BSP texture name from that
same selected range. This works even when the decal image is unavailable;
geometry, decal timing, clipping and rendering are unchanged. Ambiguous edge
hits remain rejected rather than borrowing a coplanar neighbor's texture.
The `GameClientHost` transports only this neutral value. Neither collision,
renderer nor mixer links Half-Life material rules.

The module owns a session-local `HalfLifeMaterials` table shared with the
existing movement-audio policy. It receives `sound/materials.txt` bytes from
the existing exact-root, identity-checked local resource path, no arbitrary
filesystem fallback. It copies at most 512 entries from at most 128 KiB;
lines above 511 bytes, unknown codes and malformed entries reject the table.
An empty/unavailable file is typed missing. Duplicate normalized twelve-byte
keys take the first entry deterministically; the pinned SDK's duplicate sort
order is unspecified. The installed file has such duplicates, so this is a
deliberate local policy. Traced names are ASCII uppercased and truncated to
the first twelve bytes after removing one `+x`/`-x` animation frame prefix
and then one `{`, `!`, `~` or space marker. Table names are not stripped.
Known `C/M/D/V/G/T/S/W/P/Y/F/N` codes have project-owned enum values.
Unknown texture names, a missing table and a rejected table all use the SDK
concrete fallback, but have distinct diagnostic source categories. Flesh on
static BSP deliberately uses the default concrete sound, not entity/blood
semantics. The pinned SDK has no snow-specific impact case, so snow also
uses concrete/default. Neither fallback invents a material from visual color.

The impact family follows pinned Valve SDK revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`, `pm_materials.h`,
`pm_shared.c`, `cl_dll/ev_hldm.cpp` and `dlls/crowbar.cpp` as behavioral
references, not copied implementation. Concrete uses `pl_step1/2`, metal
`pl_metal1/2`, dirt `pl_dirt1/2/3`, vent `pl_duct1`, grate `pl_grate1/4`,
tile `pl_tile1/2/3/4`, slosh `pl_slosh1/2/3/4`, wood `debris/wood1/2/3`,
glass/computer `debris/glass1/2/3`. All names are relative virtual names
under the approved `sound/` namespace. The accepted command sequence picks a
deterministic variant; replay, scene rebuild and camera motion cannot choose
again. Glock emits one world-space material voice plus its unchanged fire,
decal, flash, light and shell effects. Crowbar emits one material voice and
one `cbar_hit1/2` weapon-strike voice, the latter at the SDK's material-
dependent reduced `fvolbar` gain. Miss emits neither. Normal pitch is retained:
the project's current presentation path does not claim SDK randomized pitch.
The original E7 handoff did not include the SDK's separate optional
ricochet cue; the E7.1 correction below adds that audio layer. Damage,
penetration and moving/entity surface effects remain out of scope.
The existing local-audio virtual-name validator is narrowly extended to
allow `debris/*.wav` only for the pinned Half-Life profile source; arbitrary
model-event paths remain disallowed.

All 24 distinct material profile sounds are requested once at local projection
readiness through `ApprovedSoundAssets`; no contact callback performs file
I/O. Existing asynchronous cache limits (64 local names, 32 MiB PCM),
250 ms pending-cue deadline and typed resource/voice failure counters remain.
`-MuteGlockFireSound` still removes only the Glock fire voice. The optional
network diagnostic emits at most 32 accepted hit lines with bounded surface
ID, normalized key, material, classification source, selected virtual sample
and immediate resource status; existing aggregate mixer counters distinguish
voice submission. An immediate `pending` status is not proof of audible PCM.

This is deterministic local-compatible static-world presentation, not
server-authoritative material/damage or stock-binary parity. The source eye
and direction, blocker representation, line-only crowbar reach, local sample
selection and unsupported entity surfaces retain E5/E6 limits. Normal game
assets and audio device are optional read-only controls; mandatory tests use
project-owned synthetic fixtures. Actual E7 audibility and visual result
remain for a new user manual test.

## E7.1 original Glock hit-sound correction (2026-09-30)

New manual feedback (`attribution=user_report`): the E7 sounds work, but
material hits sound like footsteps; the user wants the original Half-Life
impact sound. This is an accurate perception, not a bad asset mapping:
the pinned SDK's `EV_HLDM_PlayTextureSound` intentionally uses the
`player/pl_step*` and related material samples. For a Glock BSP hit, the SDK
also calls `EV_HLDM_GunshotDecalTrace`, which independently emits one of
`weapons/ric1..5.wav` for roughly half of hits. E7 had omitted this second
sound. `weapons/bullet_hit1/2.wav` belongs to flesh hits and is not a valid
replacement for a static concrete or metal wall.

The normal application now pre-requests all five `ric` samples from the same
approved root as the world. A confirmed Glock static-BSP hit keeps its
material voice and, when the action-stable 15-bit local gate selects one,
adds an independent spatial `ric` voice at the frozen hit point. Selection
uses the SDK's half-range rule and five-sample family but an independently
authored deterministic hash of network generation and command sequence;
the exact stock mutable RNG sequence is not claimed. The optional cue has
its own marker ordinal/serial/automatic voice; replay and repeated updates
cannot create another. Missing `ric` does not suppress the material voice,
decal, shell or muzzle effects. Miss and crowbar do not acquire `ric` audio.
`-MuteGlockFireSound` still mutes only the fire sample. Bounded opt-in hit
diagnostics now include `supplemental_sample=none|weapons/ricN.wav`.

The new Release's manual listening outcome is `not_run`. User confirmation
of E7 material sounds does not establish E7.1 audibility. A Glock wall hit
will not produce `ric` on every shot, consistent with the pinned SDK's
optional behavior; compare several shots, optionally with fire muted.

## Original E7 offline verification (2026-09-30; superseded executable)

- Final Release `hlclient.exe` and `hlclient_tests.exe` built from the E7 source.
  The full Release offline test binary passed with seed `1943821004`:
  2,428 cases, 2,395 passed, 33 skipped, and 819,980/819,980 assertions.
  Its first run at that seed had one unrelated Windows fake-server UDP
  readiness timing failure (`query_timeout` versus `query_source_mismatch`);
  the exact case passed alone and the full suite passed on rerun at the same
  seed. This is a known test flake, not erased from the record.
- The final Debug focused `[e7],[world-impacts],[crowbar-impacts]` run passed:
  32 cases, 29 passed, 3 opt-in local-asset skips, 901/901 assertions.
  The two installed-materials controls passed separately in Release against
  the read-only research copy: 2 cases and 151/151 assertions. Mandatory
  tests require no installed game or audio output device.
- The existing `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` core-only build and its
  offline core API and architecture-guard tests passed (2/2). Launcher
  `-CheckOnly`, no-stock PowerShell fake-runner matrix and `git diff --check`
  passed. No live, stock-client or managed game session was launched for E7.
- ASan runtime and real-device listening were not verified. The Release SHA-256
  is `FBC9117D5BC28151859C9B545137FAF7BF5A3316F7DD64CA908AD2FADBA50BF5`.
  New E7 manual validation remains `not_run`; an earlier E6 user report does
  not validate this build.

## E7.1 offline verification (2026-09-30)

- After the final production edit, the Debug focused
  `[e7],[world-impacts],[crowbar-impacts],[audio-diagnostic]` run passed
  32 cases, 3 opt-in skips and 950/950 assertions. The read-only installed
  materials and all five `ric` WAV decode controls passed 181/181 assertions
  in 2 cases.
- `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` core-only build and its API/architecture
  tests passed 2/2. The full Release offline suite passed 2,397 cases with
  33 capability/opt-in skips and 820,023/820,023 assertions. The unchanged
  launcher passed `-Prediction reference -CheckOnly`; `git diff --check`
  and the no-stock PowerShell launcher fixture passed.
- New live launches, HLDS, stock client and managed captures: zero. ASan
  runtime and real-device listening were not run. No staging, commit or push
  was performed. Current normal Release `build/bin/Release/hlclient.exe`
  SHA-256: `337CFFCBE18E713358CC8FBFF3EC2B9F521A6AA29A9C3D0589B59C92FD2A986D`.
  E7.1 manual listening remains `not_run`.
