# Windows 10 / MSAA 4× — 50b5ffe1

Наступний вузький CPU-пакет: [CPU history/projection](CPU_HISTORY_PROJECTION_50B5FFE1.md), [докази](evidence/CPU_HISTORY_PROJECTION_50B5FFE1.json). History scan <1% у declared fixtures; один current-time pose-кандидат реалізовано, але відхилено після36 незалежних CPU runs. Production та доставки знову exact actual B0; нової release немає. Історичні GPU-відмови й результати нижче збережено.

Поточний статус після продовження **C1: no_accepted_candidate; оптимізацію не завершено**. Виконано72 нові timed processes /110592 raw frames: P2d і setup-кандидат не встановили виграш понад шум; перероблений MSAA direct resolve дав виграш GPU, але не пройшов11 контрольних регресій. Усі власні production-зміни відкликано до exact B0. Обидві активні збірки, dist і ZIP незмінні; нової доставки немає. Попередні **4190 passed /75 skipped /0 failed належать B0**, а не новим кандидатам. Повний suite у C1 не повторювався без прийнятої зміни. `manual=not_run`, target100FPS **не доведений**, agent live/Steam/HLDS sessions0, staging/commit/push none.

Task records: [win10-msaa4-50b5ffe1-20261024](../manual-artifacts/task-records/win10-msaa4-50b5ffe1-20261024). Машинний запис: [evidence JSON](evidence/WIN10_MSAA4_100FPS_50B5FFE1.json). Нові підсумки — `C1-final-summary.json`, `C1-delivery-observation.json` і розділ C1 нижче. Історичні невдалі спроби та старі результати не переписані.

## Продовження C1 — 24.10.2026

Новий prompt: attachment `9b0f3639-3f02-4580-a3f2-6b31b7ab4336`. Стан відновлено з наявного checkpoint, без нового ingestion чи повторення повного suite. HEAD/main незмінні;754 production files зіставлено з B0 до і після. Попередні CPU merge/cache/metadata експерименти не повернуто.

### Класифікація попередніх результатів

P1 був inactive/operations-only на sRGB GL3.3 output. CPU-AABB P2 заблокований через неповний correctness proof, не через timing. P1b і P2b мають зафіксовані контрольні регресії; їх без змін не повторювали. P2d мав малий позитивний результат усередині шуму без recurring regressions — це `time_gain_not_established`, не доказ відсутності виграшу. P3 не мав заявленої compiled savings: компілятор уже виніс trig за цикл. Усі snapshots і frozen B0/B1 залишилися доступними.

### Умови та нові порівняння

На початку компіляторів локально не було. До кожного run і після нього — CIM inventory; під час — process polling з ціллю100ms, фактичні gaps/query cost у manifest. Загальний найбільший інтервал між спостереженнями —609.05ms (також охоплює межі запусків),12 завершених groups без observed-busy. WMI `Win32_ProcessStartTrace` повернув Access denied; elevation/services/power/Windows settings не змінювалися. Polling може пропустити процес між опитуваннями, тому абсолютної відсутності зовнішнього навантаження не заявлено. Окремий harmless-child probe пройдено.

У всіх порівняннях: фактичні1920×1080, AA4 primary, neutral0/1/1, однакові frozen128warm/1536samples, camera/actors/poses/lights/time, Win32/v143. CL read-tlogs обох fixture variants показують MSVC14.34.31933; PE linker14.34. B0/B1 libraries відрізняються лише renderer. Порядок3pairs: B0/B1, B1/B0, B0/B1. B0-only gates записані до читання B1 timings; формула `max(B0 span,5% B0 median)+0.01ms` не змінювалась. Усі raw rows/quantiles/hardware/operations/pixels/depth перевірені аналізатором.

| Кандидат | Нові процеси / raw frames | Виміряний результат | Рішення |
| --- | --- | --- | --- |
| C1-P2d, той самий shader unroll |6 /9216 | GPU static-world paired-p50 gain0.054028ms; gate0.145418ms;0 recurring flags | time_gain_not_established;54 controls not_run |
| C1-P1c, новий streamlined direct resolve |60 /92160 | resolve gain0.379496ms > gate0.061480; GPU total gain0.394992ms > gate0.227266;11 recurring control regressions | measured_cost_regression_notaccepted |
| C1-B2q, окремий stable-begin fast path |6 /9216 | CPU owned gain0.064600ms < gate0.246400; GPU total gain0.021216ms < gate0.225236;0 recurring flags | operations_only/time_gain_not_established;54 controls not_run |

Ці цифри — median **парних** gains, не різниця двох незалежних medians. Повні CPU/GPU p50/p95/p99/max і memory peaks у кожному `*-campaign-analysis.json`. CPU owned_subset не є повним CPU update гри; GPU elapsed не перетворено на live FPS.

### C1-P1c: що реально реалізовано й чому відкликано

Зміна ізольована до private scene-target header і adjustment-аргумента renderer begin. Збережено matched-format/sRGB correctness попередньої спроби, але legacy fallback/adjusted resolve повернуто до B0-послідовності; direct path має draw-only selection та одне повне фінальне відновлення. Окремий GL hook підтвердив7→4 framebuffer binds у resolve при sRGB Off і On. Це нова обґрунтована зміна, не повтор незмінного відхиленого P1b. Причину всіх старих CPU хвостів не оголошено доведеною.

Correctness:20 cases /1230284 assertions passed, включно з форматами, AA0/2/4/8, custom FBO/MRT, alpha, errors, resize, reset, sRGB та transitions. Окремі old/new operation proofs — по1case/16assertions passed. Перший operation-probe упав через пропущений `initialize_renderer()` перед GLAD-викликом; source/log/exit збережено, нові initfix fixture identities виправили лише harness.

Primary GPU total B0→B1 по repeats:4.380064→3.985072;4.345328→3.988140;4.330248→3.932396ms. Resolve:1.055600→0.676104;1.029600→0.679016;1.018836→0.638352ms. CPU owned:1.4591→1.3369;1.3138→1.3738;1.3158→1.3095ms.

