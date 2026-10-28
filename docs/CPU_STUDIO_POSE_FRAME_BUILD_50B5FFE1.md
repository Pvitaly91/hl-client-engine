# Studio pose / EntityRenderFrame — 50b5ffe1

## Поточне приймання — 25.10.2026

**Поточний CPU-кандидат прийнято й доставлено на обидва ПК.** Завершено відсутні Debug/Release, ASan, core-only/alternate, OpenGL/Null, AA0/2/4/8, launcher/preflight та повний Release suite. Повторних timed campaigns не було. Повний підсумок кваліфікації та доставки — наприкінці цього звіту.

`historical_rx_regression_cause=not_established`; `current_CPU_candidate=accepted`; `target_Win10_MSAA4_100FPS=not_verified`; `new_build_manual=not_run`.

## Історичний результат до фінальної кваліфікації

**Кандидат реалізовано, виміряно й відхилено за оголошеним RX-tail gate.** Production відновлено до exact actual B0; нового EXE/package немає. Це не ще один history-cache або quaternion експеримент.

Статуси: `production_fix=rejected_and_withdrawn`; `integrated_CPU_gain=measured_p50_gain_not_qualified`; `MSAA4_total_frame_gain=not_run`; `target_Win10_100FPS=unproven`; `manual_acceptance=not_run`.

Продовження25.10: **результатC — причинність попереднього RX-tail не закрита**. Нові24 bounded runs пройшли свої CPU gates, але A/A не довів помилки старого контролю; це не дає підстав стерти попередню відмову або назвати production-проблему виправленою. Виправлено повноту приватного вимірювання release, збережено operation witnesses і результати. Новий EXE не випускається; подробиці в останньому розділі.

Основний CPU p50 виграш у контрольованій кампанії — **195,1 мкс** для16/16 Diagnostics Off. Проте `commit_new_rx` Diagnostics On мав p99 regressions у2/3 пар понад50 мкс. Основний gain не скасовує цього blocker. Причинність RX погіршення саме від arithmetic change **не встановлена**; називати його гарантованим шумом також не можна. Повторів до favorable pass не робили.

[Evidence JSON](evidence/CPU_STUDIO_POSE_FRAME_BUILD_50B5FFE1.json), [task records](../manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024), [попередній пакет](CPU_HISTORY_PROJECTION_50B5FFE1.md).

## Actual B0 та незалежність

`main`, HEAD `6a86ce0e3b826341c90bbcee8c603771f1dc58c8` плюс збережені попередні dirty changes. На intake усі754 production entries збіглися з попереднім actual B0. Новий snapshot:1328 source/test/script entries,131 libs,4 shipped binaries, git diff/status/CMakeCache. Source root та Win32 CMake paths перевірено. Snapshot — не clean-HEAD claim.

Actual B0 `src/entity_render/entity_scene_render.cpp`: `c86202eba1682bdfe0fc6138fa6d64cb497f89eeb9aabb2196964587ff2b20a6`; candidate: `20b93defeba5a8d1cf029ba6d9c1c46cd31624b089887d57709880b4b7b825e2`. Єдина змінена normal library — `hlclient_entity_scene_render.lib`; pose/model/game API headers та ABI не змінювались.

MSVC14.34.31933/v143, Release/Win32/O2, без fast-math/approximate trig/new ISA requirement. B0 driver лінкує frozen original libs; B1 — той самий production harness із candidate frame library. Profiling copies не лінкувались у timed drivers.

| Controlled driver | SHA-256 |
| --- | --- |
| B0 | `773684acb04817238f282b1e9bacef6934c3f5fb9940afc8c7f12f7e754049e3` |
| B1 | `0b3af4e0c591e4fb3bde44a7916176b891fbcf6d23832fedbb626d064ab73e1f` |

Original screen/final drivers та всі hashes окремо збережені. Нові файли тестів не перетворюють старі63 passed або4190/75 на новий suite pass. Старі shader/resolve/setup campaigns та відхилений endpoint reuse не повторювались і не повертались.

## Деталізація normal path

Окремі **private profiling copies** production app/pose/frame-builder source з test-owned aggregate collector. Shipping source не отримав clocks по кістках, нових pose fields або profiler framework. Стек кожної алокації збирався тільки в окремому profiling run; symbols/JSON виводились після fixture, не на кадр. У timed drivers — лише попередній bounded allocation counter, однаковий для B0/B1.

16 actors/16 observations,64 warm+256 samples, Diagnostics Off; synthetic BSP, actual local Gordon/p_shotgun, synthetic viewmodel. Публічні clocks/current-time poses,1/2/4 blends/controllers/gait/transition/hierarchy/attachments не змінювались. Historical frame inputs не відтворювались і не вигадувались; це не target Windows10 full-frame replay.

Детальний profiler **має overhead**: present p50 з ним1707,3 мкс, у тому самому private driver з вимкненими таймерами1443,2 мкс; exact output signature однаковий. Наведені нижче агреговані суми/call counts — attribution run, не acceptance p50. Timers inclusive, їх не додають до батьківських; frame scope тут включає world **і local viewmodel** build.

