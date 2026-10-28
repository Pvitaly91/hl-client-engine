# CPU Studio/submission — 0c29f4ca

Статус: **no_candidate_accepted_source_restored — P1/P2/B2/B3 відхилені; мета оптимізації не досягнута, нового FPS gain або нової доставки немає**.

Попередні звіти, сирі виміри, невдалі повтори й кандидат `6E27…` збережені як історія. Merge/reserve, GL-cache, B2 metadata-plan та B3 command-local setters **вилучені з production**. Усі чотири змінювані engine/client source files byte-identical frozen B0. Залишено корисні no-stock tests/діагностичні докази та попередні користувацькі launcher changes. Подальших optimization variants, retune або timing reruns у цій кампанії не буде.

## Стан source і доставки

Репозиторій `D:/DEV/CPP/HL-Client-engine`, `main`, HEAD `6a86ce0e3b826341c90bbcee8c603771f1dc58c8` плюс незакомічені зміни. Попередня launcher/fullscreen правка користувача збережена; це не clean-commit claim. Git stage/commit/push, нові live/Steam/HLDS/game sessions — none. Працювали лише offline fixtures; чужі процеси не завершували.

Build: VS2022 v143/MSVC14.34.31933, Win32 Release `/O2`. Frozen B0 source/headers/tests/libs/binaries лишаються у task-local `baseline`. Harness/obj/EXE baseline є ізольованою збіркою з тим самим тестом, а не незміненою clean-tree програмою.

P1 повернено **byte-for-byte** до frozen B0:

| Source | SHA-256 після withdrawal |
| --- | --- |
| `src/goldsrc/studio/goldsrc_studio_pose.cpp` | `d2f3a3a22048c61e5f42243f8738823eb5041cf28883312d6c18d9f54b9003f5` |
| `include/hlclient/goldsrc/studio/goldsrc_studio_pose.hpp` | `c79921250548e68f3ebbb7dfb98e0e238b204778880f1946ccb5a54f5ea3e015` |
| `src/app/runtime_replay_local_assets.cpp` | `c6dd1cc0dda5ba04bfe564b28c5029181c82a137d17c50f77367f81887d35680` |

Раніше прийняті A–D compose/metadata та world/MSAA/GL3.3 виправлення збережено. Вилучені лише експериментальний local-only child merge, чотири palette reserve і три пов’язані telemetry-comment рядки. Bitwise/rejection oracle лишився для перевірки початкового алгоритму.

Після P2, B2 та B3 renderer повернуто byte-for-byte до frozen B0 SHA `a78c66ae53104c8acc6502608039557bb3ab2e0556c689e9f124aaf65cef77d9` (62 965 bytes). B2 `2d45…` і B3 `ef0c6710734952978a318b1e4d2354738950c0c3dfe1601ab038aa3a6734c074` — лише frozen rejected history, не current/shipped source. Нормальний test macro повернутий до baseline0.

Історична незалежна delivery-observation від `2026-10-23T22:58:25.9484746Z` підтвердила незмінні local/shared/dist:

- client: `7765414555a74e4ab5cd3bdf7ddce9da03d998c4ba76659acf43fce92022cd22`;
- launcher: `9d401a751adc933831f86bcd52d6c468fb801981d1af4fb499e44530ce1fb9da`;
- approved delivery SDL: `ad833f09fad8d99eadfdb1a765a76f9f20872a3ab6fadbef648f09334f2fac87`.

Старий private candidate `6e27bf4f88534987ab3bac628ccfedc4e4f6929974ba998e9979e79da8732327` містив P1/P2; він **rejected/not delivered**, не B3 кандидат. Його build/dependency/correctness passes не переносяться на B3. Нову доставку ще не виконано; settings/auth/logs/shared test-data не копіювалися.

Final read-only audit `final-baseline-delivery-observation.json` від `2026-10-24T00:11:27.6194462Z` підтвердив754 production files,0 mismatch до frozen B0; у1220 baseline entries різняться лише5 test files. Попередня dirty launcher fix також exact B0 і збережена. Local Release/dist усі4 shipped binaries exact; SHA256SUMS усі15 entries valid; canonical ZIP16 files exact dist, SHA `67b9b79ae631b34efb747cbc42f7f144fb6eaab5b1253da190007e9e66515412`.