Але secondary4 і AA2 мають recurring CPU p95/p99 regressions; adjusted4 має Studio-prepare p50 gains−0.1592/−0.1634/−0.0991ms, render-submit−0.0681/−0.0634/−0.0450ms і owned−0.2739/−0.2474/−0.1331ms. Загалом11 flags. GPU win не приховує цих витрат; механізм Studio-prepare regression не встановлений. Кандидат збережено у `C1-P1c-frozen`, не доставлено.

Підсумковий analyzer спочатку зупинився на PowerShell typed-variable collision між pair dictionaries і double[] quantiles. Окремий recovery змінив лише назви pair-змінних та перевірив уже записані B0 gates; raw data, thresholds і original plan/analyzer збережені, timing reruns0. Успадковані від P2d описові operation-list/screen поля у P1c plan не є новими правилами: фактичні preregistered `acceptance` і analyzer прямо дозволяли лише output-blit/target-allocation counter differences і вимагали resolve **та** full-GPU benefit. Це metadata-clarification, не послаблення gates; кандидат однаково відхилений11 regressions.

### C1-B2: інша операція, без direct resolve

Побудовано незалежно від B0: пропуск лише зайвого `restore_bindings()` у stable `SceneTarget::begin`, коли renderer без проміжних GL-binding mutations щойно створив Frame. Default API без цього явного proof flag зберіг стару поведінку; reallocation/resize завжди restores. Колір/формат/shaders/resolve залишилися B0. Proof:11 MSAA cases/1229534 assertions і1binding case/26assertions passed; stable begin binds3→1 приAA4 і2→0 приAAOff, changed-caller fallback перевірений. Source snapshot — `C1-B2-frozen`; старий скопійований опис direct-resolve у його generated manifest не описує фактичний B2 delta — authoritative scope тут і в мінімальному B2 plan/source hashes.

Перша B2 спроба заблокована **до будь-якого timed process** о19:20:58Z: MSBuild26724/cl15232, foreign HostX86/x64 compilation. Нікого не завершували. Локальна перевірка о19:23:17Z вже була idle, тому без вимоги нового повідомлення користувача виконано один окремо оголошений B2q resume; первісний blocked0-run manifest збережено, дані не pooled. Нові3pairs завершились exact/0flags, але benefit не перевищив B0 variation. Не називаємо це доведеною відсутністю можливого виграшу.

### Фінальний стан і наступна незакрита операція

Обидві власні production-зміни відкликані;754/754 apps/include/src exact original B0. Renderer Release archive перебудовано з B0 (`C1-final-B0-renderer-build-20261024T192739822Z-e507e2`, exit0). Normal EXE/launcher не перебудовувалися і не замінювалися. Тому full4190/75 не повторювали для заповнення часу. Старі quality failures і нові fixture/analyzer failures збережено окремо від product correctness.

Fresh `C1-delivery-observation.json`: local/shared/dist усі12 binaries exact B0, canonical ZIP незмінний. Client SHA `7765414555a74e4ab5cd3bdf7ddce9da03d998c4ba76659acf43fce92022cd22`, launcher `9d401a751adc933831f86bcd52d6c468fb801981d1af4fb499e44530ce1fb9da`; ZIP `67b9b79ae631b34efb747cbc42f7f144fb6eaab5b1253da190007e9e66515412`. Settings/logs/auth/shared test-data не копіювалися. Rollback baseline збережено.

Наступна конкретна незакрита операція: `RuntimeReplayLocalAssets::present_entities` (`src/app/runtime_replay_local_assets.cpp:1245`) — повторний per-player history/continuity scan з `game.world_entity`, далі `project_entities`. Inclusive `studio_prepare` не розрізняє їх від pose/merge/bounds/publication, тому потрібне окреме вимірювання цих підчастин перед новим cache/production патчем. **Не доведено**, що саме history scan є bottleneck; незмінна network sequence не дозволяє заморозити анімацію. Попередні merge/cache експерименти повторно не впроваджуються.

C1 завершується без прийнятого кандидата, не завершенням оптимізації. Ціль100FPS не досягнуто/не доведено, target-PC timing not_run. Нового ручного тесту незмінного EXE не просимо.

## B0 та збереження поточного стану

Робоча копія `D:/DEV/CPP/HL-Client-engine`, гілка `main`, фактичний HEAD `6a86ce0e3b826341c90bbcee8c603771f1dc58c8`. Поточні незакомічені launcher/test/docs зміни збережені. Це не clean-commit claim. Свіжий B0 frozen о `2026-10-24T13:26:20.8422594Z`: **1319 source/config/test/script files, 124 Release libraries, 4 delivery binaries**. Попередній CPU-task baseline — історія, не підміна цього B0.

| Frozen receipt | SHA-256 |
| --- | --- |
| `baseline/source-manifest.json` | `751d5f881a18404a6cfc1ae4b9773a14f3ae87ddf3fbd520655008c6f9212f3d` |
| `baseline/library-manifest.json` | `b7a9ae116a178bfc604785ce6c11b12d98a03a8f899756be003d95fc543e5f50` |
| `baseline/binary-manifest.json` | `53c79bb9e21e027fb8b96de2c9e971d17ffe58e52790b42856b35575575f7463` |

Manifest paths, sizes, hashes та фактичний dirty status збережені; не виконувались reset/clean/stash/restore/rebase/amend/branch switch. Початкова ingestion-спроба після збереження source/libs зупинилася на неіснуючому guessed helper filename. Завершувальний скрипт не переписав baseline: перевірив уже скопійовані файли та додав фактичні `HLClientSystemDiagnostics.exe` і `SDL3.dll`. Обидві спроби обліковані в intake receipt.

| Окремо хешований активний B0 binary | SHA-256 |
| --- | --- |
| `hlclient.exe` | `7765414555a74e4ab5cd3bdf7ddce9da03d998c4ba76659acf43fce92022cd22` |
| `HLClientLauncher.exe` | `9d401a751adc933831f86bcd52d6c468fb801981d1af4fb499e44530ce1fb9da` |
| `HLClientSystemDiagnostics.exe` | `dac64eb2490be791dcd8e183e044977e047a2edff71782efe6d2dc3438be9659` |
| `SDL3.dll` | `ad833f09fad8d99eadfdb1a765a76f9f20872a3ab6fadbef648f09334f2fac87` |