| Scope, усі256 кадрів | Сума, мкс | Calls |
| --- | ---: | ---: |
| Parent compose | 220445 | 4096 |
| Immutable pose setup/validation до controller selection | 1654,3 | 11264 |
| Track metadata validation | 20185,7 | 339712 |
| Channel decode/controller stage, inclusive track validation | 126414 | 339712 |
| Endpoint rotation math | 74470,3 | 339712 |
| Local blend/motion stage | 388,7 | 11264 |
| Per-sample hierarchy validation/build | 11863,8 | 11264 |
| Parent posed bounds | 29058,2 | 4096 |
| Child path inclusive | 68515,9 | 4096 |
| Child sampling/merge | 40743,8 | 4096 |
| Child bounds | 18753,6 | 4096 |
| Child socket plan/evaluation | 2476,7 | 4096 |
| EntityRenderFrame build | 95774,8 | 512 |
| **Frame signature** | **87978,6** | **512** |
| Transactional publication | 1396,6 | 256 |

Частина validation/channel timings містить вартість приватних clocks; не приписуємо весь pose scope незмінним перевіркам. Канали — owned `ModelAnimationChannel` vectors із bounded run lookup, не byte-stream decoder за назвою функції. Current-time decode/interpolation/math потрібні; даних для складного prepared-owner fast path цей attribution не довів. Натомість **приблизно92% frame-build scope** локалізовано у signature з одним aggregate timer, без per-bone clock у цьому scope. Тому обрано вузький напрям B, а не три нові кеші.

## Allocation/copy sites та lifetime

Окремий stack run:94 bounded sites,0 overflow; peak203914 **tracked requested live bytes** у вибраних warmed phases,0 tracked live після fixture teardown. Це не RSS, не повний resident asset cache і не доказ відсутності всіх можливих malloc leaks. Кожний site peak збережений окремо й не підсумовується як concurrent peak.

| Найбільший site | Calls /256 frames | Requested bytes |
| --- | ---: | ---: |
| Parent render-palette vector growth/copies | 40960 | 33292288 |
| Final composed world palette resize | 4096 | 14450688 |
| Child render-palette vector growth/copies | 28672 | 9961472 |
| Main local-bone sample reserve | 4096 | 4816896 |
| Child evaluated world palette | 4096 | 4128768 |
| Additional local blend sample | 2816 | 3311616 |
| Sampled observation owning copy | 256 | 3142400 |
| Presented effect-entity owning copy | 256 | 3142400 |

Temporary local poses живуть у compose/evaluate/merge; published palette ownership переходить у immutable frame, старий frame звільняється через звичайну публікацію/останнього читача. Sampled/effect observations володіють своїми vectors. Увесь traffic398006 bytes/frame не приписано frame builder. Нового reserve/child-merge варіанта зі старої відхиленої P1 не вводили. Retained palette capacity, recorder capacity та actual allocations у CPU runs збережені; кандидат їх не збільшував.

## Єдина production-правка

`StableHasher::add(uint32_t)` історично серіалізує **8** little-endian bytes:4 bytes значення та4 нулі. Для FNV-1a нульовий byte не змінює XOR, тому чотири такі кроки точно дорівнюють `h * prime^4 mod 2^64`. Кандидат хешував low4 bytes, потім виконував це одне unsigned multiplication.

Це **8→5 source-level modular multiplications**, не виміряний hardware-instruction counter. Serialization length/order/hash contract не змінено; uint64/double path лишався вихідним. Float bits, включно зі signed zero, надходили через незмінний bit_cast. Жодної приблизної математики, нового cache/trusted flag/власника, allocation або зміни arbitrary-input validation.

Result frame досі owning та immutable, frame construction/publication/destruction і delayed consumers залишались у тому самому lifecycle. Немає work shift у commit/destructor/worker. Немає pose throttling, stale samples або додаткового кадру затримки; A–D, prediction, RX ordering, bounds/culling, light/chrome та effects не змінювались.

## Independent oracle та screening

Новий `[frame-hash-padding]` test:1024 ordered records у незалежних original-B0/candidate binaries; **6112 assertions кожний, точна відповідність** signatures/errors/entity IDs/draw counts. Корпус містить seeded finite bit patterns, ±0, subnormal/nearby/extreme finite values, NaN/Inf rejection, invalid identity/skin/pose/opacity/blend/entity та5 delayed owning readers. Після build мутація input не змінює retained palette; failed build не пошкоджує retained results. Float tolerances не змінено.

Baseline-only screen budgets записано до candidate timings. Дві interleaved pairs із reverse order для16/16 noRX та RX дали present gains219,1/199,3 і201,7/209,4 мкс; gates80,705/97,485 мкс. Це дало дозвіл на повну campaign, **не final acceptance**.

Наперед зафіксовані правила: primary p50 paired median >max(30 мкс,B0 span,5% B0 median)+10 мкс та≥2/3 positive; recurring≥2/3 p50/p95/p99 regressions понад власний max(50 мкс,5% B0 median,B0 quantile span) — blocker. Allocation calls/bytes/capacity не повинні зростати. Raw max/cold values зберігаються. Gates після failure не послаблювались.

## Початкова48-run campaign та причинний cold repair

