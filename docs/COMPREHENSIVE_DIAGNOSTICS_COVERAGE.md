# Comprehensive multiplayer diagnostics — coverage

This is an evidence inventory for the comprehensive diagnostics change, not a declaration that live gameplay is fully covered. The capability table describes what the Half-Life module knows; the fixture rows below describe events actually observed while exercising production owners with synthetic inputs. Neither is live-server coverage.

All manual gameplay scenarios in this document have status `not_run`. No live server, Steam or HLDS run is represented by these fixtures. Missing server action, hit, miss, target or rewind evidence remains unknown.

## Evidence and interpretation

- The 70 weapon rows below are copied without changing their numeric values from `manual-artifacts/task-records/comprehensive-diagnostics-20261018/focused-release-final.log`. That log ends with **78,841 assertions in 64 test cases, all passed**.
- `production-hooks-release.log` in the same directory records the `[multiplayer-diagnostics]` filter: **4,204 assertions in 17 test cases, all passed**. This is offline production-owner testing, not a networked game.
- `full-release-suite.log` records **3,985 cases: 3,910 passed, 75 skipped**, with **4,701,445 passing assertions**. A skipped installed-asset or optional-context test is not a pass.
- These logs are named snapshots. Later presentation timing/analyzer regression additions must be evaluated against the final test receipt, not retroactively included in an older snapshot's counts.
- Test files/tags in the matrix are exact current source locations and selectors. They identify relevant tests; their presence alone does not prove that every selector ran in one of the logs above.
- Observation-only microbenchmarks do not measure total end-to-end diagnostics cost. This document makes no aggregate gameplay CPU/GPU/FPS or latency overhead claim.

## General owner coverage matrix

“Implemented” means an observation is connected to its existing runtime owner, not a second simulation or parser. “Synthetic” means controlled fixture input, including tests that call the real owner and real renderer. A real OpenGL context remains an offline fixture.