Shared path під час остаточної read-only спроби **недоступний** (`Cannot find path`), тому його4 current binary hashes **verification_unavailable**. Ранішній успішний shared audit — історія, не свіжа перевірка. Жодного shared write/replacement не пробували; нової доставки або replacement_pending немає, бо0 accepted fixes/new releases. Повну current two-PC verification/delivery не заявляємо.

## P0 — що доведено про первинний запис

Run `0c29f4caaaad3ed789ab8ee1474e4f70`: оригінальний ZIP 8 034 036 bytes, 36 entries, без EXE, SHA `3f3dd270d6fc495f64f4853e56e698423b703b518dbf80de2d0af453d43d9651`. Retained movement JSON: `0d29077b2002f176cc549b3cf6ea8136ddea1434f93c40f141d4b3abf0cf2964`.

6520 controlled-alive frames / 6516 придатних intervals: mean7.710682 ms, CPU update5.069180, inclusive Studio3.359557, submission1.646015, GPU2.635528, parse/commit0.431767. Actual1920×1080, AA Off/VSync Off/cap0/neutral; resolve/adjust/copy passes не виконувалися.

Frame7775 retained: interval18.6914, update14.0231, Studio11.2728, parse0/records0/replay0, prediction0.9882, submission3.283, GPU2.850848 ms. No RX не означає незмінну current-time pose. Exact details5545/1794/5461 у primary JSON не retained; historical subcall cause не встановлена.

`studio_ms` включає present_entities та viewmodel handle/materialization: history, interpolation, projection, local sampling/composition/merge, sockets/bounds/light/publication/diagnostic producers. Це не лише bone arithmetic. CPU/GPU/update/Studio scopes перекриваються; scoped reciprocal не називається FPS гри. Діагностичні producers/scheduler/ACK/events/physics заради цифр не переносилися й не вимикалися.

## Завершений P1 — rejected, не поточна оптимізація

Final quiet-host phase3: 6 processes exit0, 360 scope rows, 92 160 scope samples, **18 432 різні measured frames**, 64 warm+256 samples/cell. Усі inputs/argv/raw hashes, Off/On, 1/4/8/12/16 remotes та16-actor burst перевірені. Signatures, matrix values/order, palette counts і time-varying no-RX poses збігаються точно.

Жоден із present_entities/owned-total cases не подолав свій noise gate:

| 16 actors, steady, Diagnostics Off | B0 median run-p50, µs | P1, µs | Gain | Gate |
| --- | --- | --- | --- | --- |
| present_entities inclusive producers | 712.2 | 696.1 | 16.1 | 45.61 |
| owned_visual_update_subset | 737.8 | 727.3 | 10.5 | 46.89 |

153 final tail warnings не відкинуто. Present p99: B0 `1012.0/1283.1/1067.2` → P1 `1623.6/1515.7/1213.0` µs; **усі три pairs гірші**, median+448.5 µs. Owned p99 median1139.1→1673.2 µs. Причина конкретних хвостів не встановлена; повторювані хвости й відсутність gain достатні для rejection без вигаданої attribution.

Structural merge N+U→U та actual published palette capacity417→385 slots /26688→24640 bytes для16 actors залишаються валідним історичним operation/capacity proof. Це не CPU gain, actual heap allocation count чи GPU residency. **Merge/reserve правки вилучені**; їх не доставляємо.

## Завершений P2 GL-cache — strict rejected

Початкові 48 processes /8 workloads та заздалегідь обмежені18 follow-up processes збережені повністю: **66 runs,12 672 CPU samples,264 timed proof rows**. Hash/context/actual AA0/2/4/8/output controls пройшли; color, resolved depth, ordered draws і pose uploads збігаються. Жодного skip/warning або відкидання поганих repeats.

| Фіксований follow-up | Gain median run-p50, ms | Frozen gate, ms | Комбінований six-repeat gain | Результат правила |
| --- | --- | --- | --- | --- |
| 16 actors AA0 | 0.05510 | **>0.05560** | 0.07910 | **FAIL** |
| 16 actors AA2 | 0.17900 | non-regression0.07180 | 0.12685 | control pass |
| 8 actors AA0 | 0.03080 | non-regression0.04020 | 0.02255 | control pass |

Primary follow-up0.3195→0.2644 ms не досяг frozen gate на **0.00050 ms (0.5 µs)**. Комбінований gain і2/3 сприятливих paired results не замінюють окрему обов’язкову median умову. **Не округлюємо до pass, не послаблюємо gate і не повторюємо той самий код до вдалого результату.**