3 pairs ×4/2,8/16,16/16 noRX і16/16 RX every4 renders ×Diagnostics Off/On:48 processes/12288 warmed frames. Основні CPU gains та warm allocations пройшли, але в5 pair-level cold comparisons були точно **+6 calls/+1988 bytes**. Initial `final-cpu-analysis.json` зберіг **CPU_not_accepted**; його не переписано favorable результатом.

Cold counter включав setup fixture, зокрема native temp path з непадованим PID. Окремий незмінний B0 відтворив причину:

| Test PID | Padding | Root length | Cold calls | Cold bytes |
| --- | --- | ---: | ---: | ---: |
| 1234 | ні | 84 | 52077 | 10466916 |
| 12345 | ні | 85 | 52083 | 10468904 |
| 1234 | 10 digits | 90 | 52063 | 10465440 |
| 12345 | 10 digits | 90 | 52063 | 10465440 |

Це точний причинний контроль, не вибір вдалого repeat. Змінено лише **private test-support copy** формування temporary path; shipped fixture helper/production не змінювались. Virtual resources, model bytes, clocks, calls та frame outputs ті самі. Один окремо оголошений confirmation на48 processes із реальним padded PID та root length90 у всіх runs; screening/initial/confirmation не pooled. Zero-growth memory gate залишено тим самим. Усі initial bad rows збережені.

## Confirmation: CPU gain є, acceptance немає

| Actors/history, режим | B0 median p50 present, мкс | B1 | Median paired gain |
| --- | ---: | ---: | ---: |
| 4/2 Off | 354,0 | 303,5 | 51,1 |
| 4/2 On | 353,0 | 303,4 | 50,4 |
| 8/16 Off | 734,4 | 644,2 | 102,5 |
| 8/16 On | 745,1 | 635,1 | 106,1 |
| 16/16 noRX Off | 1434,3 | 1244,3 | 195,1 |
| 16/16 noRX On | 1436,2 | 1252,0 | 178,7 |
| 16/16 RX Off | 1798,8 | 1632,7 | 202,9 |
| 16/16 RX On | 1787,6 | 1605,5 | 182,5 |

Medians вкладених scopes не складаються; paired median не дорівнює різниці двох medians. Exact signatures/no-RX pose witnesses збіглися в усіх runs. Warm і cold allocation/byte/capacity gates пройдені після причинного fixture repair. Cold application preparation includes owned fixture imports/states/assertions, не cold OS cache: cache flush не робився. Raw cold times/counts/bytes є в evidence, не приховані у warmed gain.

**Blocker —16/16 RX Diagnostics On, commit_new_rx:**

- B0 p99:99,5 /77,2 /90,1 мкс; B1:157,9 /103,3 /225,6.
- Paired regressions58,4 /26,1 /135,5 мкс; **2/3 понад50 мкс**, отже strict fail.
- p50 medians49,4→49,8 мкс; paired p50−0,4. Це не переміщення виміряного великого обсягу роботи в commit, але tail gate все одно порушено.
- Max: B0107,3 /82,0 /115,2 проти B1206,2 /474,9 /708,4 мкс. Усі3 max гірші; їх не виключено.
- Parent-new-RX мав paired p50 gain171,3 мкс і paired p99 gain171,9 мкс; окремий поганий третій repeat збережений. Це не дозволяє ігнорувати commit blocker.

Source change не додавав commit calls або allocations, але причина observed tail не доведена. Її не списуємо на noise і не міняємо правило після результату. Інших випадкових variants/довгих повторів до pass не запускали.

## Перевірки, відновлення та доставки

Після withdrawal свіжі Release checks: **40 frame/presentation passed +1 skipped**, **18 pose passed**,0 failures. Skip — opt-in ordinary installed NPC: exact research root не заданий. Це58 нових executed cases плюс1 skip, не попередні63 і не full suite. Незалежний oracle окремо1024 rows/6112 assertions на кожному binary.

Accepted-candidate Debug/full Release/core-off/alternate/architecture/ASan/OpenGL/Null/MSAA0/2/4/8/launcher acceptance gates — **not_run**: CPU qualification не пройдена. Старі passes не перенесено; ASan build/runtime обидва not_run у цьому пакеті. Нормальний новий EXE не збирався. Підготовлені копії quality helper scripts не означають їх виконання.

Setup failures/retries збережені: sandbox C1902 PDB; та сама private profiling збірка поза sandbox пройшла; C4324 усунуто явним padding private Win32 allocation header, без послаблення /WX; screening wrapper `==` виправлено до `-eq` **до0 timed runs**. Profiling retries не є новими candidate timing attempts.

Process guard inventories/polling не бачили сторонніх builds/games під час timings; чужі процеси не завершувались. Polling може пропустити процес, що повністю вклався між polls, і не доводить нульового іншого OS load. Нових live/gameplay/Steam-auth/HLDS sessions0; remote execution0.

Власну єдину production-правку відновлено через `apply_patch`:754/754 source entries exact actual B0, frame library rebuild exit0. Rejected source/lib та всі private binaries/raw results/failed analyses збережено. SDL3.dll **не перебудовувався**: залежності/активний normal EXE не запускались на build. Фінальний read-only audit підтвердив усі12 local/shared/dist shipped files і canonical ZIP exact B0. Settings/logs/auth/shared test-data не чіпали.