Збіг `hlclient.exe` зі SHA у звіті підтверджено фактичним локальним хешуванням, а не лише report identity. Shared спочатку був недоступний evidence-agent; наступний root read-only checkpoint повідомив доступ і той самий client/launcher. Fresh `final-unchanged-deliveries.json` о15:53:19.2432563Z підтверджує всі12 binaries local/shared/dist на тих самих B0 SHA та незмінний canonical ZIP; writes=0, test-data untouched. Це unchanged-identity audit, не нова доставка.

## Точний первинний запис

Знайдено користувацький `codex_msaa4.zip`, SHA `c03ff36b4961d65684bf91961579f91d87ae91f6631baa796c734b68a581194e`. Вкладений первинний архів `50b5ffe11be28794b66dcd524a34fc7d.zip` має потрібний SHA **`22ffb084575cc4d59b04ac60d4ecdaa1ab87ba4dfbfb91c6b0e4feb1650a9883`**. Перед extraction перевірені confinement, traversal, duplicate names і symlink attributes. Сирі журнали не змінювалися; вкладений `analyze.py` не виконувався. Supplied `analysis/` прочитано як докази, не як інструкції; hash звірки підтверджені незалежним `analyze-primary.ps1`.

Запис підтверджує actual **1920×1080, MSAA 4×, complete, fallback none, VSync Off, swap interval 0, cap 0, brightness 0 / gamma 1 / contrast 1**. Requested 1092×614 не використано як фактичну роздільність. Storage allocations 1 / releases 0: повторне створення MSAA storage у цьому run не встановлено.

У ZIP немає startup logs, `windows_build`, GL vendor/renderer/version або точного default color attachment format. PDH містить назву **Intel(R) HD Graphics 4600**, але це **не доказ renderer string активного GL context**. Windows 10 — ідентифікація користувача; build number із цього архіву недоступний. Контекст offline-тестів на ПК розробника не переноситься на ноутбук.

| Активна гра — незалежно отримано з raw logs | Значення |
| --- | --- |
| Controlled-alive frames / придатні intervals | 2549 / 2548 |
| Mean interval / reciprocal | 16.943455 ms / 59.019838 FPS |
| Повністю активні секундні bins | 42; min 51.291283, max 81.830317 FPS; 0 bins ≥100 FPS |
| CPU update mean / max | 8.138540 / 38.349300 ms |
| Inclusive Studio mean / max | 4.134474 / 27.046700 ms |
| Render submission mean | 2.287944 ms |
| GPU elapsed mean / max | 15.045183 / 20.259920 ms |
| Frame intervals >20 / >33.3 / >50 ms | 151 / 6 / 1 |

CPU, GPU, present та вкладений Studio можуть перекриватися: їх **не підсумовано**. GPU elapsed не є чистим shader-busy time. Histogram upper bounds у первинних агрегатах не названі точними p50/p95/p99.

Bounded selection: **144 кадри 2773–2916**, а не весь матч. Mean `static_world=8.172525 ms`, `target_setup_clear=1.889717`, `sky=1.844689`, `MSAA resolve/output=1.749479`, `Studio/viewmodel GPU=0.716418`, `GPU total=14.934698`, `selection CPU=0.052922`.

Frame 2875: `prepared_selected`, eye `[-898.762634,54.294888,-1471.996948]`, final angles `[-4.5,18.2,0]`, 48 draws / 6894 triangles, world GPU 10.41976 ms, total GPU 16.22144 ms. Frame 2908: 47 draws / 6637 triangles, setup/clear 3.84336 ms, sky 1.89184 ms. Це camera seeds; повний historical RenderScene, poses/materials/lights із них не відтворювався і не вигадувався.

## Інтеграція попередньої CPU-роботи

[CPU_STUDIO_SUBMISSION_0C29F4CA](CPU_STUDIO_SUBMISSION_0C29F4CA.md) завершився `no_candidate_accepted_source_restored`: експерименти merge/reserve, GL-cache, B2 metadata-plan і B3 command-local setters withdrawn. Їх повторно не впроваджено. Уже прийняті A–D compose/metadata, world/MSAA/GL3.3 зміни та корисні regression tests збережено. Попередні counts/timings/failures — історія, не нові quality passes цього task.

## P1 — перша neutral direct-resolve спроба, не прийнята

Scope: `src/renderer/opengl/opengl_scene_target.hpp`, сумісність caller output, `SceneTarget::begin/resolve`, fallback, bounded output-format observation. Ізольований B0/B1 і snapshots лежать у `P1-frozen` та `isolated-fixtures`.

Перший fixture link **failed**: не було `gauss_world_visibility_control.cpp`, отримано LNK2019/LNK1120 для `RuntimeWorldVisibility`. Лог `isolated-fixtures/B0/msaa4_p1_b0/build.log` збережено. Окремі `_linkfix` targets з необхідним source успішно зібрані; це виправлення складу тестового harness, не прихований timing retry або вилучений невдалий run.

Незалежний actual-context correctness: **16 cases / 1,230,062 assertions passed** (`P1-correctness.log`). Контекст ПК розробника: GL 3.3, default sRGB, rgba bits `[8,8,8,8]`, samples 0; guard повернув `neutral_direct_eligible=false`. Тому fast path у цій кампанії **не працював**, output blit залишався.

Усі **54/54** заплановані interleaved processes завершилися exit 0; 0 skipped/failed timing processes, **0 timing retries**, 10,368 raw samples, 59,430 assertions; exact proof pairs пройшли. Але primary gain gate **FAIL**, recurrent tail warnings збережені. Це не прийнята оптимізація і не доказ FPS gain. Базові noise gates записано до candidate comparison; їх не послаблювали. Повні p50/p95/p99/max, memory observations та парні результати: `P1-campaign-analysis.json` і `P1-baseline-noise-gates.json`.