Median follow-up AA0-16 p95 .5312→.4771, p99 1.2047→.5615 ms; combined p95 .71470→.49695, p99 1.28075→.71215. Погані repeats також відкриті: новий AA2-r2 max1.2024→1.6391, AA0-16-r2 p99 .6825→.9725, AA0-8-r2 p99 .4814→.5410/max .6626→.9664 ms. Початкові AA2-r1/r2 не визнані invalid і не стерті.

P2 `StudioSubmissionState`, byte-uniform/program/VAO/texture/blend reuse **більше немає у production source**. Sprite bind expectation повернуто до baseline+4; корисні opaque/masked class checks збережені. Немає AA2-specific bypass чи forced AA Off.

## Final actual-assets — старий rejected P1/P2 кандидат

Quiet-host final actual-assets:24 processes exit0, AA Off/8×/2×/4×, Diagnostics Off/On, neutral1080p, approved installed assets, 8 synthetic players+12 pickups/550 visible bones. 48 measured cells×192 = **9216 distinct frames**, 768 scope rows /147456 overlapping scope samples. Це fixed synthetic scene/camera/server-time, не historical replay, live game чи target Windows10 PC.

| AA | Diagnostics | Studio B0→B1 p50, ms | Studio gate | Submission B0→B1 p50, ms | Submission gate |
| --- | --- | --- | --- | --- | --- |
| Off | Off | .8146→.8042 | .050730 | .4844→.4403 | .034220 |
| Off | On | .8151→.8053 | .050755 | .4855→.4232 | .048000 |
| 8× | Off | .8890→.8612 | .057000 | .6007→.5767 | .057800 |
| 8× | On | .9023→.8851 | .152500 | .5979→.5964 | .091000 |
| 2× | Off | .8386→.8441 | .051930 | .5629→.5331 | .038145 |
| 2× | On | .8496→.8365 | .052480 | .5755→.5271 | .038775 |
| 4× | Off | .8614→.8487 | .053070 | .5777→.5412 | .040400 |
| 4× | On | .8611→.8459 | .053055 | .5766→.5345 | .038830 |

Studio gain cells0; render-submit cells4 поза локальним p50 gate не скасовують strict P2 rejection і не доводять FPS. 329 tail warnings retained. AA0 DiagnosticsOn Studio p99 `1.3562/1.3828/1.3551` → `2.2150/1.5192/1.6175` ms: усі pairs гірші.

Full-scene color signatures та draw/pose/geometry counts exact B0/B1/Off/On у кожному AA. Separate world-only owned resolved-depth witnesses exact. Main scene MSAA depth=null — **не entity/viewmodel depth proof**; null==null не validation. P2 synthetic full-scene depth proof — інший окремий oracle, не підміна цієї межі.

## Завершений B2 — metadata-only, strict rejected

B2 не успадковував acceptance P1/P2. Експеримент мав8 optional FIFO slots на existing StudioGpuAsset з exact immutable-owner/full-body/skin key й owning ordered mesh/material payload. Metadata range/typed validation, atomic commit, duplicates та GL calls лишалися контрольованими; source-operation savings не означали CPU pass.

Direct B0/B2:4 cases і36 unique keyed proof records на кожен binary exact. Початковий wrapper exit1 помилково вимагав однакового порядку незалежних Catch cases за різних RNG seeds; усі cases самі пройшли. Порівняння complete keyed records з multiplicities виправлене без binary rerun; порядок draw/uniform/pose всередині кожного frame не ігнорувався.

Фіксована кампанія повністю завершена:50 processes,96 warm+512 measured samples/run,25 600 CPU samples,200 timed proof records, усі outputs/GL3.3/AA context exact. Один process — один run, frames не pooled. Усі50 runs, cold/memory/max/tails збережені.

| B2 group | Median paired p50 gain, ms | Baseline-only G, ms | Результат |
| --- | --- | --- | --- |
| 16 actors AA0 primary | **-0.00010** | **0.06070** | **FAIL** |
| 8 actors AA0 | 0.00820 | 0.03340 | control pass |
| 16 actors AA8 | 0.02920 | 0.08070 | control pass |
| 16 actors AA2 | 0.00300 | 0.07400 | control pass |
| 16 actors AA4 | 0.00170 | 0.10040 | control pass |