Client SHA `7765414555a74e4ab5cd3bdf7ddce9da03d998c4ba76659acf43fce92022cd22`; approved SDL `ad833f09fad8d99eadfdb1a765a76f9f20872a3ab6fadbef648f09334f2fac87`; ZIP `67b9b79ae631b34efb747cbc42f7f144fb6eaab5b1253da190007e9e66515412`. Нового package/SHA-only release не видаємо. Staging/commit/push none; branch/main не перемикалась.

## Незакритий результат

Безпечна арифметична можливість і суттєвий local integrated CPU p50 gain доведені, **release qualification не завершена через конкретний RX DiagnosticsOn tail**. Подальший причинний аудит має відокремити commit/diagnostic-time/call-site tail від measured frame-signature saving; до нього не повертати цей rejected код як stable. Великі allocation/copy sites теж збережені як дані, не як автоматичний дозвіл повернути старі reserve/merge candidates.

CPU subset не включає весь normal update/render/swap.1000/pose_ms або1000/present_ms не названо FPS; GPU gain і100FPS на Windows10/1920×1080/MSAA4/VSyncOff/fps_max0/neutral image не підтверджено. Новий user test незмінного B0 не потрібний.

## RX-tail investigation — 25.10.2026

Продовження `HLC-STUDIO-CANDIDATE-RX-TAIL-CAUSAL-FIX`: [нові task records](../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025). Попередній `rejected_and_withdrawn` verdict та всі48 controlled runs збережено. Intake HEAD той самий; новий snapshot1336 source/test/script files,131 libraries,4 shipped binaries. Усі754 `apps/src/include` entries збіглись із actual B0 попереднього snapshot. Production не відновлювався до HEAD і не редагувався для досліду.

Кандидат — **лише точне згортання чотирьох нульових bytes FNV**, не quaternion/history cache. Source/lib SHA наведені вище; приватний B1 має власну копію headers (байти ідентичні B0) та131 frozen libraries, із яких **тільки** `hlclient_entity_scene_render.lib` замінена на збережену candidate library `9e0b12d11d38bf43999b96848ddbe4f8c68f1324f81c6a540f104715ad3c913c`. `B0-long-inputs.json`, `B1-long-inputs.json` фіксують усі headers/libs; ABI або різні поточні headers не змішувались.

### Відновлені первинні значення

`historical-rx-analysis.json` заново прочитав raw arrays останньої **controlled**, а не screening/initial campaign. Кожний наведений квантиль має **64 valid new-RX observations**:64 warm-up renders,256 measured renders, RX кожні4 renders,192 measured no-RX. Усі runs exit0, без skipped/dropped iterations. Квантиль — ascending sort і `floor((N−1)×q)`, без interpolation: p50 index31, p95 index59, p99 index62, max index63. Таким чином p99 — друге найбільше значення. Малий обсяг сам по собі не доводить noise.

16 actors/history16, Diagnostics On; `Δ = B1−B0`, додатне — гірше. Час у мкс:

| Пара | Метрика | B0 | B1 | Δ, мкс | Δ, % | Valid RX/run |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | p50 | 49,4 | 49,8 | +0,4 | +0,81 | 64 |
| 1 | p95 | 59,8 | 61,4 | +1,6 | +2,68 | 64 |
| 1 | p99 | 99,5 | 157,9 | +58,4 | +58,69 | 64 |
| 1 | max | 107,3 | 206,2 | +98,9 | +92,17 | 64 |
| 2 | p50 | 46,2 | 45,1 | −1,1 | −2,38 | 64 |
| 2 | p95 | 66,5 | 62,0 | −4,5 | −6,77 | 64 |
| 2 | p99 | 77,2 | 103,3 | +26,1 | +33,81 | 64 |
| 2 | max | 82,0 | 474,9 | +392,9 | +479,15 | 64 |
| 3 | p50 | 50,4 | 58,2 | +7,8 | +15,48 | 64 |
| 3 | p95 | 66,9 | 91,2 | +24,3 | +36,32 | 64 |
| 3 | p99 | 90,1 | 225,6 | +135,5 | +150,39 | 64 |
| 3 | max | 115,2 | 708,4 | +593,2 | +514,93 | 64 |

p99 gate `max(50 мкс, 5% baseline median, baseline p99 span)` = `max(50;4,505;22,3)` = **50 мкс**. Перевищення в2/3 пар — коректне застосування старого правила, не арифметична помилка. Max не був самостійним gate, але всі значення збережено. Виграш195,1 мкс стосувався **p50 present_entities** у16/16 **no-RX Diagnostics Off**, не commit і не цілого кадру.

### Межі операцій і знайдена прогалина таймера

`commit_new_rx` вимірює весь `observe_entities`: validation/generation guard → owning observation copy → ordinary/player history copy, append/eviction → candidate/lifetime vectors/maps → follow-owner clone → effect/decal copies → noexcept publication/moves/erase → staged-owner destruction перед return. Це не тільки функція з назвою `commit_authoritative_observation`. Стек `EntityRenderFrameBuilder`/FNV належить пізнішому `present_entities` та `materialize_viewmodel`, не викликається безпосередньо з цього commit path. Runtime library, input validation, структури/headers, capacity policy та ownership contract у B0/B1 однакові. Сам по собі незмінний source/library не виключає непрямого allocator/cache/lifetime/code-layout впливу.