GL3.3 вимагає сумісності multisample blit, зокрема однакових форматів і розмірів; GL4.4 описує ширше type-compatible перетворення. Version guard не замінює exact color/alpha/state checks. Джерела: [OpenGL 3.3 core §4.3.2, p.227](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf#page=242), [OpenGL 4.4 core §18.3, pp.485–486](https://registry.khronos.org/OpenGL/specs/gl/glspec44.core.pdf#page=507).

## P1b — сумісний sRGB target, rejected / withdrawal

Окрема кандидатна зміна: нейтральний owned MSAA target у `GL_SRGB8_ALPHA8` для доведено сумісного sRGB caller; зміни `neutral_msaa_output_compatible`, `SceneTargetFrame` sRGB state restoration, `SceneTarget::begin/resolve/release`, cache/allocation key та передавання `wants_adjustment` із renderer. Ненейтральний шлях мав зберігати legacy RGBA8 і всі переходи neutral↔adjusted, fallback/custom FBO, alpha/depth/stencil, caller read/draw selection та error semantics. Це історичний scope відхиленого кандидата, **не final accepted function manifest**.

Збережена pre-lazy correctness спроба: **19 cases, 18 passed / 1 failed; 1,230,243 assertions, 1,230,242 passed / 1 failed**. `P1b-prelazy-correctness.log` показує mismatch `same_available_channels(adjusted, draw_to_custom_color(...), output.alpha)` при `srgb_enabled=true`, `test_msaa_e311.cpp:625`. Цей failure не замовчано й не перетворено на pass.

Окремий `P1b-adjusted-diagnostic-20261024T140938213Z-7f72a4` з незалежними B0/B1 binaries уточнив oracle: при sRGB Off обидва мають default/custom hash `12422780832854808869`; при sRGB On обидва мають той самий default hash `16646336782649773805` і той самий, але відмінний від default, custom hash `12218293762972316198`. Отже default-vs-custom різниця вже є у legacy pipeline; B0-vs-B1 **в кожному відповідному pipeline exact**. Кожний diagnostic binary виконав 1 case / 77 assertions. Це виправляє порівняння oracle, а не послаблює pixel tolerance. Після lazy-path зміни окремий `P1b-lazy-correctness.log`: **20 cases / 1,230,284 assertions passed**.

Різниця sRGB conversion при записі та blit має окрему семантику, тому byte equality перевіряється проти коректного legacy oracle, а не припускається з назви формату. Джерело: [OpenGL 4.4 core §17.3.9 і §18.3](https://registry.khronos.org/OpenGL/specs/gl/glspec44.core.pdf#page=482).

Первинна P1b кампанія — `P1b_notaccepted`, 9 recurring scope flags; незалежний `P1b-independent-tail-audit` зберіг rejection. Sparse p99 observations не пояснюють AA8 p95 increases та submit-p99 failures у всіх 3 pairs. Attribution не вигадувалася.

Один заздалегідь обмежений довший corroboration завершився **`long_corroboration_failed_notaccepted`**: 78 processes / 119,808 raw samples, exact outputs у Long і B0/B0 Null datasets. Long має **18 recurring flags**, Null — 3; Null шум не скасовує кандидатні failures. Gates залишилися unchanged; runs не pooled, adverse samples не відкинуті. Повні quantiles/max/raw hashes: `P1b-world-corroboration-analysis.json`.

| Long whole-owned CPU scope | Paired B0−B1 gains, ms | Baseline-only gate, ms | Погіршені pairs понад gate |
| --- | --- | --- | --- |
| one4 p50 | −0.1860 / −0.3044 / +0.0290 | 0.07671 | 2/3 |
| one4 p99 | −1.0356 / −1.2817 / −0.4531 | 0.32510 | 3/3 |
| many4 p95 | −0.5754 / −0.5866 / +0.1555 | 0.17320 | 2/3 |
| many4 p99 | −0.9686 / −1.0193 / +0.6116 | 0.51050 | 2/3 |

Рішення: **P1b rejected, withdrawn, не доставляється**. Навіть локальний resolve gain не скасовує full-owned/control regressions. Root повернув P1b header/tests і renderer callsite до B0 перед незалежною P2 фазою. Read-only SHA-256 перевірка підтвердила exact B0 `opengl_scene_target.hpp=de22ec4855b422696594ca83e8b2e8e51c2762cfc695386f9778f42047f28d7a`, `test_msaa_e311.cpp=c2f1916efd88da9f7e09baad59f0faeebe35406dd005f132fb7c727ec1d2166a`. Frozen attempts/evidence залишаються. Фінальний renderer source exact B0 підтверджено нижче; додаткових P1b performance passes не заявлено. Уже зібрані renderer-dependent test EXE потребують relink із фінальною renderer library: саме відновлення source не очищує їх від старого P1b object code.

## P2 — static world / setup-clear / sky: кандидати завершено без прийняття

Таймер `target_setup_clear` починається раніше за `glClear`: охоплює відповідну preparation/validation, target allocation/cache decisions, state setup та clear. Тому його 1.890 ms не прирівняно до вартості одного очищення. Sky — реальний view-dependent pass; необхідне небо або очищення не видалялися. Доведено зайвого clear на цьому checkpoint немає; GPU timer boundaries не пересувалися.

Підготовлений початковий `candidate-P2.patch` / `P2-PREAPPLY-REVIEW.txt` пропонував CPU-AABB proof для prepared-world zero-light routing до наявного no-light shader. Незалежний review **заблокував цей proof до build/measurement**: за MSAA interpolation без `centroid` може оцінювати fragment inputs поза primitive bounds, отже geometric AABB alone не доводить нульовий внесок на всіх реально оцінених fragments. Жодних вимірювань або прийняття цього CPU-AABB кандидата не заявлено. Near/tangent guard не усуває цю загальну прогалину доказу. Подальші P2b/P2c/P2d кандидати обліковані нижче; жодного не прийнято. Камера, геометрія, освітлення та animation cadence не змінюються.

P2b/P2c/P2d proof, primary/control campaigns та accept/reject receipts наведено нижче. Actual-assets variants використовували однаковий `HLCLIENT_71B97_VISIBILITY=all` для production prepared selection. Після rejection production renderer source відновлено exact B0; final baseline quality пройдено окремо, це не нове кандидатне вимірювання.

### P2b — окремий shader-local guard, correctness passed / performance rejected

Наступний кандидат обмежує перевірку нульового light contribution самим fragment shader, не CPU geometric AABB; виходить з original B0 і **не включає відхилені P1/P1b**. Окремий proof `P2b-proof-20261024T150058728Z-b5fd5d`: кожен із незалежних B0/B1 binaries виконав **1 case / 6973 assertions**, по **512 exact color+depth pairs**, actual AA Off/2×/4×/8×. Жодного non-exact pair у raw stdout. Це correctness proof на тестовому контексті, не performance acceptance.

Зафіксований до вимірювань `P2b-campaign-plan.json`: 10 groups ×3 paired repeats ×2 variants = **60 processes**, 128 warm frames і 1536 recorded samples на process. Primary — one-light `primary4` GPU static-world; controls none/far/many light, second camera, Off/2×/8×, adjusted4, diagnosticsOff4. Gates використовують власний B0 spread кожного scope/quantile; far-only gain не рятує primary failure. Full GPU, owned CPU, усі operation counters та exact controls обов'язкові.

Фінальна `P2b-campaign-analysis.json`, SHA **`91bee56db2cc093ba79678a3a02e6d2c62fc93653c9dac930d8e26f205f0c2b4`**: 60/60 processes passed, **494484 assertions /92160 raw samples**, exact controls і required operations пройдено, retries=0. Але primary gain **−1.9983079433 ms** проти gate **0.1448776 ms** не пройдено; **89 recurring scope-tail flags**. Результат `P2b_notaccepted`, accepted=false. P2b withdrawn source збережено окремо, frozen binaries/усі measurements незмінні. Evidence agent не запускав паралельних builds/tests.

### P2c — literal single-effect specialization, proof passed / R1 primary rejected

Кандидат P2c спеціалізував лише opaque-lit `effect_count==1` literal index0; `else` залишав original loop. Чотири B0/B1 performance/proof binaries зібрано й заморожено. `P2c-proof-20261024T151649537Z-3d9399`: кожен variant **1 case /6973 assertions**, 512 exact pairs AA0/2/4/8; source interception B0 `compiled_sources=0`, B1 `compiled_sources=32`. Історична P2c renderer library **не прийнята**; наступна робота P2d обліковується окремо.

Кампанія session5214 стартувала о15:16 UTC. `P2c-primary4-20261024T151653-5d14a1` завершена guard failure о **15:17:13.0814264Z** після одного B0/B1 pair, обидва processes exit0. Причина manifest: `Concurrent build/test/game exists; no process will be terminated`. Manifest SHA **`77e835d61e8579c5ce3bce1005aa8b69946c295ae26ceec8e9f4ee318077a372`**; обидва raw runs збережено. Це **incomplete campaign / orchestration interruption**, не доказ performance regression і не acceptance. Конкретний interfering process на цьому checkpoint не встановлений. Evidence agent у цей час виконував лише file reads/пошук, не CTest discovery чи compiled programs. Будь-яка наступна контрольована спроба потребує окремого рішення root, без переписування цього failure або прихованого pooling.

Root окремо дозволив **один bounded R1 orchestration restart** з тими самими frozen EXE/harness128warm/1536samples і settings. Початкові2 logs збережені та виключені з R1 metrics через перервану three-pair group, а не через значення timing. R1 спочатку виконує3 нові primary pairs: unchanged primary-gate failure одразу відхиляє кандидат, решта9groups `not_run`; pass дозволяє лише продовжити controls. Acceptance однаково потребує всіх60 processes та первісних gates. Подальшого автоматичного retry/pooling немає; фактичний підсумок наведено нижче.

R1 завершився **`P2c_restart1_primary_failed_notaccepted`**: **6 processes /50442 assertions /9216 raw samples**, exact controls пройдено; primary gain **−0.1069118976593 ms** проти gate **0.14213200569 ms**, **5 recurring tail flags**. Решта **54 processes `not_run`** за попередньо визначеним early-reject правилом. Це не повний control campaign pass. `P2c-restart1-campaign-analysis.json` SHA **`7bbfa7e844065c335a02c1a75ba5eebf4045bffb8810e47b9cdf81157c4578e0`**. Початковий interrupted pair залишається окремим, не доданий до R1 metrics.

### P2d — proof/review passed, bounded R1 primary rejected

Окремий full nested uniform-loop unroll зібрано. `P2d-proof-20261024T153948561Z-c00218`: кожен незалежний B0/B1 має **1 case /7621 assertions**, **560 exact unique color/resolved-depth pairs**, actual AA0/2/4/8,35 states ×4 layouts. B0 shader-source probe=0, B1=32;280 program receipts на variant збігаються. Незалежний `P2d-independent-source-review.json`, SHA **`f3b1b4c4800b94a9abf1b33b9bdaab88b9481fd72ac9e0e6271e7d55e9623067`**, підтвердив повну mechanical equivalence opaque-lit shader ordered expansion, відсутність інших production змін і збереження попередніх32 numeric states. Додано4/5/7-light controls. Це proof на test-host GL3.3, не performance claim і не target-GPU proof.

`P2d-primary4-20261024T153954-1847be`: тільки B0-r1 завершився exit0. Host guard **15:40:04.4181063Z** зафіксував **MSBuild.exe PID12736** і **cl.exe PID15560**, після чого зупинив кампанію; процеси не завершував. Manifest SHA **`bd6a5462a4b6d40066b845f569f77da15870e773cd5375687b21750521f301eb`**. Єдиний baseline run та всі logs збережено; candidate timing або complete pair відсутні. Це **confirmed external-compilation interruption, не performance rejection**. Пізніше root виконав один bounded restart1 із тими самими frozen inputs; його підсумок наведено нижче. Workflow зберігає primary-first reject-only gate, original B0 та всі quality/whole-owned/control constraints без послаблення порогів чи якості.

P2d R1 завершено **primary reject**: **6 processes /50442 assertions /9216 raw samples**, exact controls, **0 recurring tail flags**. Gain **+0.041964054107666 ms** менший за baseline gate **0.1418303942680359 ms** — позитивне мале число не доводить виграш понад шум. Решта **54 processes not_run** за predeclared early-reject правилом; це не full-control pass. `P2d-restart1-campaign-analysis.json` SHA **`bf58f9799ab386934e74a7b3479d80f870565a16650b4bf04a455ec42e873ac7`**. Original unmatched B0 run залишається збереженим і окремим. Поле immutable analysis `excluded_candidate_processes=1` має неточну назву: фактично виключений один incomplete **baseline B0** process, без B1; analysis не переписано.

### Остаточне рішення щодо оптимізації

**`no_accepted_candidate`; optimization objective not achieved.** Усі bounded кандидати завершилися rejection/no-benefit або proof block. P2d не показав recurring regression, але не пройшов обов'язковий benefit gate, тому також не приймається. Нових кандидатів, змінених порогів або FPS claims немає.

Root відкликав лише власні production renderer зміни до current-task B0. `opengl_renderer.cpp` SHA **`e477a6ac45ddcb06e3a9841966e373048b620a18897a1c2d1c0712bf2507ca33`** exact B0. Відхилений P2d збережено у `P2d-withdrawn-source/manifest.json`, SHA **`d587ba71859736403747d167dfd7c250e2333440cedf41f141140b954e89d0f4`**. Preexisting dirty changes, корисні regression tests та всі receipts збережено. Final Release21-project relink і library identity зафіксовано; focused/core та canonical Release runtime пройдено нижче.

Active local/shared client/launcher не оновлюються відхиленими binaries. Нова optimized build/доставка/dist ZIP **не створюються як результат цієї оптимізації**. Fresh final audit підтвердив незмінні active/package identities: `final-unchanged-deliveries.json` SHA **`b3606a3f93233617b683cc0fdafa5b105c7dc245620707fac776ef87d20150a0`**,12 files, writes0. Усі виконані quality gates і skips зафіксовано, історичний core Debug missing-library failure збережено разом із наступним successful dependency build. Target100FPS unproven, manual not_run.

## P3 — CPU Studio bounds, withdrawn за compiled proof

`studio_ms` включає `present_entities` та viewmodel handle/materialization: history/continuity, interpolation, projection, poses/merge/sockets/bounds/light/frame publication і diagnostic producers. Це **не тільки skeletal math**. У frame660 CPU 38.3493 / Studio 27.0467 / parse-commit 4.8255 / submission 3.7005 / GPU 11.24856 ms не встановлюють конкретну дороговартісну підфункцію.

Було ізольовано source-level винесення шести sin/cos із восьми кутів у `transform_entity_render_bounds`/internal `transformed_bounds`. Але disassembly фактичної frozen B0 Release library показав: **MSVC вже виніс усі 6 sin/cos перед циклом**. B0 calls `0x73,0x85,0x97,0xA6,0xB5,0xC7`, loop `0x131`, backedge `0x3D1→0x131`; кандидат також має шість pre-loop calls. Отже у compiled B0 немає 48→6 runtime savings. Кандидат withdrawn, новий timing benchmark не запускався.

Production `src/entity_render/entity_render_frame_composer.cpp` відновлено точно до B0 SHA `62e4a05de3193a6be698ed6e2ab5341f29076ac29278a2482c2ee2780fb59cb7`. Attempt source SHA `06a79d01b5b180dc9f5cac1ac9419905171e96c3e5b0953e227d385fa05d8947` і обидва disassembly збережені. Private candidate library `b82880aa5f97f29a8cf68180f6990bf21669ef00b6ebd516fbbc76f3ed6448f6` **не можна лінкувати або доставляти**.

Корисний test `[50b5-bounds]` залишено: independent original-math bitwise/rejection oracle для 600 edge cases, 4096 fixed-seed inputs та 18 invalid/overflow inputs. Новий bounds oracle пройшов в обох final focused Release/Debug (по1 case). Не застосовано нових persistent caches, animation freezes або повторення відхилених CPU-варіантів.

## Quality / збірка / доставка — фінальні receipts

О16:06:25Z зафіксовано нове повідомлення root про відповідь користувача: **інша компіляція ще триває**. Це час запису повідомлення, не встановлений точний час початку сторонньої роботи. R1 pre-run guards повідомляли idle, однак точний overlap із P2d R1 о15:49–15:50 **не встановлений**; чисте quiet-window final performance не доведено. Root зупинив подальші performance measurements, але продовжує correctness checks. Незмінні campaign/gates не пройдено; це не доказ універсальної регресії або target-GPU поведінки. Повторний timing потребує нового підтвердження завершення іншої збірки й окремо оголошеного receipt; bounded R1 вже використано, автоматичного retry немає. No accepted candidate/no delivery залишаються чинними.

Фінальний Release plan SHA **`20e81e8656a8d21d72095692b11023fefa9511c3918244f36904475a4d240c10`** підтверджує21-project build passed. `production-after.json`: **754/754 production files exact B0**, changed_paths=[]; preexisting launcher збережено. Перебудована Release renderer library SHA **`b859aef5186ee222f038b65bb984e4ba7c777c1fda276cc937c2ed3e654d4437`**; це новий build receipt відновленого source, не вимога двійкової тотожності старій `.lib`. `quality-source-manifest-20261024T160019044Z-7833bd` фіксує3 restored production paths. Main Debug137-project build `quality-build-Debug-20261024T155818741Z-3a72d8` і core Debug86-project build `quality-build-Debug-20261024T160304496Z-bee0d6` **passed**. Перед core build Catch2/Catch2WithMain prerequisites у `quality-core-debug-catch-prerequisites-20261024T160141572Z-6764b3` обидва завершилися exit0. Попередній LNK1104 missing-library failure вирішено свіжою dependency build, оригінальний failure збережено. Focused Debug/Release, named core runtime та final canonical suite згодом passed (нижче), із явно облікованими skips.

Підготовлені task-local scripts у `QUALITY_RUN_COMMANDS.txt`; їх підготовка й parse-check не є запуском tests/build. Fresh counts не замінено старими. Скрипти бережуть 4 active B0 binary SHA, `/m:1 /nr:false`, Win32 toolset14.34.31933, окремі logs/attempts; normal client будується тільки у private candidate tree, launcher/helper не перебудовуються. CTest shadow перевіряє точні команди/properties/original cwd; не hardcode-ить історичну кількість тестів.

Дві початкові root orchestration-спроби quality Release зупинилися **до фактичної компіляції**: перша через XPath, який захопив `ItemDefinitionGroup/ProjectReference` без `Include`, друга через guard на неявний reference до normal client. Виправлено вибір лише `ItemGroup/ProjectReference[@Include]` з явною перевіркою XML-атрибута; неявні delivery-EXE dependencies тепер обліковуються як existing/not built, explicit target залишається забороненим. Обидва attempt directories збережені; їх не видаємо за успішні builds. Наступний initial 137-project Release build пройдено; фінальний21-project build після withdrawal обліковано окремо.

Початковий широкий Release build `quality-build-Release-20261024T141423790Z-ee9d24` успішно завершив 137 projects. Це не final post-withdrawal binary acceptance і не повний CTest pass. Для наступних Debug/final incremental builds передбачено explicit `-CurrentTaskChangedLibrariesOnly`: повний apps/include/src SHA audit до/після, лише два renderer files можуть відрізнятися від цього B0, composer exact; 11 focused build targets або один core-only target, решта existing/not built.

Для final Release додається `-FinalRendererConsumers`: **21 project** — renderer library, 19 test executables та CLI-tested entity viewer. Dependency audit встановив 17 renderer-linked test EXE, з яких focused plan покривав лише 8. Додаткові дев'ять: console, early_signon, gauss_71b97, installation_check, movement_c6, public_server_control, resource_transition, steam_diagnostics, viewmodel; entity viewer потрібний для 13 зареєстрованих CLI gates. `quality-build-Release-20261024T155224404Z-86ed5a`: усі21 projects **build passed** після withdrawal. Native launcher/helper/active client не перебудовувалися; final canonical offline CTest має використовувати чинний baseline client, не відхилений private candidate. Це build receipt, не runtime pass.

Full offline runner додатково звірено з authoritative test opt-ins: `HLCLIENT_E31_STEAM_RUNTIME` unset; `HLCLIENT_RUN_WFP_CAPABILITY_TEST=0` не дозволяє inherited opt-in увімкнути реальні elevated WFP canaries. Optional Studio/P3 benchmarks теж disabled, required correctness assertions залишаються. Усі overrides process-local із відновленням у `finally`; попередні значення не логуються. Це підготовлений guard, не новий test pass і не зміна Steam/system settings. Installed-asset read-only controls і окремі actual-assets campaigns не названі live sessions.

Поки performance campaigns були зупинені, core-only Release incremental build `quality-build-Release-20261024T152240400Z-ebc0a8` **passed**. Наступний core-only Debug `quality-build-Debug-20261024T152250330Z-c7dcf4` **failed at LINK**: LNK1104, відсутній `../../lib/Debug/hlclient_console.lib`. Це missing Debug dependency binary, не встановлений C++ compile/source defect; attempt збережено, Debug runtime після цього build не оголошений passed. Окремий Release run `quality-core-Release-20261024T152716472Z-94f83b`: **2/2 CTest passed, 0 skipped/failed**, точні gates `hlclient_core_api_offline` і `hlclient_game_boundary_guard`. Це актуальні Release core/architecture результати, не повний final suite.

| Gate | Стан цього checkpoint |
| --- | --- |
| Final focused Debug / Release | кожен33 gates,339 executed,5 optional skipped,0 failures |
| Final MSAA/opaque/GL3.3 exact controls | focused Debug/Release та canonical full suite passed |
| Actual-assets / synthetic candidate campaigns | завершені/відхилені; 54 controls для P2c/P2d R1 not_run за early-reject policy |
| Core-only Release + architecture | current build passed; 2/2 CTest passed |
| Core-only Debug | fresh prerequisites +86-project build passed; окремі2 API/architecture gates passed,0 skips/failures |
| Null / alternate-module / launcher-preflight | final canonical suite passed; required focused preflight passed |
| Fresh ASan configure / build / runtime | initial configure failed; short retry configure passed, build log succeeded but wrapper interrupted / separate runtime-only passed |
| Final `git diff --check` | exit0; main HEAD незмінний, staging/commit/push none |
| Final Release offline CTest | **passed:4265 registered,4190 executed,75 skipped,0 failures**; fresh names/count exact |
| Final B0 renderer / consumers |754 production files exact B0; Release21/Debug137/core86 builds і final runtime passed |
| Local / shared / dist / canonical ZIP | fresh unchanged B0 audit passed:12 files, canonical ZIP unchanged, writes0 |
| Новий користувацький Win10 MSAA4 тест | not_run |

Final focused Release `quality-focused-Release-20261024T161448960Z-358349` та Debug `quality-focused-Debug-20261024T161517241Z-a893f7`: **кожен33 gates /339 executed /5 skipped /0 failures**. Skips лише optional installed MDL/WAV, HUD coverage, menu WAV, SPR HUD та Steam-runtime inspection; не названі виконаними tests. Required launcher-preflight passed. Applications у focused runner пропущені навмисно, їх покриває canonical normal-tree full suite.

Помилковий broad core-OFF run `quality-core-complete-Debug-20261024T161747685Z-5d616f` збережено:45 registered/accounted,12 skipped,29 failures,4 passed. Runner вибрав усе дерево, включно з **незібраними tool/app EXE**, замість первісно запланованих двох core API/architecture gates. Це orchestration selection failure, не встановлена source-регресія. Окремий свіжий `quality-core-api-architecture-Debug-20261024T161920641Z-51a03f`: **2/2 passed**,0 skips/failures. Assertions не змінювалися. Canonical normal-tree Release `quality-final-baseline-Release-20261024T161937175Z-30c810` завершився **passed:4265 registered,4190 executed,75 skipped,0 failures,exit0**. Fresh discovery і JUnit мають точний збіг names/count. Skipped75 не названі виконаними; їх імена та capability/opt-in причини залишаються у JUnit.

Перший final focused Release `quality-focused-Release-20261024T160828200Z-9c7f8a` завершився orchestration failure в `audio-weapon`:15 testcases,1 failure optional installed-MDL read `test_local_weapon_audio.cpp:78`. Root окремими child-process probes довів, що PowerShell `.NET SetEnvironmentVariable(...,$null,Process)` залишав presence=true,length0, тоді як `[NullString]::Value` видаляє ключ (presence=false). Presence-only `SDL_getenv` через порожній ключ помилково активував optional asset read. Це не встановлена клієнтська регресія; audio-device opt-in не виконувався, full suite ще не стартував. Обидва probes і failure збережено; helper `Set-QualityEnvironment` виправляє лише process-local apply/restore. Root session67093 згодом завершився окремою orchestration failure, облікованою нижче; performance повторів не було.

Correctness retry R1 `quality-focused-Release-20261024T161251547Z-1d674c`:31 gates passed, зокрема nullstring fix спрацював (optional audio asset skipped). Далі wrapper впав **до запуску check-only process**: `Get-Command pwsh` повернув два executable paths, які потрапили в scalar filename через newline. Script-only fix вибирає перший command; assertions/production не послаблено. Root session21357 згодом отримав final focused Release/Debug passes і named core pass (вище); обидва попередні failures збережено. Це були correctness orchestration retries, не performance retries; final canonical suite також згодом passed.

Перший ASan attempt `quality-asan-core-Debug-20261024T143759786Z-6fcea4`: **configure failed**, build/runtime **not_run**. CMake C-compiler try_compile викликає pinned CL19.34, але MSBuild завершується `MSB6003 / System.IO.DirectoryNotFoundException` у `TrackedDependencies.ExpandWildcards` для `cmTC_78f62.tlog`. Надрукований directory path має 223 символи; сам лог не доводить точний Windows path-limit threshold. Failure збережено; це не результат sanitizer-перевірки програми.

Один orchestration retry у короткому **новому** `D:/DEV/CPP/HL-Client-engine/build-asan-50b5-core`: configure **passed**. `quality-asan-core-Debug-20261024T144411870Z-1d932c/asan-build.log` закінчився `Build succeeded`, 0 warnings / 0 errors, executable створений. Проте wrapper залишився в `Start-Process -Wait`; після перевірки відсутності MSBuild/CTest/ASan child root перервав лише власний wrapper. Його exit=1, фактичний MSBuild exit code wrapper не зберіг: **log-success не підмінює process-exit receipt**. Початковий attempt і його stale running summary залишено незмінними як interrupted/orchestration evidence.

Окремий runtime-only attempt **`quality-asan-core-runtime-resume-Debug-20261024T150103329Z-4c3cb3` passed_runtime_only**, без rebuild: той самий core EXE та matched ASan runtime, CTest timeout 120 s, джерельний build exit code `null`. CTest: **1/1 passed, 0 skipped/failed**. Вкладений Catch: **535 cases =534 passed +1 skipped; 670593 assertions passed**. Skip — явний optional installed-sky audit (`test_sky_assets.cpp:73`, `HLCLIENT_SKY_ASSET_ROOT` не задано); він не названий виконаним тестом. Inner count підтверджено `build-asan-50b5-core/Testing/Temporary/LastTest.log`, SHA `5cd9d5f3b06ea66ac7a90ae683f913ddf6f2f8a5d7b57f05af24bb6f6a469d7e`; CTest JUnit обрізає stdout до 1024 bytes і сам не містить підсумкового Catch count. EXE SHA до/після `a4b7e8bb26219aa9122b0bf468f15d277ec125010c483ca203b04d5ea0974379`; active чотири binaries незмінні.

Future helper замінює `Start-Process -Wait` на owned `Process.WaitForExit()` і чекає redirected handlers перед hashes. Microsoft документує, що [-Wait очікує також descendants](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/start-process#-wait), а [WaitForExit завершує очікування конкретного process та async output handlers](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.process.waitforexit). Це усуває надмірне tree-wait очікування в майбутніх спробах, але точна причина вже перерваного wrapper не доведена. Runtime pass не переписує interrupted build-wrapper як passed. Compiler/sanitizer/warning-policy не послаблено; старі C4702/loader0xC0000142 не видаються за новий результат.

Нової binary доставки в цьому task немає: жоден кандидат не прийнятий. Fresh final SHA audit підтвердив active local/shared та dist на чинній baseline. Canonical ZIP SHA **`67b9b79ae631b34efb747cbc42f7f144fb6eaab5b1253da190007e9e66515412`**,4183966 bytes, також незмінний. Settings/resources paths/logs/auth/shared `test-data` не змінюються. Відхилені candidate binaries не можна пакувати або підміняти ними активні програми.

Final runtime summary SHA **`1b36746087650557399c451b2d108b70cc255cb7ead075d8f4a3c3eddbe1597f`**, discovery **`0a73737009c49e2ef7fa9b85f88908927d92367bf334d2d7e22d9ec748d738bf`**, JUnit **`bf61911b20623977702a55b52a928a39030d57d1ba55cfcc3455faffc80468ca`**. `final-runtime-verification.json` підтвердив names/count, source unchanged та відсутність нової доставки; environment_restored=true, active B0 unchanged. `final-git-readonly-check.json`: diff_check exit0, main/HEAD незмінні. Після quality виконано ще один fresh audit: `final-deliveries-after-quality.json` SHA **`a4b29a2a451d695619cd7bc4b610a255f82c7115a5c1bda564c70682b3a55f35`**, усі12 local/shared/dist files та canonical ZIP незмінні, writes0, settings/test-data untouched.

## Межі висновку

Історичний stop першого циклу очікував підтвердження «завершилась». Новий prompt і C1 вище це замінили: стан перевірявся локально, B2q стартував після виявленого завершення компіляції без нового повідомлення користувача. Дані старого bounded R1 не переписані й не pooled. Guard тепер опитує процеси під час runs, із чесно зазначеним polling limitation. Optimization objective усе ще не досягнуто.

Немає зниження resolution/MSAA/detail/числа моделей/light/effects, частоти анімації, зміни FPS counter, prediction/physics/weapons/damage/command time чи прихованого вимкнення діагностики. Немає змін Steam/server/bots/auth/rates/driver/power/system settings або remote execution. Readback — offline correctness, поза timed region.

59.02→100 FPS означає приблизно 41% скорочення інтервалу; усі 1.75 ms MSAA scope не названі зайвими, оскільки необхідний resolve залишається. Hidden/no-swap результати на іншому ПК **не є live FPS** і не доводять 100 FPS на Windows 10. Оскільки нової optimized binary немає, нового manual test для цієї відхиленої серії не вимагаємо. `manual=not_run`, `target_100fps=unproven`; майбутній target-PC тест має сенс лише після окремого прийнятого виправлення.