Primary paired p50 gains: `0.0147/0.0073/-0.0228/-0.0243/-0.0001` ms; лише2/5 positive і0/5>G. Median p99 `0.7054→0.8527` ms, +0.1473>G;4/5 paired p99 також гірші більш ніж G. Чотири control passes не заміняють primary significance/tail gates. **B2 withdrawn/not delivered**, renderer byte-restored B0; raw evidence/tests лишилися.

Analyzer failures retained: початково provenance stderr відхилений до candidate timing/gates; наступний dynamic-wrapper мав порожній PSScriptRoot. Виправлення приймає лише exact eight-key context1 block, перевірений проти stdout. Binary rerun, зміни правил або raw samples не було; фінальний результат — **withdraw_B2**.

## Завершений B3 — останній експеримент, strict rejected

Це був **scope-restricted subset withdrawn P2**, не unrelated mechanism і не продовження його acceptance. Єдина свіжа preregistered кампанія завершилася strict fail; подальших variants/threshold retune/reruns не буде.

Після unchanged per-command static-light write створюється лише stack-local state для `masked`, material-light RGB, `directional`, `flatshade`, `chrome`. Перший actually emitted mesh завжди writes all5; далі тільки exact same property пропускає відповідний setter. RGB порівнюється bitwise через3 uint32 values, включно з signed zero. State не переживає draw_studio invocation, asset/sprite/command/pass/frame/reset/exception. Texture dimensions **завжди** uploaded; program/VAO/texture/blend/transform/opacity/tint/pose calls, mesh selection/order і GL3.3 boundaries не змінюються. B2 metadata cache відсутній.

Read-only source proof охоплює12 installed stock MDL плюс3 texture companions; numeric metadata, SHA/mtime до/після збережено. Для body0/skin0 можливі API-call savings серед6 original material setters:

| Actual stock model | Meshes | Можна не повторювати5 properties | Original6-property calls |
| --- | --- | --- | --- |
| player | 23 | 108 | 138 |
| w_gauss | 6 | 21 | 36 |
| w_battery | 5 | 20 | 30 |
| v_9mmhandgun | 11 | 38 | 66 |
| p_9mmhandgun | 4 | 15 | 24 |

Це структурна можливість з immutable metadata та fixed nonwhite per-instance light, **не виміряний CPU/driver time, heap count або FPS gain**. Adjacent equal material-index shortcut відкинуто: у20 перевірених stock models він давав0; індивідуальні flags/light повторюються навіть між різними material indices. Не створювали favorable asset/scene для pass.

B3 direct output/effective-state controls пройшли; усі50 timing processes завершилися exit0, context/proofs exact,200 timed proof records і25 600 CPU samples збережено. Перевірялися effective-uniform values на кожному draw, unchanged tracked setter stream, binding/draw/pose/color/depth parity; raw changed uniform-call stream закономірно інший. Probe охоплює оголошені entrypoints, не невиміряні всі GL uniforms: effect-light `glUniform4fv` не входить у старий6-entrypoint counter.

Frozen plan: unchanged1920×1080 fixture,96 warm/512 samples,5 interleaved pairs ×5 groups=50 processes, no retries. Baseline-only G=max(run-p50 span,5% median)+.01 ms; primary paired-median gain>G,4/5 positive,3/5>G; controls і median p95/p99 nonregression. У кожному group≥3/5 paired p95 або p99 гірших за-G — blocker. Усі cold/memory/max/tails retained, окремі process runs не pooled.

| B3 group | Median paired p50 gain, ms | Baseline-only G, ms | Strict result |
| --- | --- | --- | --- |
| 16 actors AA0 primary | **0.02150** | **0.08140** | **FAIL** |
| 8 actors AA0 control | 0.00600 | 0.04920 | **FAIL — tails** |
| 16 actors AA8 control | 0.09920 | 0.49720 | control pass |
| 16 actors AA2 control | 0.03900 | 0.21040 | control pass |
| 16 actors AA4 control | -0.02080 | 0.15250 | control pass |

Primary усі5 gains позитивні (`0.0148/0.0215/0.0398/0.0075/0.0328` ms), але **0/5>G**, median gain0.0215<G0.0814. Додатково8-actor control p99 `0.8191→0.8741` ms, +0.0550>G0.0492;3/5 paired p95 погіршилися більше G. Operations reduction/quality pass не скасовують цієї відмови.