Старий parent починався перед observe, включав remote present, host/viewmodel і diagnostic collection, але закінчувався **до destructor локального owning viewmodel frame**. Старий world frame звільняється в normal publication за відсутності іншого читача; локальний результат жив до кінця fixture iteration після oracle. Отже, попереднє source-level твердження про відсутність work shift не було повним timing-доказом lifetime cost. Новий приватний harness зберігає старі scopes, додає `local_frame_release`, engine teardown та **same-event** `owned_*_sum = parent + local release`. Це неперекривні інтервали одного event, не сума p99/medians. Oracle/assertions між інтервалами лишаються явно поза CPU workload; повний render/swap/GPU кадр тут не виміряний.

### A/A перед новою оцінкою B1

Перший A/A — **точно той самий** original controlled B0 EXE SHA `773684acb04817238f282b1e9bacef6934c3f5fb9940afc8c7f12f7e754049e3`, без rebuild. Оголошено3 pairs/6 processes з forward/reverse порядком, тим самим64/256 workload, clocks, actual models та padded native temp path90. Невиміряний старт і один contaminated слот відокремлено через сторонній MSBuild; чисті слоти не повторювались. `aa-analysis.json`:

| Пара | A0 commit p99, мкс | A1 того самого B0 | Δ, мкс |
| --- | ---: | ---: | ---: |
| 1 | 92,7 | 73,4 | −19,3 |
| 2 | 105,1 | 92,6 | −12,5 |
| 3 | 64,6 | 104,7 | +40,1 |

Чинний50 мкс gate **не відхилив B0**:0/3 commit violations, жодного recurring blocker в інших scopes. Це обмежені3 process pairs; воно **не доводить**, що стара B1-регресія була noise. Причинної перемоги типуB за цим A/A немає.

### Одна нова bounded campaign

`long-plan.json` оголошено до нової B1 оцінки:3 pairs ×long A/A On, B0/B1 RX Off, RX On та noRX primary Off =24 process slots. RX:64 warm +4096 measured renders,1024 new-RX/3072 no-RX; primary noRX лишився256. Scripted clocks5/20 мс і history16 збережено, підготовлено достатньо owned source records. Той самий lower-order quantile; p99 index1012,11 значень вище. Gate formula/tolerances не підвищено. Сусідні1024 RX — не1024 незалежні process repeats; baseline span та raw maxima зберігаються. No pooling зі старою campaign.

Новий long A/A (та сама нова B0 SHA `f3f4e86a7203610e36f2b15ef052d2972b07d28d29277619b2c47513315332cc`) пройшов: commit p99 A0 `94,7/113,3/106,9`, A1 `101,9/106,5/140,5` мкс; Δ `+7,2/−6,8/+33,6`,0/3 violations50 мкс. Parent p99 span460,8 мкс показує, що навіть1024 RX не роблять весь CPU workload точним до мікросекунди. `long-aa-analysis.json` зберігає всі scopes, memory і bounded witnesses; B1-driver SHA `17d2c0d1ef4f6b900f84d2ef27c816e088afd588ab9180e029cb32e9f10c884d`.

Кожна довга RX-подія має RAM witness; після run виводяться максимум16 найповільніших commit/owned events:ordinal/render/source record,1 committed record/18 entities, model/generation changes, history append/eviction bound16, allocation calls/requested bytes/frees, non-owning old/new frame strong-owner counts. Додаткового strong owner не створюється; він змінив би час release. Це не лог кожної кістки й не JSON write в timer. Detailed stack profiling відокремлено від performance runs.

Свіжий original-B0/candidate oracle:1024 exact ordered rows,6112 assertions кожний. Додатковий focused Release:по39 executed/1 opt-in skip/0 failures на B0 і B1; він включає oracle case, тому не сумуємо його двічі як різні correctness cases. Lifecycle/slot/reset, source append/eviction, no-RX time, failed staging та delayed readers збережені. Це **не full suite**, не старі58 post-withdrawal passes.

### Завершена нова оцінка — без переписування старої відмови

Усі24 чисті оголошені process slots завершені; valid timing failure не повторювався. Один B1 RXOff slot зі сторонньою компіляцією збережено окремо, не включено й замінено тільки після idle. Жоден чистий результат не відкидався. `long-analysis.json` — **0 recurring timing/memory flags** за наперед записаним правилом. Це CPU-data qualification конкретного нового методу, не автоматичний production acceptance.

Кожна клітинка нижче — `p50 / p95 / p99 / max`, мкс; **1024 valid new-RX/run**,3 незалежні process pairs на режим:

| Diagnostics | Пара | B0 commit | B1 commit |
| --- | --- | --- | --- |
| Off | 1 | 51,0 /76,7 /109,6 /461,9 | 47,7 /67,9 /91,5 /155,9 |
| Off | 2 | 45,9 /62,0 /90,1 /426,7 | 50,3 /75,7 /101,3 /252,5 |
| Off | 3 | 47,0 /69,2 /98,5 /240,0 | 44,8 /61,8 /106,3 /460,9 |
| On | 1 | 46,4 /66,8 /106,3 /1202,0 | 41,8 /57,9 /74,0 /129,0 |
| On | 2 | 42,9 /59,4 /80,8 /182,2 | 37,0 /53,8 /81,9 /302,9 |
| On | 3 | 41,8 /57,6 /69,4 /145,1 | 40,3 /58,5 /84,2 /251,3 |

Commit p99 Δ Off `−18,1/+11,2/+7,8`, On `−32,3/+1,1/+14,8` мкс;0/3 понад незмінний50 мкс gate в кожному режимі. Старі `+58,4/+26,1/+135,5` при64 RX цим не видалені й не названі noise. Max On гірший у2/3 нових пар, але B0 має окремий1202 мкс event; max не перетворено постфактум у новий gate або аргумент про scheduler.

| Режим/пара | B0 parent new-RX p99 | B1 | B0 owned new-RX p99 | B1 |
| --- | ---: | ---: | ---: | ---: |
| Off/1 | 3212,0 | 2446,6 | 3212,6 | 2447,5 |
| Off/2 | 2597,7 | 2630,8 | 2598,2 | 2631,6 |
| Off/3 | 2747,7 | 2547,4 | 2747,9 | 2547,7 |
| On/1 | 2623,6 | 2347,1 | 2624,3 | 2347,8 |
| On/2 | 2538,2 | 2221,4 | 2539,3 | 2221,9 |
| On/3 | 2515,8 | 2823,6 | 2544,0 | 2825,2 |

On/3 parent p99 погіршився на307,8 мкс понад власний126,91 gate, owned на281,2 понад127,2 — **1/3**, не recurring2/3. Цей невдалий repeat збережено. Full frame/GPU/swap не виміряно; commit p99 не названо frame p99.

Primary noRX Off present p50 B0 `1421,3/1423,8/1433,0`, B1 `1226,3/1196,1/1185,2` мкс: median paired gain **227,7 мкс**, primary gain gate81,19 мкс пройдено. Owned visual sum paired gain228,0 мкс. Новий вимір не pooled зі старими195,1 мкс; обидва демонструють корисний local CPU saving, але не100FPS і не самостійне пояснення старих tail events.

Release не приховано: local frame release median0,3 мкс у всіх режимах/версіях, raw p99/max збережено. Наприклад, B0 On/3 мав owned max8201,1 мкс проти parent max7626,6; це саме той lifecycle cost, який старий parent міг пропустити. Engine teardown subset (`host.teardown`, world reset, assets owner reset):0 allocations,16685 frees Off або16686 On, парні counts однакові; raw teardown time збережений окремо, це не normal per-frame render. Решта fixture/recorder/metadata destructors при return не входять у цей subset; повне звільнення tracked commit allocations після return перевірене окремим profiler, не назване повним timing shutdown.

### Operation/memory attribution

У всіх270 відібраних slow-event witnesses:1 source record/18 entities, **87 allocation calls/279318 requested bytes/87 frees у commit**, old/new strong frame owners1/1, model/generation changes0. Відомі fixed-input player lifetimes не змінювались. `history_append/eviction=1` означає одну зміну source у вже повному16-entry history кожного presentation owner: це виведений із fixture/source-capacity witness, не доданий production counter. Не трактуємо його як виміряний окремий time scope.

Наприклад, B1 On/2 event928/render3772/source958:commit302,9 мкс, parent2008,0, local release0,2, owned2008,2; той самий87/279318/87,18 entities, жодного resource change. B1 On/3 event880:commit251,3, parent1929,6, release0,3; такий самий commit traffic. Різниця present allocations між events відповідає різним animation inputs; **парні totals B0/B1 exact**.

RX run totals B0=B1:commit89088 calls/286021632 bytes; present4240628 calls/1707776136 bytes; published matrix capacity881; recorder capacity0 Off/5314632 On. Cold preparation B0=B1:67190 calls/69922506 bytes Off,67199/75237348 On; larger cold fixture містить достатню кількість owned records для1024 RX, тому не порівнюється як production memory growth зі старим100-record setup. Primary noRX commit allocations0, present241920/101889536; cold53778/17377164. Cold raw timings не включені у warmed quantiles і не видалені.

Окремий **symbolized allocation profiler** (`commit-allocation-profile-20261024T222338448Z-d7a95c`):тільки warmed `observe_entities`,1024 RX,54 bounded sites,0 overflow. B0/B1 **exact normalized stack/calls/bytes/site peaks**, concurrent tracked peak624098 requested live bytes,0 tracked live після fixture. Це не RSS/total process memory; site peaks не складаються. Expanded allocation headers/CaptureStackBackTrace/PDB perturbed profiling, тому жоден час цього run не використано у gates.

Найбільший RX site — `make_unique<EntityFollowPresentation>(entity_follow_)` у `stage_authoritative_observation`:1024 copies по239760 bytes,245514240 requested bytes загалом, site peak239760. Observation packet-entity owning copy та effect-entity vector assignment — по1024 calls/12569600 bytes кожний. Ці витрати **однакові у B0/B1**. Дорогий existing follow clone не є доведеною причиною різниці tail і не отримав випадкової нової optimization-переробки в цьому завданні.