| Owner / area | Runtime implementation | Relevant current tests / tags | Offline evidence and boundary | Manual | Explicit unknown / limitation |
| --- | --- | --- | --- | --- | --- |
| Input | Single SDL dequeue observation, raw bounded input metadata, focus/capture/console ownership, normalized batch identity in normal main path. SDL time and local steady time remain different domains. | `tests/test_input_events.cpp` — `[input][event][comprehensive]`; `tests/test_sdl_input_translation.cpp` — `[platform][sdl][input]`; `tests/test_input_state_tracker.cpp` — `[input][tracker]`; `tests/test_multiplayer_timeline.cpp` — `[multiplayer-timeline][schema]`. | Synthetic typed events test clock separation, held/edge/focus order, limits and serialization. Actual main event routing is implemented; physical-device/OS delivery under gameplay is not reproduced by the DTO tests. | `not_run` | No text/key-content capture; no estimate of hardware sampling time. A console-owned event in an input batch is not a gameplay action. Sampled/omitted raw facts do not imply absent input. |
| Command | Existing scheduler/build/history owners record immutable normalized commands, owner-domain deadline and exact command/input identity; actual wire fields remain separate from intent. | `tests/test_resource_client_response_stage.cpp` — `[goldsrc][live-runtime][multiplayer-diagnostics][lerp-contract]`; `tests/test_multiplayer_timeline.cpp` — `[multiplayer-timeline][cadence]`, `[schema]`. | Fake transport drives the production live-stage scheduler and checks release/history/interpolation values. Cadence fixture reuses the same immutable commands at 30/60/100/144/240 Hz plus jitter/stall; it does not execute another physics model. | `not_run` | No wire `weapon_select` is invented where the protocol has none. Owner scheduler deadlines are not subtracted from steady timestamps without a clock mapping. Bounded retention can omit parts of the chain. |
| TX / ACK | Existing transmission/transport owners report selected new/backup command membership, encoding, successful send and actual ACK facts with independent producer IDs. | `tests/test_goldsrc_usercmd_transmission_stage.cpp` — `[goldsrc][usercmd][transmission-stage][multiplayer-diagnostics]`; `tests/test_resource_client_response_stage.cpp` — `[multiplayer-diagnostics]`; `tests/test_multiplayer_timeline.cpp` — `[negative]`, `[schema]`. | Production codec/transmission stage under fake transport; exact membership, duplicate/nonaccepting sink behavior and no extra send authority. Analyzer fixtures retain the earliest available successful carrier and distinguish backup membership. | `not_run` | ACK proves transport acknowledgement, not server execution, hit or damage. The first retained carrier may not be the first actual send; original-send latency remains unknown if earlier evidence was omitted. |
| RX / commit | Socket-read and decoder/commit owners retain source sequence, actual steady observation stamps, validated committed source identity and rollback separately. | `tests/test_resource_client_response_stage.cpp` — `[goldsrc][live-runtime][multiplayer-diagnostics][lerp-contract]`; `scripts/test_movement_session_analysis.ps1` — receive/commit identity and cross-generation negative fixtures. | Controlled valid and malformed continuations exercise real staged parsing/atomic commit. Offline analysis joins separate read/commit facts only by exact network/map/source sequence. | `not_run` | Read-to-commit interval is an observed interval, not a proven backlog cause. Missing, conflicting or cross-generation source identity yields unknown timing; no timestamp-nearest join. |
| Local movement / prediction | Existing `MovementIncidentRecorder` emits accepted local correction/simulation/camera observations into the same shared timeline; original exact correction pools and whole-session summaries remain. | `tests/test_movement_diagnostics_e32.cpp` — `[movement-diagnostics]`, `[velocity-stop]`; `tests/test_movement_session_diagnostics.cpp` — `[movement-session]`, `[fps-diagnostics]`; `tests/test_priority_movement_capture_b080.cpp` — `[b080][capture-policy]`; `tests/test_multiplayer_timeline.cpp` — `[manual]`, `[priority]`, `[cadence]`. | Synthetic owner observations verify correction identity, raw/physical/display separation, delayed GPU ownership, OFF behavior, caps and delayed F8. Dense 5,000-event cosmetic noise cannot erase the protected local action before F8. | `not_run` | A correction or hold is not automatically a prediction defect: server damage/knockback, collision and missing intermediate steps may be unknown. Not every per-step collision is retained. |
| Remote players | Existing remote observation owner emits selected source-pair/sample/render/support/pose data with independent actor lifetime/generation identity; existing remote recorder remains the owner. | `tests/test_remote_movement_diagnostics.cpp` — `[e32-remote-recorder][movement-diagnostics]`; `tests/test_remote_player_presentation.cpp` — `[game][remote-player]`, `[pose-handoff]`; `tests/test_remote_support_presentation.cpp` — `[remote-support][movement][brush][no-stock]`. | Synthetic pairs, holds, discontinuities, reentry, crouch/gait and moving-support fixtures exercise production presentation and recorder paths. | `not_run` | Presence in a render/input list is not proven pixel visibility. No server rewind pose or remote command history is available. Support/render offset is presentation evidence, not server collision authority. |
| Ordinary Studio / NPCs | `RuntimeReplayLocalAssets` observes actual raw frame/sequence/animtime/framerate/controllers/blending and evaluated cached pose without a second pose evaluation. | `tests/test_runtime_replay_local_assets.cpp` — `[multiplayer-diagnostics][ordinary-studio-diagnostics][local-capture-assets][no-stock]`, `[bounds]`; `tests/test_multiplayer_timeline.cpp` — `[schema][studio]`. | Synthetic imported MDL and real presentation owner compare diagnostic ON/OFF bone matrices, pose evaluations/reuses, raw/evaluated separation, negative framerate, absent fields, generation changes and bounded 128-entry suppression cache. | `not_run` | Render-frame/model-revision/visibility/hold values stay absent when the owner cannot establish them. Raw animation state is not a damage hitbox or server rewind proof. Cache eviction/repetition counters disclose omitted detail. |
| Weapons / actions | Actual Half-Life registry and existing controllers/host expose intent, accepted/begin/end/cancel, state observations, gates, committed sound/temp/scripted evidence and persistent-object visibility. | `tests/test_weapon_diagnostics.cpp` — `[multiplayer-diagnostics][weapon-diagnostics][no-stock]`; `tests/test_game_client_module.cpp` — `[multiplayer-diagnostics][game-module][game-api][core-only]`; 70 rows below. | Fourteen registry profiles, five modes each; real controller acceptance before visual sampling, held shots, charge/beam lifecycle, reload/selection, gates, duplicate suppression, source class identity and explicit unknowns. | `not_run` | Capability != observed action != server result. Local pellet count/spread or a cosmetic event is not authoritative per-pellet hit/miss. Visibility enter/leave does not prove object spawn/destruction. No nearest-shot ownership association. |
| World / brush / support | Existing movement support/query evidence and read-only accepted-action world/brush query feed shared records. Explicit lifecycle/damage/use observations are retained where existing owners expose them. | `tests/test_multiplayer_presentation_diagnostics.cpp` — `[comprehensive][geometry][production-path]`; `tests/test_movement_diagnostics_e32.cpp` — `[velocity-stop]`; `tests/test_remote_support_presentation.cpp` — `[remote-support]`; `tests/test_weapon_diagnostics.cpp` — `[weapon-diagnostics]`. | Controlled blocker/support identity, start/end/fraction and missing geometry; fixture incoming damage belongs to the receiving player without inventing attacker/shot identity. | `not_run` | This is not a complete world-event journal. Every door/button/trigger or intermediate hull step is not independently instrumented. Current local world query is not historic server world state. |
| Presentation / timing | Normal main path records render submission, swap begin/end, actual measured frame interval and ready-only asynchronous GPU result with its original frame/generation. | `tests/test_multiplayer_timeline.cpp` — `[multiplayer-timeline][presentation-timing]`; `tests/test_movement_diagnostics_e32.cpp` — delayed GPU cases under `[movement-diagnostics]`; `tests/test_movement_session_diagnostics.cpp` — `[fps-diagnostics]`; analyzer frame/GPU join fixtures. | Synthetic swap/source/poll identities and real recorder serialization ensure delayed GPU data cannot be relabelled as the polling frame. Offline analysis preserves known frame/GPU values independently. | `not_run` | Swap completion is not scanout/input-to-photon latency. GPU values remain unknown until a query is ready; no blocking query is introduced. A measured long scope does not establish the physics cause. |
| Read-only geometry | Last completed bounded CPU-frame proxy, actual final camera/viewport and pose hitboxes; accepted-action queries retain local proxy and current-world provenance separately. | `tests/test_multiplayer_presentation_diagnostics.cpp` — `[comprehensive][geometry]`, `[production-path]`, `[opengl][actual-context]`; `tests/test_multiplayer_timeline.cpp` — `[negative]`. | Affine MDL transforms, actual production scene/camera path, blockers, stale/generation reset and missing pose negative controls. An actual offline GL context verifies camera effects/projection, not a live server hit. | `not_run` | Source-model approximation only: not server collision, rewind, weapon reach/spread or authoritative target. Query/candidate budgets, unavailable pose or old frame keep negative/closest-result conclusions unknown. Proxy lifetime memory is separate from the 2 MiB event storage. |
| Analyzer / reports | Same bounded local report consumer validates schema/run/manifests, exact causal IDs, distinct clocks, omissions and evidence strength; optionally publishes escaped local HTML. | `scripts/test_movement_session_analysis.ps1`; producer-output fixture from `tests/test_multiplayer_timeline.cpp` via `HLCLIENT_MULTIPLAYER_TIMELINE_FIXTURE_OUTPUT` / `HLCLIENT_MULTIPLAYER_TIMELINE_FIXTURE_INPUT`. | Positive/intentional-negative fixtures cover ACK/cosmetic authority, wire-vs-effective lerp, aim mismatch, hold with fast frame, receive/commit, exact backup carriers, frame/GPU order, stale proxy, bounded malformed inputs and manifest mismatch. | `not_run` | Findings are observations/candidates, not global “healthy” or “broken” verdicts. Partial/absent manifests are unknown; validated metadata is not a current executable rehash. Incomplete retained chains are disclosed, not declared complete. |
| Launcher | Existing opt-in diagnostics setting and F8 retained; Open report invokes the fixed offline analyzer for the owned run and local bounded HTML, with the existing report fallback. | `tests/test_external_launcher_e31.cpp` — `[e32][e31][launcher][config]`, `[e33][launcher][reporting][no-stock]`; `scripts/test_movement_session_analysis.ps1` — explicit run/HTML/manifest fixtures. | Configuration default/migration and existing launcher ownership/report state are covered offline; analyzer publication is fixture-tested. GUI click-to-report integration is source-implemented, not an automated human-launcher session. | `not_run` | No claim that a live user's browser/PowerShell availability, settings or run-selection interaction was manually verified. Open report does not connect to a server. |