Незалежний audit receipt `1d9569…` підтвердив50 exits0, raw hashes, frozen gates перед candidate read та strict withdrawal. Analyzer alias `H`/Get-History і залишений B2 schema guard спричинили попередні fail-before-data; виправлення naming/identity/loop boundary збережені, без зміни формул, raw runs, exclusions чи retries. B3 source повернуто B0; stock source-opportunity proof лишився історичним, не CPU/FPS gain.

## Correctness відновленого baseline та межі результату

Історичний rejected `6E27…` мав exact-cwd shadow-v3 full Release CTest exit0:4264 registered /4189 executed /75 skipped /0 DISABLED/failures. Раніші full Catch: перший exit42 через bind-stat expectation; після історичного P2 test fix streamed exit0,5915 JUnit section/case rows /79 skips. Це не4264 унікальних Catch cases. Усі failures/abort receipts збережені. Після withdrawal/B3 ці passes не називаються final-current-source acceptance.

Debug focused21 gates, fresh core-off17 Studio+32 alternate-module та architecture passed для тодішніх identities. ASan core-off build failed через C4702/C2220 /WX; HL-ON ASan runtime loader `0xC0000142`,0 output/JUnit, correctness not_run. Sanitizer acceptance не закривається звичайними passes.

Final restored-baseline quality перевірено свіжо:

- Release12/12 builds і21/21 focused gates — exit0;259 selected cases,4 optional skips.
- Debug12/12 builds і21/21 focused gates — exit0;259 selected cases,4 optional skips. Це scoped selections, не deduplicated unique coverage. Початковий sandbox C1902 першого target збережений; retry поза sandbox використовував незмінені source/flags і пройшов, не приховавши failed attempt.
- Обидва focused manifests мають `CoreBuildRoot=build` (normal HL build), не module-OFF. Окремо fresh actual module-OFF:17 Studio cases/2717 assertions +32 alternate cases/668 assertions,0 failures/skips.
- Повний canonical Release CTest: **4264 registered/accounted =4189 executed+75 skipped,0 failures, exit0**, unfiltered serial, `2026-10-24T00:23:03.7581559Z→00:30:49.3025945Z`. JUnit SHA `a87689b3b3078b12b167ba458dbbe4b55f9a1e4b0c985564d926c55305de159c`.

Scope caveat: повний registered suite використовує available targets; не кожен standalone target окремо relinked. Свіжі12 focused builds/config, broader full `hlclient_tests` і core-OFF явно перевірені; це не universal clean rebuild усіх EXE. Старі `6E27…` passes не переносяться: вище наведені нові baseline receipts. Нові sanitizer gates restored source — **not_run**; попередні ASan compile/loader limits лишаються відкритими.

Незалежний `final-baseline-quality-audit.json` повторно звірив4264 discovery/JUnit/accounted, відсутність argv pruning,0 failures і незмінний active client. Bounded baseline quality підтверджена, але жодна performance optimization не qualified.

Accepted optimization candidate відсутній, тому **новий normal EXE/package/ZIP не випускаємо**. Active client `7765…` exact до/після full suite, новий client build/delivery=false. Final actual-assets/manual нового accepted candidate — not_run/not_applicable; optimization goal не achieved. Bounded baseline quality і local/dist/ZIP audit завершено; shared current hash verification unavailable. Нового favorable variant не шукаємо.

AGENTS delivery лишається обов’язковою: local `build/bin/Release`, shared `\\192.168.0.104\storage\HLClient-Win10-Test`, `dist/HLClientEngine` і canonical ZIP через audited workflow, rollback/dependency/package metadata та SHA changed binaries між обома installations. Per-PC settings/resource paths/auth/logs/monitor prefs/shared test-data не переносити. Поки acceptance не завершено — **not delivered**. Manual/target-PC/FPS/original-HL parity: not_run/not_established.

## Основні artifact identities

Повні raw samples, усі final comparison cells, tails/cold/memory і hashes: machine-readable [report](evidence/CPU_STUDIO_SUBMISSION_0C29F4CA.json) та referenced analyses.