Build/profiling setup retries збережені:перший private profiler не компілювався через пропущений `<array>` після вилучення старого capture header; виправлено лише include, /WX не послаблено. Перший успішний profile без PDB мав numeric stacks; він збережений, а symbolized link зроблено окремими targets **після** завершення24 timed slots. Це не performance repeat. Sandbox `git diff --check` спочатку не мав доступу до submodule metadata; та сама read-only команда поза sandbox пройшла, exit0.

### РішенняC та стан доставок

**Причина саме старих58,4/135,5 мкс RX-p99 excess не встановлена.** Прямих нових commit calls, allocations, capacity growth, changed structures, extra rebuild або перенесення measured CPU роботи в release не виявлено. Непряма зміна часу owning stage/allocator/free/code-layout не локалізована до конкретної операції; scheduler/Windows/driver не оголошено причиною. Старі event-level operation traces відсутні й не можуть бути відновлені з самих64 timing values.

Нові CPU gates пройшли, але ні old A/A, ні long A/A не відхилили незмінний B0. Отже, **A/A не довів проблему вимірювання**, і outcomeB заявляти не можна; production RX-причину не виправлено, тому outcomeA також не заявлено. За заданим варіантомC докази щодо історичної причинності залишаються неоднозначними. Корисна арифметична правка, independent oracle, новий release-complete harness і всі manifests збережені для продовження, але rejected кандидат не повернуто як stable на підставі лише favorable нової campaign.

`production_fix=rejected_and_withdrawn`, `rx_tail_investigation=C_causal_unresolved`, `new_CPU_method_gates=passed_not_production_acceptance`, `manual_acceptance=not_run`. Accepted-fix Debug/core-OFF/alternate/architecture/OpenGL/Null/GL3.3/launcher/preflight/full Release/AA0/2/4/8 — **not_run**, оскільки production fix не прийнято. ASan build/runtime окремо **not_run**. Жодних старих suite passes не перенесено; нового нормального Release EXE/package не зібрано.

Фінально754/754 production files exact intake B0. Read-only SHA audit підтвердив12/12 local/shared/dist shipped binaries **включно з SDL3.dll**, canonical ZIP exact B0; package metadata незмінна разом із ZIP. `final-delivery-audit.json` у новому task root. Settings/resources/auth/logs/shared test-data не копіювали й не змінювали. Remote execution0, live/gameplay/Steam-auth/HLDS sessions0; staging/commit/push/branch changes none. Повторного user test B0 не просили й нового EXE не видаємо.

## Фінальна кваліфікація та доставка — 25.10.2026

Цей розділ замінює попереднє рішення про **поточне приймання**, не стираючи історичної відмови або невідомої причини старого RX-tail. Нове завдання явно розділило історичну причинність і критерії поточного кандидата. Актуальних failed/inconclusive обов'язкових correctness/performance критеріїв після наведених нижче перевірок не залишилось. Не перевіреними залишаються саме Windows10 gameplay та цільові100FPS, а не ця обмежена CPU-оптимізація.

Артефакти: [qualification.json](../manual-artifacts/task-records/pose-final-qualification-50b5ffe1-20261025/qualification.json), [publication receipt](../manual-artifacts/task-records/pose-final-qualification-50b5ffe1-20261025/delivery-rollback-20261024T232951184Z-ed378287/publication-receipt.json), [delivery audit](../manual-artifacts/task-records/pose-final-qualification-50b5ffe1-20261025/final-delivery-audit.json). Acceptance receipt SHA `230425c5e4420ee0872d0a8161a618eb4ff5c9d2cc6e32055e792573e905fd31` зафіксований **до** публікації; його `delivery=pending` означає стадію документа, фактична доставка підтверджена окремим publication receipt.

### Заморожений кандидат і використані докази

З нового actual-B0 snapshot відновлено **лише** FNV uint32 padding-fold у `src/entity_render/entity_scene_render.cpp`; SHA source exact `20b93defeba5a8d1cf029ba6d9c1c46cd31624b089887d57709880b4b7b825e2`. Решта753 production files, включно з попередніми змінами лаунчера, не змінена. Не повернуто GPU/history/quaternion експерименти. Незмінні порядок і біти підпису, numerical/error/ownership contracts; нових cache/allocations/clocks немає.

Перебудовано тільки цю Release-бібліотеку; інші130/131 libraries exact B0. Її SHA тепер `90b989c94d601afed0f3e20b3e7cb167cfee596943f243bd60e5c2039212c96b`; відмінність від архівної `9e0b12…` пояснено **точним порівнянням усіх9488 рядків dumpbin /all після вилучення лише імені input і archive/COFF timestamps**: код, raw sections, symbols і relocations exact. Це не нова версія арифметики, якій приписано старий gain.

