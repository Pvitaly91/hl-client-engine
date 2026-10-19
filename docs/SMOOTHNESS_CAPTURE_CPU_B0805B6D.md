# B0805B6D — пріоритетний запис руху та вартість CPU

## Межі результату

Попереднє виправлення selected-slide velocity збережено:
`movement_smoothness_improvement=passed`, `attribution=user_report`.
Повне усунення ривків — `not_verified`. Історичну корекцію 31.56 units
не пояснено і не виправляли припущеннями. Фізика, collision hulls, пороги
корекції, camera lerp, серверна влада та usercmd timestep не змінені.

Нова ручна перевірка збірки: `manual=not_run`. Нові live/Steam/HLDS/capture
сесії агента: **0**. Staging/commit/push: **none**.

| Окремий результат | Статус і точні межі |
| --- | --- |
| `priority_movement_capture_fixed` | `passed_offline` — lifecycle summary, owning failure, priority replacement, manual reservation і flood bounds перевірені. |
| `manual_marker_precontext_extended` | `passed_offline` — ціль 4s до /1s після, actual та encoded coverage окремі; background sampling явно проріджений. |
| `critical_correction_evidence_retained` | `passed_offline_bounded` — top8+recent16, точні producer keys, available/missing та delivery status; unlimited completeness не обіцяється. |
| `frame_cost_root_cause` | `redundant_owning_copies_proven`; причина історичного frame7270 — `not_established`. |
| `frame_cost_fix_status` | `copy_reduction_verified` і нижчі warm medians у контрольованому subset; дорогі frame tails — `improvement_not_established`. Pacing лише уточнено у звіті, не перебудовано. |
| `remaining_movement_issue_status` | `not_verified`; історичні31.56 units — `not_explained`; фінальна manual acceptance — `not_run`. |