| Artifact у task-records/cpu-studio-submission-0c29f4ca-20261023 | SHA-256 |
| --- | --- |
| `analysis-P1-final-phase3.json` | `ddec6f5665691b0f90849ae71d140c198af4b16b9bee36d5fdb131c03e1dc489` |
| `analysis-P2-followup-v1.json` | `e45d7dc37ae794028d538d5b021d63d038599cd7b7e1620ab646185e2ec65f36` |
| `analysis-final-actual-assets.json` | `6e5ec23cc9c8f5618b8a7d08670e32a2910ef42d43501501305bb38a34ad7b5e` |
| `P1-rejected-experiment-withdrawal.json` | `e0d8461fb19582df64d3495824768a79ffb3c7542d54393cdcc1e14531d83b8f` |
| `B2-metadata-plan-preregistration.json` | `1472c361e58e62cc69d472c22179bd9abf1b51e24d794921d2f6e0420a7a5796` |
| `B2-metadata-source-operation-proof.json` | `ae7291290dc26d0b2a25f682c89ea36f28f3bd06f975022d6aaa6539991d7c38` |
| `B2-source-proof.json` | `38c5b432a2758f0aa40119d436a50aa91c14f4d65d690282f78405dac173cedf` |
| `B2-initial-build-record.json` | `02c948c3f73b710a86fe9a632ac40dfac050d55a57928f8b68688432a2154d7a` |
| `B2-direct-correctness-analysis.json` | `4f86ae2d96e2ddaa46e2bd2cf408647ad4e566ac33fd78a51312e514bf0a32a2` |
| `B2-metadata-analysis-v2.json` | `c303e4eaa59257eb796978e5614fd16b6f2ec9ff5bac7a7bfb8fbcba98bb48fc` |
| `B2-withdrawal.json` | `21cf5e5f86771ab94af94b59b6903c9f292d2c2c37c22622bcc6eadaaa8e7666` |
| `B3-command-uniform-plan-preregistration.json` | `52c0c8029ac5c8ca77c09b476a002de84cdb1a8a663833d50fa4461295a8137c` |
| `B3-stock-material-uniform-source-proof.json` | `e8133fdd796ec0c485afe1f3ae56148ccaf85cd5f48a028992c6410212a7ddb5` |
| `inspect-stock-material-uniforms.ps1` | `46fef342c644149b6363b35ece74101dee8afa7efed8ef87f6ac7e0da7e00155` |
| `B3-command-uniform-analysis.json` | `1bd194bd32d325d4a2ef7139f50f0dfb65a4f3ab5a94abe2821f5d6290d29e7a` |
| `B3-command-uniform-baseline-gates.json` | `c37ced8ab96c85335ee00c3e3eba2a79c8bbf2cf4a875e26dbb0ad4463008cbb` |
| `B3-command-uniform-analysis-correction-receipt.json` | `1d956934f41c2ec8177b98e200fb7ee57ff27de6d33b69cb0cf8550f4d2beac0` |
| `final-baseline-delivery-observation.json` | `4d4affb0737e56c827f48b9b27b3397ab73a8748c6577f6ff1e2425cc555daf4` |
| `final-baseline-Release-focused-build-results.json` | `87964892e13eb9aa2188afc9411dfe00a86f94324fd88c48aa4bf85545529d69` |
| `final-baseline-Debug-focused-build-results.json` | `275545e32ca2befcf856931bf9d6fd2cb15a16840a9d3ba8e808d80ceb46a5c3` |
| `focused-gates-Release-20261024T001811191Z/focused-gates.json` | `ccd676ce1a5b43f307df11f4693eda4ca81b08f4aa9035579b3243a59f1d50db` |
| `focused-gates-Debug-20261024T002015697Z/focused-gates.json` | `4f4928e07440c26c3ea221dc8af593e531fe1cb07b00240107b5126d98ee23c8` |
| `final-baseline-coreoff-result.json` | `48140b79b048e87a40b73dabc73eb98a4df86d1d67c868906d12d2ba51708f75` |
| `final-baseline-Release-ctest.result.json` | `b8edbf52005d44973379db1f8bf7c1e0a43c6a9d200d15a440b917ba574f6e95` |
| `final-baseline-quality-audit.json` | `117b1097b05de59a5c113b1fb28234c595d2002389314c52111fb8a720ffa210` |

Попередні MD/JSON/checkpoint byte-identical у `report-history/pre-B2-*`, `pre-B3-20261024-checkpoint.*` та `pre-B3-rejection-checkpoint.*`. Stale awaiting-quiet/in_progress_B2/B3 статуси — лише історія. Поточний machine-readable report v3 фіксує rejection всіх кандидатів, відновлений source і пройдені bounded baseline gates, без stable/FPS/new-delivery claim; shared verification і sanitizer/manual limits не приховані.