Незалежний read-only аудит повторно звірив24 accepted stdout/stderr/EXE hashes,264 raw arrays із quantiles,132 gate calculations,395 headers,131 libraries, однакові harness/defines/compiler options і план до B1. Всі1024 RX/run, порядок lifecycle і симетричний explicit release підтверджені. Збережено prelaunch refusal і2 contaminated runs; повторної timed campaign — **0**. SHA незміненого `rx-tail-investigation.json`: `93546118b3484b7ec69bf1e41cba6f490a34c7c300048d8a99bfb7dbd9bb284a`.

Виграш **228,0 мкс** — median paired p50 `owned_visual_sum` у PrimaryOff,16 actors/history16; present окремо227,7 мкс. Scope включає CPU visual-update parent та explicit local-frame release; **не** GPU/render/swap або повний ігровий кадр. Gate81,42 мкс пройдено;1024 RX у кожному long run,0 recurring RX/memory failures. Погіршення RXOn pair3 parent p99 на307,8 мкс/owned на281,2 мкс збережене:1/3, не recurring2/3. Allocation/capacity totals та54 normalized sites exact; tracked requested-live peak624098 bytes в обох, live після fixture0. Це не вимір RSS. Історична причина лишається `not_established`, не названа noise.

### Нові перевірки саме B1

| Перевірка | Результат |
| --- | --- |
| Focused Release | 36 груп;403 case executions,6 явних пропусків,0 failures |
| Focused Debug | 36 груп;403 case executions,6 явних пропусків,0 failures |
| Повний Release offline | 4267 registered/accounted;**4192 executed,75 explicit skips,0 failures**;535,81 с |
| ASan Debug build | Пройшов; окремий початковий sandbox C1902 збережено |
| ASan Debug runtime, core-only | **645 executed,1 opt-in skip,0 failures** |
| Normal OpenGL offline fixture | Завершено;5 source records,visual_failed0,gl_errors0,4 непорожні framebuffers,3 зміни зображення |
| AA correctness | Фактичні0/2/4/8 samples;140 exact color/resolved-depth pairs на режим у dynamic control; Studio/lighting controls також passed |
| Core-only / alternate / architecture | Реальний game-module-OFF build; frame/pose/alternate-module cases і boundary guard passed |
| Launcher/preflight/package | Source-only reference/off, native UI, startup failure logs, Null, чиста Unicode-папка, dependency/ZIP audit passed |
| Після доставки | 2 canonical local Null/runtime-visual smoke tests passed із доставленими DLL |

403 — case **executions у scoped selections**, не deduplicated unique coverage; не додаємо їх до full suite. Усі назви75 full-suite skips збережені в qualification JSON; це explicit opt-in/platform cases, не приховані failures. Actual AA0/2/4/8 не skipped. Normal OpenGL smoke використовує project-generated offline fixture, не live stock gameplay.

Full suite запущено на exact normal Release candidate через перевірений CTest shadow:4267 імен/порядок, аргументи та **всі** properties/cwd exact; змінено тільки13 normal-client path tokens. Перед ним перелінковано всі потрібні consumers, включно з6 додатковими CLI/test EXE, пропущеними старим renderer-only планом. Кандидат використовує normal `main.cpp` → `present_entities`/`materialize_viewmodel` → `EntityRenderFrameBuilder`; test-only hashing path немає.

Setup retries не приховані: старий Debug CMake graph не містив нового test target — регенеровано graph; ASan sandbox C1902 — ті самі source/flags пройшли поза sandbox; одна helper-команда виправлена до запуску EXE через порожній PSScriptRoot. Production для QA не змінювався, tolerances не послаблювались. `git diff --check` passed; staging/commit/push/branch changes none.

### Доставка та межі результату

Один normal Release EXE,Win32/MSVC14.34.31933,5584384 bytes:
`dfc0f490e725ad1ae8cc171a07221993173f92d23b8015419bb526b6dcbe4a9a`.

- Local `build/bin/Release` — delivered/verified.
- `\\192.168.0.104\storage\HLClient-Win10-Test` — delivered/verified.
- `dist/HLClientEngine` та canonical ZIP — delivered/verified. ZIP SHA `62219e73a68e57e097df90ed0a575ea7c2d13ac4871c2e8dd20eba0d6aa9f9b6`.

SHA/розміри **24/24 EXE/DLL entries** (8×3 destinations) exact. Launcher/helper/SDL не перебудовані; SDL SHA `ad833f09fad8d99eadfdb1a765a76f9f20872a3ab6fadbef648f09334f2fac87`. До локальної папки додані ті самі4 аудовані CRT DLL з portable package, щоб dependency bytes теж збігались. Перед overwrite збережено rollback всіх змінюваних наявних файлів/metadata/ZIP, publication тримав exclusive handles. Shared test-data не обходили; settings/logs/auth/resource paths не переносили.

Поточні статуси: `historical_rx_regression_cause=not_established`, `current_CPU_candidate=accepted`, `target_Win10_MSAA4_100FPS=not_verified`, `new_build_manual=not_run`. Нові live/Steam-auth/HLDS/remote-execution sessions0. Рендер-навантаження/effects, current-time animations, prediction/network/server settings не змінювались. Наступний контроль — **один короткий user test нового EXE** на Windows10:1920×1080, genuine MSAA4,VSyncOff,cap0,neutral image, всі ефекти. CPU-fixture у FPS не перераховується.