Авторитетний checkout — `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`,
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`. Перед правками збережено
1,468 вихідних файлів із перевіркою кожного SHA, поточні EXE/DLL, Git status,
порожній index та окрему копію історичного запису. Наявні незакомічені зміни
інших етапів залишені. Артефакти цього завдання:
`manual-artifacts/task-records/priority-capture-cpu-b0805b6d-20261017`.

## Точний історичний baseline

Run `b0805b6d5a412ed8285fbec350c2f47a`, PID31940,
creation134367150042652094. ZIP SHA-256:
`D7FAEE7CAF7D190EF7D068CB9D41E39C1E2F85AA911497AB02B923CD0C8C4549`.
Original `external-movement.json` SHA-256:
`E634B44B9F487563D343078EFBC63E073504D35C21DB55DF4D0095C5039CFC06`.
Жоден файл історичного run не переписано; `historical-audit.json` — окремий
похідний витяг, не відновлення втрачених даних.

Manifest/config/receipts відповідають client
`F7829D7374522CE96548FA72A362ED75985E6FCBDA959940639F496CB6CE0715`
та launcher
`4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688`.
Endpoint саме цього run — **192.168.0.102:27016**, `maps/crossfire.bsp`,
valve/protocol48/maxclients16. Завершення штатне: exit0,
`application_closed`, identity-bound receipt, transport ACK; підтвердження
сервером не спостерігалось. Власного процесу сервера немає.

Режим: borderless1920×1080, VSync On, фактичне MSAA8 без fallback;
brightness0.1/gamma1.04/contrast1.1, volume41. Частота дисплея та VRR
історично не записані. Поточний launcher.json також збережений окремо;
його адресу чи відеоналаштування не змінювали.

9,059 controlled-alive кадрів; 9,054 придатних інтервалів, середнє
9.633110647ms ≈103.8086 FPS-equivalent. 24 інтервали >20ms, жодного
>33.334ms; max28.2258ms. 9,041 active, 18 очікувань свіжого carrier після
нового життя. `movement_trace_failed` у reason summary=0; один startsolid
не є доказом причини F8. Це **не контрольований A/B із c6d71ecc**.

Три різні ділянки не змішуються:

| Ділянка | Точний факт | Межа висновку |
| --- | --- | --- |
| 92.491645–93.491645s | 90 active controlled-alive frames, max raw31.561639909604008, max sticky camera_jump1.743467 | Не відома пара однієї події. Самого великого correction frame немає серед 1,584 унікальних детальних кадрів; їхній max raw4.679794532. Причина невідома. |
| F8 95.291808s, frame9894 | ordinal7; 94.299128–96.277986s, 188 samples, no omissions; max raw0.359808893 | У marker input/velocity нульові; попередній великий stop→snap тут не повторився. Не доводить відсутності ривка раніше. |
| frame7270, end68.923332s | interval28.2258ms; inclusive update25.9214, parse/commit10.8974, prediction1.9799, Studio3.8493, render1.7518, present0.2472, GPU9.501024ms | Wall-clock scopes вкладені та перекриваються. Це не повний replay checkpoint і не доказ єдиної алгоритмічної причини. |

F8 містить swap-completion intervals13.9839/6.9289/13.8827/6.9611/
13.8039/6.8621ms. Нерівномірність виміряна; scanout і причина VSync/VRR —
не встановлені. Наявні remote observations не доводять дефект конкретного актора.

## Чому губилися потрібні події

Шість automatic windows витратилися до62.938602s: два стартові довгі кадри
та чотири звичайні `local_player_dead`. Старий код не відділяв owning
lifecycle reason від помилки, не замінював завершені малокорисні вікна,
а reservation рахував зайняті вікна загалом. F8 копіював лише1s pre-context;
при512 samples не можна чесно зберегти5s повних кадрів на240Hz.

Окремо producer корекцій мав лише sticky snapshot. Якщо в одному update
приймалися дві корекції, app бачила тільки останню; агрегат сам по собі
не міг надати owning доказ попередньої великої події.

Нові правила:

- Очікувані whitelist lifecycle reasons дають короткі bounded decisions,
  не забираючи movement windows. Failed/active owning state має пріоритет
  над застарілим reason; справжні помилки loading/death не приховуються.
- Manual priority100; failed owner50; alive movement40, інший рух30;
  performance20/10. Дві automatic-reserved-for-manual позиції збережені,
  але всі вісім вікон можуть бути ручними.
- При заповненні замінюється лише завершене automatic window: найнижчий
  priority, потім найстаріший ordinal; manual не перезаписується.
- Coalescing тільки в одному network/map/life і не продовжує fixed deadline
  при flood. Перетворення automatic→manual зберігає старі exact frames;
  якщо їх уже немає в recent ring, відкривається окреме manual window.
- 128 bounded decisions містять kind/time/source/context/reason/target.
  Evictions та пропуски detail явні, не означають відсутності проблеми.

## F8 та budgets

Новий outer schema — `hlclient.external-movement.v2`; session/remote
subschemas залишаються явними v1. Analyzer читає зовнішні v1 і v2;
інші версії отримують точне unsupported-version повідомлення.

F8 запитує **4s pre +1s post** із вже наявної історії. Recent ring961
включає обидва endpoints4s на240Hz. Вісім incident buffers по480 records.
Фон ручного вікна — перший спостережений кадр кожного12.5ms bucket,
автоматичного —8.333ms. Це **проріджений фон, не повний покадровий запис**.
Marker/trigger та independent correction witnesses не проріджуються.

Кожне вікно має requested/actual pre/post, retained count, sampling interval,
omission counts, boundary/partial-close reason. Окремі encoded actual spans
показують, що реально доставлено у файлі при16MiB cap. Analyzer показує саме
доставлене покриття; retained-before-encode не підміняє його.

Actual sizeof: frame1696B, incident header712B, witness904B, decision224B.
Розрахунок всіх detailed backing buffers:
`1696×(961+8×480)+712×8+584×16+224×128+904×24 = 8,207,904B`,
нижче8MiB (запас180,704B). Session aggregates мають незалежний512KiB cap;
remote recorder —1MiB. Runtime checks та точні підсумкові виміри наведені
нижче. Encode cap16MiB збережено.

Першими кодуються critical witnesses, bounded exact marker/trigger prefix,
session maxima, manual/high-priority incidents; останнім — recent background.
Публікація лишається звичайним захищеним write-new після завершення гри.
Off не виділяє capture buffers і не робить I/O/serialization у кадрі.

## Owning correction witnesses

Producer синхронно передає одну fixed-size numeric подію на кожну спробу,
включно з rejection. Key: event ordinal +network/map/life/deaths+source record.
Джерело містить wire origin/velocity окремо від collision-derived rebase seed,
matched state, command/carrier/history boundary, replay suffix, accepted/rejected
result, actual steady timestamps, logical caller time окремо, collision/support
context та freshness. Missing velocity/hull/query не вигадуються нулями.

Вісім найбільших +16 останніх подій дають deduplicated union≤24 witnesses.
Equal maxima детерміновано зберігають первісну identity, зокрема при reset
producer ordinal нового generation. Whole-session/cohort/bin producer maxima
посилаються на точний key зі статусом available/witness_missing/not_measured.
Старі frame-ordinal/sticky camera metrics не перейменовані на exact events.

Same-time camera delta існує лише при справді однаковому selected time та
контексті. Requested time не дорівнює selected endpoint time; clamped samples
позначають different_time/not_measured. Support velocity — retained derived
model, не нове wire observation. Жодного додаткового physics/replay/trace або
presentation sampling заради діагностики не виконується; sink failure не змінює
рух чи commands. Навіть small rejected alive correction перед подальшою death
в тому самому update має власний fallback evidence.

Producer delivery окремо позначена `not_observed`/`complete`/`partial`:
публікації та sink failures знімаються з існуючого producer snapshot,
generation-qualified cumulative counters не додають sticky polls повторно.
Reset/stale generation явно роблять delivery partial; до першого справжнього
спостереження немає вигаданого complete. Це статус доставки producer→recorder,
не обіцянка збереження всіх detail: invalid/evicted/omitted counters лишаються
окремими. Звичайний respawn більше не запускає новий automatic correction
incident від старих sticky ordinal/error того самого producer generation.

## Доведена зайва робота CPU

1. Disposable `EntitySnapshotHistoryBuilder::publish()` копіював всю історію,
   хоча decoder одразу відкидав builder. Новий consuming overload переносить
   власні vectors; non-consuming const& API лишається копіювальним. Усі
   validation/limits/atomic commit збережені, source builder придатний до reuse.
2. Sampled `RuntimeReplayLocalAssets::present_entities` копіював/алокував увесь
   `EntityFollowPresentation`, не читаючи/змінюючи цей staging object, і присвоював
   його назад. Copy/observe/commit тепер лише в authoritative transaction.

Source-derived removed operations для warmed64 snapshots:65 vector allocations
і копіювання4,224/5,120/6,144 entity-state values на publication для2/16/32 actors
плюс64 ordinary entities. Це не всі allocations decoder: staging/insert і
immutable publication shared_ptr залишаються. На sampled materialization
прибрано одну копію239,760B follow pool, його allocation/map-node copies і
зворотне fixed-array assignment; authoritative path не змінено.

Fixture проходить реальні encoded wire→RuntimeReplaySession::apply_record→
authoritative projection→sampled Studio→capture paths. 80 warm commits заповнюють
64-history, потім256 measured samples; actors2/16/32 +64 ordinary, capture On/Off.
Ресурси тільки project-owned synthetic MDL/BSP/sprite, шлейф реально змінюється.
Детерміновані signatures перевірених CPU observation/Studio instance/transform/
bone/trail fields, event counts та malformed rollback перевіряються поза timed
scopes. Це не byte-for-byte порівняння всіх RenderScene fields і не framebuffer/
pixel equivalence. Input preparation/import/encoding
також поза ними.

`owned_update_subset` — сума виміряних послідовних decode/materialization/
collection відрізків, **не повний normal CPU update**. Prediction, renderer,
GPU, present та непокриті стадії ним не виміряні. Історичний frame7270 ним не
відтворено. Відтак оптимізація доводить незалежну зайву роботу, не єдину причину
його25.9214ms і не гарантію FPS.

Контрольна baseline variant використовує фінальну capture policy та ті самі
fixture/Release settings, повертаючи тільки два copy mechanisms до історичної
поведінки; це окремий offline driver, не виданий старий client. Потім exact
optimized source SHA перевірені повторно. Порівняння інтерлівоване, після
warmup, без compiler/CTest/live processes: before/after/after/before/
before/after/after/before. Baseline driver SHA
`8AAA0C6F7CBC10AF97859A5BD9E937FB807FB040870823FFBE5D436F537FB603`.
Сім shared source/fixture identities перевірені; відрізняються тільки два
CPU implementation files. Кожен із восьми запусків пройшов25,527 assertions.
Всі240 JSON rows мають однакові outputs/events/history для відповідного actor
count незалежно від before/after та On/Off. 1,008 events у кожному workload.

Після цих вимірювань фінальний audit додав лише delivery-counter metadata
та заборону повторного sticky correction після life boundary; CPU optimization
і workload input не змінені. Виміряний common-capture before/after лишається
окремим offline експериментом на зафіксованих тоді source hashes, не benchmark
остаточного normal client чи всього його frame pipeline. Фінальні correctness
gates нижче виконані вже після цих reporting правок.

Числа нижче — медіани quantiles чотирьох незалежних повторів, **не quantiles
об’єднаних1,024 samples**. Мікросекунди, before→after:

| Actors | Capture | Parse/commit p50 | Sampled materialization p50 | Owned update subset p50 |
| ---: | :---: | ---: | ---: | ---: |
| 2 | Off | 810.15→663.50 | 596.90→571.45 | 1870.90→1660.05 |
| 2 | On | 779.75→667.50 | 589.65→577.70 | 1857.00→1670.25 |
| 16 | Off | 895.20→805.45 | 756.65→739.35 | 2176.10→2066.20 |
| 16 | On | 898.60→804.35 | 761.80→738.85 | 2191.15→2068.00 |
| 32 | Off | 1086.70→971.05 | 963.40→922.50 | 2701.15→2525.10 |
| 32 | On | 1084.70→975.00 | 971.90→941.15 | 2715.85→2584.35 |

**Дорогі хвости не покращились стабільно.** Capture-On update subset:

| Actors | p95 before→after, µs | p99 before→after, µs | Діапазон max чотирьох повторів before→after, µs |
| ---: | ---: | ---: | ---: |
| 2 | 2516.10→2955.75 | 3922.10→4068.80 | 3378.5–5127.8→2738.7–8018.1 |
| 16 | 2657.40→3721.65 | 3136.80→5112.65 | 3850.3–5562.1→5041.0–7129.9 |
| 32 | 3272.85→4048.70 | 4123.70→6105.35 | 4303.9–5272.6→4062.9–7244.6 |

Вищі хвости є і в незміненому authoritative-materialization control: p99
On16/32 actors906.40→1544.15 /1056.30→1736.15µs; його32-actor p50
618.90→626.65µs. Найкращий after repeat має кращий32-On p99 за всі before,
але діапазон after3466.8–7055.3µs проти before3662.3–4605.3µs. Причину
варіації/хвостів не встановлено; scheduler/allocator/cache effects не виміряні
і не є доведеним поясненням. Не оголошується ні tail improvement, ні причинна
регресія певного алгоритму лише за цими wall-clock числами.

Collection-only paired On-minus-Off p50 після fix:3.85/7.30/10.95µs для2/16/32
actors. Це frame+remote collection fixture, не ізольована ціна кожного
correction callback і не повна вартість normal diagnostics. Whole-subset On/Off
overhead шумний; render/GPU/present цим CPU fixture не вимірюються.
Recorder reserved bytes у workload: Off0, On3,728,296; encoded≈1.219/1.480/
1.754MB. Це не RSS/high-water всього процесу. Фінальний retained-buffer stress:
8,207,904B detailed, encoded10,543,502B, обидва caps витримані.

`perf-summary.json` містить усі scope p50/p95/p99/max та repeat ranges;
SHA-256 `4FA7EB9DC576CB546A433031FB30676883C219CD10AB2C575692A4184F73D07B`.
Сирі `perf-01-before.log`…`perf-08-before.log` збережені.
Підтверджено зменшення owning work і warm medians; **зменшення дорогих кадрів,
причина frame7270 та нова gameplay smoothness — not_established/not_verified**.

## Frame pacing

Поведінка existing fps_max/VSync не змінена: frame-start deadline, sleep лише
якщо він попереду, додатковий yield; timestep незалежний. Нові поля містять
OS-reported current refresh, driver swap interval, requested/effective **software**
cap (0 unlimited), limiter wait і input/render timestamps. Wait у frame record
належить попередній завершеній ітерації, як і відповідна частина swap-to-swap
interval; перша ітерація unknown, не виміряний нуль. Counter scope reset на
початку цього visual run, не попередні attempts/idle. VRR і scanout — unknown.

Нових GPU passes/queries/readback/glFinish не додано: існуючого asynchronous
identity-bound загального GPU timer достатньо для цього вузького CPU fix.
MSAA8/моделі/кров/світло/effects/гравці/серверні rates не знижені.

## Перевірки та видача

Усі наведені фінальні gates виконані після останньої production-правки.

| Gate | Фактичний результат |
| --- | --- |
| Збірки | Win32 warnings-as-errors Debug focused і Release ALL — exit0. Core-only Release та ASan Debug focused — exit0. |
| Focused movement/capture/session/stage | Release і Debug: по140 cases, 535,724 assertions, усі pass. Зокрема нові stale-life/delivery controls. |
| Debug CPU correctness | 2 cases/25,591 assertions, pass; це correctness run, його timings під час інших перевірок не використані як perf evidence. |
| Debug pacing / Null | 1 case/22 assertions, pass; Null завершив1 кадр, exit0. Release equivalents також входять у фінальні gates. |
| Analyzer | 174 checks pass; реальний фінальний v2 encoder→JSON→analyzer roundtrip pass; фактичний старий b0805b6d v1 також читається, вихідний hash незмінний. |
| Core-only | 45 CTest entries, усі pass. Усередині core API: 479 cases, 478 pass/1 explicit original-sky opt-in skip, 416,315 assertions pass. Alternate-module і boundary guard збережені. |
| ASan runtime | Реально виконані movement140 cases/535,724 assertions і effects17 cases/24,518 assertions, без ASan errors. Це scoped runtime, не весь client suite під ASan. |
| Повний фінальний Release | `scripts/build_source.ps1 -Configuration Release -TestOnly -Parallel 2`: exit0. CTest: 3,883 entries, 3,811 pass/72 explicit skips/0 failures; 507.78s. |
| Release post-checks | version/help/Null, loopback self-test transport, functional-log validator, G/H summaries, no-stock manual-wrapper contracts і CheckOnly/SourceOnly — pass; live0. |
| Normal close | Незмінний strict close-verdict gate пройшов за121.46s при первісному timeout180s; reports durability/reconnect contracts не послаблені. |
| Source/state preservation | 1,468 original source files залишилися; 1,442 byte-identical, 26 змінені в межах завдання. Усі26 historical backup files byte-identical originals. Selected-slide physics і його попередні regression files byte-identical preflight. |
| Git/settings | `git diff --check` exit0; HEAD/branch, порожній index і pinned SDK HEAD незмінні. launcher.json byte-identical. Staging/commit/push none. |

72 пропущені CTest entries — справжні skips, зокрема private/installed-original
asset opt-ins, WFP/native capabilities і недоступні environment controls.
Вони не зараховані як executed passes. Внутрішній core API skip також названий
окремо. Normal OpenGL project-owned regressions виконані в повному наборі;
нових renderer passes/queries ця правка не додавала.

Фінальні логи в task-record directory:
`movement-{release,debug}-post-audit.log`, `cpu-debug-post-audit.log`,
`pacing-debug-post-audit.log`, `asan-*-post-audit.log`,
`core-post-audit-{ctest,LastTest}.log`, `analyzer-post-audit.log`,
`production-v2-post-audit{,-analysis}.json`,
`release-full-post-audit{,-LastTest}.log`, `source-preservation-final.json`,
`delivered-binary-manifest.json`. Первісні failures і попередні retries лишилися
поруч, а не замінені фінальними logs.

Готовий normal client: `build/bin/Release/hlclient.exe`.

| Файл | SHA-256 |
| --- | --- |
| `hlclient.exe` | `26DC4D83B62369761EC44AE3DCEEA37E568D45D20A35C0F964065898BADC03F3` |
| `HLClientLauncher.exe` | `4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688` |
| `SDL3.dll` | `AD833F09FAD8D99EADFDB1A765A76F9F20872A3AB6FADBEF648F09334F2FAC87` |

Лаунчер не змінювався за функціональністю: у delivery залишені точні
preflight bytes. Автоматично перелінкований ALL-build variant збережено
окремо як `HLClientLauncher-rebuilt-not-delivered.exe`, не видано замість нього.
Його незмінний entry point (`apps/hlclient_launcher/main.cpp`) визначає normal
client як sibling `hlclient.exe`; новий EXE стоїть саме там. No-stock/launcher
contracts та запуск normal client у Null перевірені; реальне підключення через
GUI не виконувалося відповідно до live0. Ручне приймання — **not_run**.

Незмінний launcher.json SHA:
`D9E75426F6D3B932AC2D9E6802EF0B300F83A08D602981F13A5EEA0055ED2A2E`.
Його адреса та відеопараметри не переписані. SDK HEAD:
`b1b5cf5892918535619b2937bb927e46cb097ba1`.
Skips не зараховуються як executed passes. ASan build/runtime відокремлені.

Початкові failures/retries не приховані:

- `baseline-capture-red.log`: старий recorder — 9 cases, 3 pass/6 fail,
  4,965 assertions/12 fail. Частина failures відтворює capture loss, окрема
  початкова вимога норми30 замість реально30.025 була помилкою fixture і виправлена.
- `history-transfer-red.log`: 6/62 assertions fail у копіювальній реалізації.
- Початкові compile retries: зайва `)` у stage fixture, INFO ternary macro
  у CPU fixture та const diagnostic-receipts pointer у main. Виправлені без
  ослаблення compiler warnings. Build logs/первісний transcript збережені.
- `capture-first-green-attempt.log`: один stale-object fixture failure;
  після перебудови перевірений. `focused-movement-release.log`: 4 legacy cases/
  6 assertions fail; виправлено Off encoded overhead і старі sampling/promotion
  очікування. 4096/8MiB/16MiB bounds не підвищено.
- `core-final-LastTest.log`: 2 legacy remote fixtures/4 assertions fail через
  старі saturation/byte-pressure припущення. Нові fixtures перевіряють replacement
  та реально заповнюють усі буфери, зберігаючи omission і byte-cap assertions.
  Після retry та фінальних правок core-only gates пройдені.
- Analyzer retries: PowerShell parser на типографічному апострофі та shared
  fixture hashtable, який помилково змінював valid identity. Виправлено fixtures,
  а не вимкнено validation; усі початкові `analyzer-v2-*.log` лишаються.
- Початкова окрема ASan спроба без сумісного runtime мала loader
  `0xC0000135`, це **не pass**. Повтор із відповідною x86 MSVC ASan DLL лише
  в PATH дочірнього shell справді виконав тести; глобальний PATH не змінено.

Після видачі: чинний HLClientLauncher, поточна збережена адреса й ті самі
відеоналаштування; checkbox «Діагностика плавності та ліфтів». Кілька хвилин
руху/поворотів/стрільби, ліфт/вихід, deaths/respawns, F8 **після відчутого ривка**,
штатне закриття. Новий flag, швидша реакція чи повний повтор арсеналу не потрібні.
`external-movement.json` лишається у звичайній run directory.

Після нового ручного запису окремо оцінюються movement discontinuities,
дорогі CPU frames і present/GPU irregularity. Доти remaining movement issue —
`not_verified`; historical31.56 — `not_explained`.

## Follow-up 2026-10-17: CPU після ручного run 1b7d438c

Це наступна правка після повідомлення користувача про FPS нижче80; результати
попередньої збірки вище залишаються історичними. Run
`1b7d438cbd1b8799139e0835b5b7162b` завершився штатно, exit0, client SHA
`26DC4D83B62369761EC44AE3DCEEA37E568D45D20A35C0F964065898BADC03F3`.
Controlled-alive:2485 придатних інтервалів, приблизно97.03 FPS-equivalent;
найповільніший повністю alive секундний кошик — приблизно68.85 FPS.
124 інтервали понад20ms,13 понад33.334ms,0 понад50ms; максимум44.2554ms.
Це реальні просадки, а не crash чи доведений повтор stop-snap.

Frame1387:CPU-update36.6483ms при frame42.9274ms; усередині parse10.9173ms
і Studio9.352ms. GPU8.003736ms виміряно окремо. CPU-update inclusive,
wall-clock: вкладені scopes не підсумовуються повторно; це не точний CPU profile.
F8 frame3212: доступне вікно28.131696–33.118503s,350 retained samples,
152 явно пропущених фонових samples і0 пропущених exact witnesses.
У вікні prediction active, presentation interpolated, startsolid/allsolid0,
максимальний raw error0.281187. Це не доводить відсутність movement defects
в усій сесії. Історичний31.56 випадок залишається `not_explained`.

### Production-оптимізації

- `EntitySnapshotHistoryBuilder::insert`: після validation/preflight та
  резервування/копіювання нового snapshot переміщує наявні owning states,
  замість повторного копіювання entity vectors. Початкова transactional копія
  builder залишається. При історії64 усунуто63 retained-vector copies/insert;
  попередні publications, rejection atomicity та всі ліміти збережено.
- Ready-frame receipt читає три scalar counters через noexcept getter, а не
  owning snapshot усієї usercmd history/transmit ranges/server samples.
  Повний getter збережено для diagnostic consumers. Нової per-frame
  серіалізації чи file flush немає.
- Ordinary Studio entities повторно використовують точно однакову pose лише
  всередині одного materialization call. Ключ: immutable model address,
  resource identity/revision, всі raw pose inputs; без quantization.
  Межі64 entries/1MiB accounted pose payload (не total RSS). Помилки не
  кешуються; cap/allocation failure лишає чинний direct result. Player
  pose/gait/p-model, per-instance transform/material/light не змінено.

Фізика, collision/prediction, renderer, StudioPoseEvaluator, input, capture
policy і налаштування користувача незмінні. Не вимикали ефекти/гравців,
не знижували MSAA заради FPS.

### Повторний Release A/B

Збережений попередній та новий test EXE: before/after/after/before/after/before/
before/after, без паралельної компіляції. Той самий warm production workload,
256 samples/scope,64 ordinary entities,2/16/32 actors, capture On/Off.
Усі8 прогонів пройшли25527 assertions кожний. Таблиця — **медіани чотирьох
run percentiles/maxima**, не pooled percentiles; microseconds.
`owned_update_subset` — не повний кадр і не exact historical replay.

| Actors/capture | p50 before→after | p95 before→after | p99 before→after | median maximum before→after |
| --- | --- | --- | --- | --- |
| 2 Off | 2223.05→1477.80 | 3449.60→2476.15 | 5036.50→3371.15 | 6282.30→3943.85 |
| 2 On | 2247.25→1581.70 | 3223.95→2414.70 | 4200.50→2829.05 | 5693.30→3364.80 |
| 16 Off | 2914.35→1940.80 | 4026.25→2771.95 | 4795.50→3204.90 | 5339.15→3891.60 |
| 16 On | 2474.65→2041.60 | 3568.35→2978.10 | 4501.90→3366.30 | 5168.45→3691.70 |
| 32 Off | 3593.00→3256.70 | 4920.45→4331.25 | 5980.70→5521.60 | 7258.80→6307.10 |
| 32 On | 3618.20→3222.75 | 4792.30→4376.95 | 5497.50→4961.75 | 6158.05→6459.05 |

Observed median subset cost нижчий на9.36–33.52%, але tails **не рівномірно
кращі**:32 On subset median-maximum +4.89%, worst maximum8099.8→12615.9us;
32 Off sampled-materialization p99 +15.57%. Причину outliers не встановлено.
Стабільний FPS/усунення всіх просадок не підтверджено. Незмінні capture scopes
також мають outliers; GPU/present/render цим workload не вимірювалися.
Capture collection paired On−Off median p50 before→after:2 actors9.25→8.80us;
16 actors11.15→11.85us;32 actors17.75→18.55us. Run noise не дозволяє
приписати різницю всього subset лише capture.

Signatures перевірюваних CPU outputs (не framebuffer),1008 events у кожному
actors/capture workload, retained history і reserved capture capacity збігаються.
Для2/16/32 actors retained entity-value bytes236544/286720/344064.
Capture capacity:Off0,On3728296 bytes — accounted storage, не process RSS
high-water.64 ordinary poses дають1 evaluation/63 reuses. Encoded diagnostic
sizes не byte-identical, бо містять measured parse/Studio timings:
1218855–1218896,1480602–1480643,1754451–1754500 bytes для2/16/32 actors.

Окремий fake-peer receipt benchmark:4096 samples/4096 ranges/256 history entries;
8 alternating pairs по64 reads. Full getter median batch28,438,100ns проти900ns
scalar:batch-equivalent444.35us проти0.0141us/read, не latency percentiles.
Scalar measurement близький до timer/loop floor. Усунуто1,106,944 logical
array-copy bytes/read (8448 elements); scalar struct12 bytes Win32. Allocator
overhead не враховано; це не приріст всього FPS у такій пропорції.

Артефакти: `manual-artifacts/task-records/cpu-frame-1b7d438c-20261017`.
Initial focused failures збережено: ordinary fixture помилково рекламував
два model slots одним resource; receipt fixture не подав neutral keyboard input,
потрібний для activation. Виправлено fixtures, не ослаблено production contracts.
Final focused Release:4 CPU/history cases7441 assertions і4 receipt cases2724
assertions, усі pass. Allocation-failure fallback перевірено source review,
не injected failures;1MiB boundary також не runtime-injected.

Нові live/Steam/HLDS sessions агентом0; нова збірка manual=`not_run`.
Staging/commit/push none.

### Follow-up correctness і delivery

- Release ALL і focused Debug builds:exit0. Debug CPU/history/workload:
  5 cases/32968 assertions pass. Debug movement/capture/session/stage:
  141 cases/536489 assertions pass. Debug timings не використовувалися
  як performance evidence; під час частини correctness run ішла core build.
- Core-only Release:45 CTest entries pass, включно з alternate-module та
  architecture guard. Внутрішній core API:479 cases/478 pass/1 original-sky
  opt-in skip; цей skip не вважається виконаним pass.
- ASan Debug build:exit0. Реально виконані CPU/history4 cases/7441 assertions
  та movement/capture141 cases/536489 assertions, усі pass, без ASan errors.
  Це scoped runtime, не весь client suite під ASan. Сумісний MSVC14.34 x86
  runtime додано лише до PATH дочірнього shell.
- Незмінність перевірено щодо1457 source files:1445 byte-identical,12 scoped
  changes (11 code/test/header files і цей follow-up). Усі1457 backup bytes
  звіряються з preflight manifest. launcher.json та3 оригінальні run artifacts
  у `preserved-inputs-before.csv` незмінні. Решта історичних файлів не має
  preflight hashes у цьому manifest; їх byte identity тут не заявляється.

- Full final Release CTest:3888 entries,3816 pass/72 explicit skips/0 failures,
  515.46s. Strict normal-close verdict pass за120.59s без підвищення timeout180s.
  Skips не зараховано як executed pass.
- Повний `build_source.ps1 -Configuration Release -TestOnly -Parallel 2` exit0:
  post-checks version/help/Null, loopback transport, functional-log validator,
  G/H summaries, no-stock manual-wrapper contracts і CheckOnly/SourceOnly pass.
  Debug Null також завершив1 кадр, exit0. GUI/server manual run не виконувався.
- `git diff --check` exit0; HEAD `a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`,
  branch `codex/stock-runtime-campaign-5e48b7c1`, порожній index незмінні.
  Pinned SDK HEAD `b1b5cf5892918535619b2937bb927e46cb097ba1` незмінний.

| Delivered file | SHA-256 |
| --- | --- |
| `build/bin/Release/hlclient.exe` | `5301A7AAF3B7A50E62DB0DD53E659455E17118044B9CA272DE1AC876B2DC8C59` |
| `build/bin/Release/HLClientLauncher.exe` | `4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688` |
| `build/bin/Release/SDL3.dll` | `AD833F09FAD8D99EADFDB1A765A76F9F20872A3AB6FADBEF648F09334F2FAC87` |

Лаунчер залишено точними preflight bytes; автоматично перелінкований варіант
збережено як `HLClientLauncher-rebuilt-not-delivered.exe`. Незмінний launcher
визначає executable як sibling `hlclient.exe`, де й розміщено нову збірку.
Launcher settings SHA `CFE8AF777D15988CEF3038DAAFF6F411A2E5CB90560A46A0C3AED91D9144D434`:
це актуальні налаштування перед follow-up, не старий SHA попереднього завдання.

Машинна перевірка: task-local `verify-preservation.ps1` і
`preservation-delivery-final.json`; source rollback у `before/`, binary rollback
у `bin-before/`. `perf-00-before.log`…`perf-07-after.log`, receipt benchmark,
initial failures, final Debug/ASan/core/Release logs збережені окремо.

CPU redundant work=`fixed_and_offline_verified`; real-session FPS tail
improvement=`not_verified`; new build manual=`not_run`. Для приймання — чинний
лаунчер, та сама адреса/графіка, ввімкнена діагностика, кілька хвилин гри,
F8 після відчутної просадки й штатне закриття. Гарантії80/144FPS немає.