## Actual registry-driven weapon rows

The formatter is `coverage(...)` in `tests/test_weapon_diagnostics.cpp` (lines 38–56). It iterates observed `MultiplayerActionPayload` events matching the row's weapon ID and mode. Its columns are **counts**, not boolean pass flags:

- `kind`: numeric declared capability kind for that mode.
- `intent`: count with phase `intent`.
- `accepted_or_begin`: count with phase `accepted` or `begin`; the fixture separately verifies actual local acceptance where expected.
- `server_observed`: count of that phase in synthetic committed/state observations. It is not a live server run or proof of a hit.
- `rejected`: count of diagnostic rejected/gated/unresolved local intent. It is not the number of failed tests.

Kinds used here: `0 unknown/unsupported`, `1 fire`, `2 melee`, `3 projectile`, `4 pellets`, `5 charge`, `6 beam`, `7 throw_object`, `8 place`, `9 detonate`, `10 reload`, `11 deploy`, `12 holster`, `13 zoom`, `17 guidance`.

Interpretation must distinguish unsupported modes and gates:

- A declared `kind=0` mode has no supported action in this catalogue. Its emitted intent and possible rejected count test honest unsupported handling; zero accepted is expected, not a broken weapon.
- Supported zoom/guidance are non-firing modes. Zero local accepted/begin does not disprove support. Zoom rows include synthetic server-observed FOV change; guidance remains unresolved by that fixture's local acceptance path. Do not infer a precise rejection reason from this count-only table.
- Deploy/holster rows come from synthetic authoritative selection changes and therefore show state observations, not fabricated local fire acceptance.
- Ordinary gates (cooldown/ammo/death/freeze), duplicates, charge/beam stop and incoming-event authority are separate cases in the same test file, not extra rows silently counted in this matrix.
- The Gauss reload row is retained exactly as logged (`kind=0, intent=1, accepted=0, server_observed=0, rejected=0`); unsupported is determined by the capability, not by forcing every unsupported intent to emit a rejection.

All 70 rows: **offline synthetic production-owner fixtures; manual/live `not_run`**.

| Weapon ID | Registry command | Mode | Kind | Intent | Accepted or begin | Server observed | Rejected |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | weapon_crowbar | primary | 2 | 1 | 1 | 0 | 0 |
| 2 | weapon_9mmhandgun | primary | 1 | 1 | 1 | 0 | 0 |
| 3 | weapon_357 | primary | 1 | 1 | 1 | 0 | 0 |
| 4 | weapon_9mmAR | primary | 1 | 1 | 1 | 0 | 0 |
| 6 | weapon_crossbow | primary | 3 | 1 | 1 | 0 | 0 |
| 7 | weapon_shotgun | primary | 4 | 1 | 1 | 0 | 0 |
| 8 | weapon_rpg | primary | 3 | 1 | 1 | 0 | 0 |
| 9 | weapon_gauss | primary | 6 | 1 | 1 | 0 | 0 |
| 10 | weapon_egon | primary | 6 | 1 | 1 | 0 | 0 |
| 11 | weapon_hornetgun | primary | 3 | 1 | 1 | 0 | 0 |
| 12 | weapon_handgrenade | primary | 7 | 1 | 1 | 0 | 0 |
| 13 | weapon_tripmine | primary | 8 | 1 | 1 | 0 | 0 |
| 14 | weapon_satchel | primary | 7 | 1 | 1 | 0 | 0 |
| 15 | weapon_snark | primary | 7 | 1 | 1 | 0 | 0 |
| 1 | weapon_crowbar | secondary | 0 | 1 | 0 | 0 | 1 |
| 2 | weapon_9mmhandgun | secondary | 1 | 1 | 1 | 0 | 0 |
| 3 | weapon_357 | secondary | 13 | 1 | 0 | 1 | 1 |
| 4 | weapon_9mmAR | secondary | 3 | 1 | 1 | 0 | 0 |
| 6 | weapon_crossbow | secondary | 13 | 1 | 0 | 1 | 1 |
| 7 | weapon_shotgun | secondary | 4 | 1 | 1 | 0 | 0 |
| 8 | weapon_rpg | secondary | 17 | 1 | 0 | 0 | 1 |
| 9 | weapon_gauss | secondary | 5 | 1 | 1 | 0 | 0 |
| 10 | weapon_egon | secondary | 0 | 1 | 0 | 0 | 1 |
| 11 | weapon_hornetgun | secondary | 3 | 1 | 1 | 0 | 0 |
| 12 | weapon_handgrenade | secondary | 0 | 1 | 0 | 0 | 1 |
| 13 | weapon_tripmine | secondary | 0 | 1 | 0 | 0 | 1 |
| 14 | weapon_satchel | secondary | 9 | 1 | 1 | 0 | 0 |
| 15 | weapon_snark | secondary | 0 | 1 | 0 | 0 | 1 |
| 1 | weapon_crowbar | reload | 0 | 1 | 0 | 0 | 1 |
| 1 | weapon_crowbar | deploy | 11 | 0 | 0 | 1 | 0 |
| 1 | weapon_crowbar | holster | 12 | 0 | 0 | 1 | 0 |
| 2 | weapon_9mmhandgun | reload | 10 | 1 | 1 | 0 | 0 |
| 2 | weapon_9mmhandgun | deploy | 11 | 0 | 0 | 1 | 0 |
| 2 | weapon_9mmhandgun | holster | 12 | 0 | 0 | 1 | 0 |
| 3 | weapon_357 | reload | 10 | 1 | 1 | 0 | 0 |
| 3 | weapon_357 | deploy | 11 | 0 | 0 | 1 | 0 |
| 3 | weapon_357 | holster | 12 | 0 | 0 | 1 | 0 |
| 4 | weapon_9mmAR | reload | 10 | 1 | 1 | 0 | 0 |
| 4 | weapon_9mmAR | deploy | 11 | 0 | 0 | 1 | 0 |
| 4 | weapon_9mmAR | holster | 12 | 0 | 0 | 1 | 0 |
| 6 | weapon_crossbow | reload | 10 | 1 | 1 | 0 | 0 |
| 6 | weapon_crossbow | deploy | 11 | 0 | 0 | 1 | 0 |
| 6 | weapon_crossbow | holster | 12 | 0 | 0 | 1 | 0 |
| 7 | weapon_shotgun | reload | 10 | 1 | 1 | 0 | 0 |
| 7 | weapon_shotgun | deploy | 11 | 0 | 0 | 1 | 0 |
| 7 | weapon_shotgun | holster | 12 | 0 | 0 | 1 | 0 |
| 8 | weapon_rpg | reload | 10 | 1 | 1 | 0 | 0 |
| 8 | weapon_rpg | deploy | 11 | 0 | 0 | 1 | 0 |
| 8 | weapon_rpg | holster | 12 | 0 | 0 | 1 | 0 |
| 9 | weapon_gauss | reload | 0 | 1 | 0 | 0 | 0 |
| 9 | weapon_gauss | deploy | 11 | 0 | 0 | 1 | 0 |
| 9 | weapon_gauss | holster | 12 | 0 | 0 | 1 | 0 |
| 10 | weapon_egon | reload | 0 | 1 | 0 | 0 | 1 |
| 10 | weapon_egon | deploy | 11 | 0 | 0 | 1 | 0 |
| 10 | weapon_egon | holster | 12 | 0 | 0 | 1 | 0 |
| 11 | weapon_hornetgun | reload | 0 | 1 | 0 | 0 | 1 |
| 11 | weapon_hornetgun | deploy | 11 | 0 | 0 | 1 | 0 |
| 11 | weapon_hornetgun | holster | 12 | 0 | 0 | 1 | 0 |
| 12 | weapon_handgrenade | reload | 0 | 1 | 0 | 0 | 1 |
| 12 | weapon_handgrenade | deploy | 11 | 0 | 0 | 1 | 0 |
| 12 | weapon_handgrenade | holster | 12 | 0 | 0 | 1 | 0 |
| 13 | weapon_tripmine | reload | 0 | 1 | 0 | 0 | 1 |
| 13 | weapon_tripmine | deploy | 11 | 0 | 0 | 1 | 0 |
| 13 | weapon_tripmine | holster | 12 | 0 | 0 | 1 | 0 |
| 14 | weapon_satchel | reload | 0 | 1 | 0 | 0 | 1 |
| 14 | weapon_satchel | deploy | 11 | 0 | 0 | 1 | 0 |
| 14 | weapon_satchel | holster | 12 | 0 | 0 | 1 | 0 |
| 15 | weapon_snark | reload | 0 | 1 | 0 | 0 | 1 |
| 15 | weapon_snark | deploy | 11 | 0 | 0 | 1 | 0 |
| 15 | weapon_snark | holster | 12 | 0 | 0 | 1 | 0 |

## Retention and missing-evidence contract

The shared timeline is a component of the existing movement recorder, not a separate weapon logger. It has an explicit 2 MiB allocation bound; action/lifecycle/query protection and command/carrier protection have separate bounded pools. F8 uses the existing four-second pre-window and one-second post-window, with 64 events per shared window and eight windows. Within a full priority class, nearer-to-marker events are retained with stable ties; exact local action protection is not washed out by ordinary cosmetic feedback. These are bounded priority guarantees, not guarantees that every dense five-second chain survives.

The existing local 8 MiB capture limit and 16 MiB final-report bound remain explicit; terminal capability/manifest additions have a reserved budget. Allocation failure, sampling, overflow, cache eviction, rejected invalid observations and encoding deadline omissions remain reported. OFF does not collect events or allocate the shared timeline.

A server ACK, local trace, muzzle effect, sound, animation or model label never becomes authoritative hit/miss evidence. No undocumented guessed IDs, clock equivalence, actor identity or server rewind pose fills an absent field. A capability row and a passing synthetic test do not remove the need for a future manually identified session with usable, matching manifests.
