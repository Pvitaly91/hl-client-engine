# C99636DB / 4AADA6B1 — static-world cost, усі режими AA

> Офлайн-парну матрицю C1C завершено; фінальну Release-збірку перевірено та
> доставлено в папку лаунчера. Ізольований B прийнято, але `all_modes_frame_time_gain`
> означає лише виміряний static-world GPU scope у hidden fixture, а не
> підтверджений приріст живого FPS. Успішний ручний тест не припускається.

## Джерела та незмінні контролі

- Source checkout: `D:\DEV\CPP\HL-Client-engine`, `main`, HEAD на початку
  завдання `d8c6ea18324b920803e3a2281fc165633eadf8ec`. Actual source
  був чистим перед правками цього завдання; історичний `22ad3a5...` не є
  тотожним R, бо пізніший World/RX source включав додаткові зміни.
- R — збережений попередній World/RX production EXE, SHA-256
  `F97074BF4BF1131A8AD48074F9330E0BB065A21C5B1517ED4D9081B4F776FA50`.
  Архівний R actual-assets fixture має SHA-256
  `06421D9DB18D85D660350DD35767B02EF082E0E92BDD771BD6B88537FB34615A`.
- C0 — доставлений перед цим завданням candidate EXE, **байтово перевірений**
  у source/delivery, SHA-256
  `6EF226EE86A04C4A2F948B2B9C19C806721BCD263B3C19A1259EA4D8F296B63D`.
  Його архівний fixture має SHA-256
  `E347F9C5FE2D5C3421C87518CAFF56F29B6225D41D95DD372E57B55D74C0E4B5`.
  Копії R/C0 збережено в
  `manual-artifacts/task-records/all-modes-cost-c996-4aada-20261021`;
  архівні копії R/C0 не перезаписувалися для порівняння.
- C1C — поточні незакомічені зміни actual source після ізольованих A1 і B:
  13 змінених tracked files і 2 нових файли звіту/evidence від entry HEAD;
  `git diff --check` пройшов. Фінальний Release `hlclient.exe`: SHA-256
  `AB167865F27392ABBA0E59206E3648836C84F941B5BE054D29F4ABE0191972C5`,
  5 555 200 bytes; MSVC Visual Studio 2022, Win32, `/O2 /Ob2 /DNDEBUG`.
- Архів MSAA 8×: точний run ID `c99636db80472503da8748b0489d159e`,
  `C:\Users\admin\AppData\Local\HLClientEngine\runs\c99636db80472503da8748b0489d159e\msaa-x8.zip`,
  SHA-256 `6D28CABBEF57CED0BB4622F5374D14DEDBA9D42D2DEE8BBA721A5A9044201CB2`.
  Архів AA Off: run ID `4aada6b16395f989364e8dad2a3a6395`,
  `C:\Users\admin\AppData\Local\HLClientEngine\runs\4aada6b16395f989364e8dad2a3a6395\no-aa.zip`,
  SHA-256 `4367C88C7C5E16E3203CC734F8E3FF00DF53FC5D24A43E5A0FBF33F43B872B1E`.
  Report identity EXE обох — C0 SHA вище; EXE не міститься в ZIP.

У двох live runs було 1920×1080 borderless, VSync On, swap interval 1,
повідомлена частота дисплея 144 Hz, software cap 0, brightness 0.1,
gamma 1.04, contrast 1.1, diagnostics On; фактичні MSAA samples 8/0.
Обидва закриття завершилися exit 0, F8 markers — 0. Повні
`controlled_alive` еквіваленти: MSAA 8× mean 115.245 FPS, мінімальна повна
секунда 89.251; Off mean 138.709, мінімальна повна секунда 111.796.
Це **різні матчі, маршрути й сцени**, тому їх різниця не доводить ні ціну AA,
ні ефект C0. Початковий endpoint лаунчера і endpoint console-attempt могли
відрізнятися через `open_client`; за launch config не атрибутуємо гру серверу.

## Actual normal path і локальна причина вартості

У normal `scene_package` без `visible_draw_list` C0 використовує
`WorldRenderPackage` material batches та immutable підготовлені blocks:
не більш ніж 16 суміжних surface ranges у block, bounds за actual indexed
vertices. Кожен кадр бере final camera matrix, робить до 16 рівномірних
probe-тестів; за менш ніж двох точно відкинутих blocks малює повні batches.
Інакше відбирає blocks консервативно, групує contiguous ranges того ж batch,
а при понад чотирьох фрагментах повертає повний batch. Це не старий
відхилений `RuntimeWorldVisibility` selector. Власник/ranges формуються при
побудові immutable package, не кожного кадру; динамічні brushes лишаються
окремими. У Crossfire попередній контроль мав 272 prepared blocks / 11 008
retained bytes; cold cost окремо не виміряли.

Історичний [звіт C0](GENERAL_FRAME_STATIC_WORLD_51BEB30E.md) уже показав
на F8 (повні batches після probe) CPU `render_submit` p50 Off:
R `0.527–0.544 ms` проти C0 `0.568–0.581 ms` у двох інтерлівованих
парах. GPU static-world Off p50 R `4.230–4.231 ms` проти C0
`4.234–4.245 ms` — у межах тих повторів сталого виграшу не встановлено.
На матриці 9 fixed views × 2 capture × 192 frames C0 подав на 28.4% менше
API draws і 34.6% менше triangles, але це **не** доводить скорочення часу
кадру. A1 перевірив попередню проєкцію immutable bounds (`center/half_extent`),
але на парних C0→C1A ракурсах не дав стійкого CPU-виграшу, місцями додавав
вартість, а retained owner зріс з 11 008 до 32 448 bytes (+21 440).
**A1 відхилено й вилучено з фінального source**; classifier, probe і
консервативні finite/range/resource-identity guards C0 лишилися незмінними.
Не видаємо усунення CPU-overhead selection за доведений результат A.

Прийнята локальна правка B: під час immutable GPU resource build поряд зі
звичайною world-програмою компілюється один shader-варіант **без динамічних
світел**. Він застосовується тільки до static world, коли немає валідного
позитивного transient point light і список effect lights порожній. Base
textures, static lightmaps, masked alpha, sky/depth та решта формули
збережені; світлові world-кадри й усі brush instances використовують
оригінальну програму. Shader compile/link відбувається під час побудови
ресурсу, не під час бою; renderer скидає per-frame binding cache при зміні
програми. Потрібне окреме фінальне вимірювання cold build/program memory;
самі додаткові program resources не є безкоштовними.

Для діагностики owner зберігає точне `source_surface_count` кожного prepared
block. Лише при ввімкненій movement diagnostics normal app запитує typed
per-frame receipt: path, fallback, resource identity, considered/selected
blocks/surfaces, `tested_bounds` (фактичні виклики classifier, включно з
повторними), submitted draws/triangles і CPU selection/preparation interval.
Цей інтервал охоплює frustum/probe/range decisions, але виключає GL
submission та map load; `prepared_rebuilds=null`, доки немає producer-side
лічильника. Запис потрапляє лише в bounded recent-window/incident recorder.
Звичайний `render()` і default `render_with_gpu_timing()` не виконують
per-draw receipt clocks, JSON serialization чи synchronous readback. Після
збільшення `MovementFrameRecord` ручне F8-вікно потребувало
capacity-aware sampling у тому самому 8 MiB ліміті; цей регресійний тест
перевіряється окремо. Порівняння diagnostics On/Off і ціна cold shader
compile не виміряні — жоден із наведених fixture timings цього не доводить.

## Парна офлайн-матриця — протокол та ізольований B

Actual-assets fixture `[gauss-71b97-camera]`: exact read-only local Half-Life
assets, fixed synthetic 8 players/12 pickups, 1920×1080 hidden OpenGL 3.3,
48 warm-up + 192 measured frames на view/capture; без swap чи present.
`HLCLIENT_71B97_VISIBILITY=all` обов'язковий для normal no-visible-list path.
Кожен запуск містить `remote_capture=false/true`. Стандартні
`HLCLIENT_22AD_VIEW` для різних структур сцени: `f8` (майже все видиме),
`f8_yaw90` (частково видиме), `f8_yaw_sweep` (поворот),
`f093_frame3538_camera_fixed_fixture` (раніше збережений проблемний ракурс).
Останній є лише camera seed; відсутні live actors/poses/lights не
відтворюються вигаданими даними. Ще два live seeds з двох ZIP так само не є
готовим повним replay.

Однаковий GL vendor/device/version, assets, scene fingerprint, actual MSAA,
extent, pixel signature і Off world-only depth signature — обов'язкові
попередні gates. Після цього інтерлівувати variants без паралельної
компіляції, зберігати raw per-frame samples. Exact archival R tester
у збереженій збірці фактично використовує лише 8×, **ігнорує запитаний view** та друкує старий набір
семи actual scene views. Його порівняння допустиме лише за збігом
`scene.view` з тим самим actual view C0/C1C; `requested_view` не є доказом
тотожного ракурсу. Його старий fixture також не має окремого
`gpu_world_static_world` scope, тому для цього scope порівняння з exact R
недоступне. Для контролю Off/2×/4× та актуальних fixed views є
збережений перебудований baseline fixture SHA-256
`CB5FC2B599B7D66DFD92191969AD46ED8CC2B676B33E14C68BD8EB88F0CBAAA9`.
Він **RProxy, не byte-identical historical R**, і має бути окремо позначений.
За відсутності надійного R 2×/4× — не вигадувати його, виконати C0↔C1C.

Метрики: `render_submit` CPU і `gpu_world_static_world`/`gpu_total` окремо,
Studio/owned subset, p50/p95/p99/max, raw aligned source frames, draws,
triangles, binds, uploads, retained prepared bytes. GPU timestamp interval
може включати затримку подання; CPU scopes вкладені/перекриваються, їх
не можна додавати. `1000/static_world_ms` — не ігровий FPS. Fixture не
робить window swap, тому live VSync/frame pacing, borderless/windowed present,
software cap та manual parity лишаються `not_run`.

Ізольований перехід C1A→C1B за однакових scenes/AA показав зменшення
`gpu_world_static_world` p50 приблизно на 0.15–0.27 ms у fixed views Off,
2×, 4× і 8×; для Off/8× були по два повтори на трьох fixed views, для
2×/4× — один на кожному з трьох views. У кожному запуску було 192 GPU
samples без pending/drop. Color signatures усіх чотирьох режимів та Off
world-only depth signatures збіглися; MSAA depth signature fixture не
записує (`null`). Це підтримує **ізольоване прийняття B**, але C1B ще
містив відхилений A1, а отже фінальний C1C має окремо пройти той самий
парний контроль із C0/R. GPU timestamp interval може містити паузи
подання команд, не є чистим shader time і не вимірює живий FPS.

Фінальні C0↔C1C fixed-view групи: кожен запис нижче — середнє двох
інтерлівованих run-level p50; це **не** злитий percentile всіх кадрів.
`RProxy` показано для контексту, а не як byte-identical historical R.
Одиниця — ms; менший `gpu static` кращий. `f093` скорочує довгу назву
`f093_frame3538_camera_fixed_fixture`.

| AA | Actual view | RProxy GPU static p50 | C0 GPU static p50 | C1C GPU static p50 | C0−C1C, діапазон двох пар |
| --- | --- | ---: | ---: | ---: | ---: |
| Off | f8 | 4.283 | 4.276 | 4.099 | +0.162…+0.193 |
| Off | f8_yaw90 | 4.244 | 4.235 | 4.028 | +0.207…+0.207 |
| Off | f093 | 4.582 | 4.589 | 4.357 | +0.206…+0.258 |
| 2× | f8 | 4.797 | 4.827 | 4.620 | +0.198…+0.217 |
| 2× | f8_yaw90 | 4.540 | 4.538 | 4.334 | +0.194…+0.213 |
| 2× | f093 | 5.047 | 4.872 | 4.695 | +0.176…+0.177 |
| 4× | f8 | 5.365 | 5.312 | 5.163 | +0.132…+0.167 |
| 4× | f8_yaw90 | 8.211 | 5.255 | 5.134 | +0.051…+0.190 |
| 4× | f093 | 5.262 | 5.402 | 5.026 | +0.342…+0.410 |
| 8× | f8 | 10.868 | 6.628 | 6.383 | +0.175…+0.314 |
| 8× | f8_yaw90 | 6.581 | 6.133 | 5.985 | +0.043…+0.253 |
| 8× | f093 | 10.249 | 6.238 | 6.026 | +0.194…+0.229 |

В усіх 24 fixed-view парних повторах C1C має нижчий `gpu_world_static_world`
p50, від 0.043 до 0.410 ms. CPU `render_submit` p50 змінився мало і не
монотонно (середнє трьох views: Off 0.405→0.399; 2× 0.436→0.427;
4× 0.488→0.469; 8× 0.479→0.477 ms). CPU p95/p99 і GPU p95/p99/
max містять окремі великі сплески; у fixed groups **немає доказу
системного покращення tails**. Так само GPU total p50 місцями шумить,
тож покращення end-to-end frame time або live FPS не доведено. RProxy
на 4× yaw90 та 8× f8/f093 особливо нестабільний між повтореннями;
порівняння C1C з ним не заміняє прямий C1C↔historical R контроль.

Поворотний `f8_yaw_sweep`: первинний Off p50 C0 4.545/4.674 проти C1C
4.295/4.268 ms. Перший sweep для 2×/4×/8× містив одночасні великі
GPU-сплески навіть у RProxy, тому його повторили після зупинки тестових
збірок. Повторний чистий sweep, `gpu_world_static_world` p50 у порядку
трьох інтерлівованих повторів:

| AA | C0 p50 | C1C p50 | Висновок |
| --- | --- | --- | --- |
| 2× | 4.900 / 4.895 / 4.928 | 4.692 / 4.719 / 4.667 | 3/3 нижче |
| 4× | 5.408 / 5.494 / 5.479 | 5.250 / 5.277 / 5.299 | 3/3 нижче |
| 8× | 6.516 / 6.837 / 7.373 | 6.581 / 6.683 / 6.828 | 2/3 нижче; перша різниця в межах шуму |

Для всіх C0/C1C fixed і sweep пар actual AA збігся із запитаним,
GPU sample count був 192 на scene без pending/capacity drops, Off не
виділяв MSAA target, 2×/4×/8× виділяли по одному target на setup.
Color pixel signatures збіглися для всіх пар; Off world-only depth
signatures також збіглися. Для MSAA fixture depth signature=`null`, а
отже окрему рівність MSAA depth не стверджуємо. World draws і triangles
у C0 та C1C на однакових views не змінилися; зниження GPU scope не
пояснюється меншою кількістю поданих трикутників. У C1C retained
prepared-owner bytes для 272 blocks: 12 096 проти 11 008 у C0
(+1 088 bytes). Додаткову program/shader пам'ять і cold build time
fixture не вимірював; їх не прирівнюємо до нуля. Warm per-frame upload
показує 0 world/studio/sprite uploads у цих сценах.

Після останньої F8-діагностичної правки перебудовано фінальний C1C fixture
і повторено короткий контроль `c1c-final-spot`: f8, Off і 8×, два
інтерлівовані повтори C0/C1C. `gpu_world_static_world` p50 Off:
C0 4.269/4.257, C1C 4.058/4.103 ms; 8×: C0 6.272/6.271,
C1C 6.221/6.192 ms. У всіх чотирьох парах нижче, але розмір виграшу
коливається; CPU `render_submit` у 8× тут трохи вищий, а Off p95 в
одному повторі мав сплеск. Color signatures збіглися в обох capture
modes; Off depth збігся, MSAA depth fixture не записує; по 192 GPU
samples на scene, pending/drops 0. Це підтверджує той самий renderer
path після F8-правки, а не заміняє повну матрицю вище.

Actual archived R tester для 8× має повторювані actual views `f8`,
`f8_yaw90`, `f8_yaw_sweep`; `f093` він не відтворює. На парі actual `f8`
R↔C0 color signature і geometry збіглися, але GPU total p50 R
8.702–8.716 проти C0 8.649–8.862 ms, тобто стабільний R→C0
end-to-end виграш не встановлено. Для exact R немає static-world
subpass, а Off/2×/4× ним не підтримуються. C1C порівнюємо прямо з
C0 на всіх чотирьох AA, а не приписуємо RProxy історичну ідентичність.

Відтворення матриці без гри: збережені в task record `run_matrix.ps1`
і `summarize_matrix.ps1`; наприклад
з PowerShell у папці task record:
`& .\run_matrix.ps1 -Variants @('RProxy','C0','C1C') -Views @('f8','f8_yaw90','f093_frame3538_camera_fixed_fixture') -Modes @(0,8) -Repetitions 2 -RunLabel 'c1c-primary'`.
Аналогічно `-Modes @(2,4) -RunLabel 'c1c-msaa2-4'`; для sweep —
`-Views @('f8_yaw_sweep')`. Усі raw `.log` збережено поруч зі скриптами.

## Незалежний Studio випадок

AA Off run frame 10574 (76.928601 s): interval 25.5853 ms, CPU update
16.6589 ms, вкладений Studio scope 11.93 ms, render submission 8.0011 ms,
present wait 0.1993 ms, GPU total 5.631756 ms; 7 observed players /
77 entities. Parse/commit і prediction у цьому кадрі — 0. Це wall-clock
спостереження, не доказ Studio culling, allocation, shader compilation чи
OS scheduler. Повних inputs кадру немає. Збережений warmed actual-assets
Studio fixture не встановив відтворюваний 11.93 ms excess або зайву
конкретну операцію. `C_historical_cause=not_established`; незалежний
speculative Studio/prediction fix не внесено. Пізніше точне відтворення
потребуватиме окремого доказу, не переписуючи попередні A–D/P2 fixes.

## Валідація, доставка і межі висновку

Фінальна Release збірка у справжньому source checkout:
`cmake --build build --config Release --target hlclient_tests hlclient_gauss_71b97_tests hlclient_core_api_tests hlclient --parallel 4`, exit 0;
MSVC Visual Studio 2022, Win32, `/O2 /Ob2 /DNDEBUG`.
`build\bin\Release\hlclient_tests.exe --reporter compact` (несандбоксований
файловий/loopback тестовий контекст): exit 0, 4 110 cases = 4 035 pass,
75 opt-in asset/capability skips, 5 563 131 assertions pass. Окремо
`build\bin\Release\hlclient_tests.exe '[b080][capture-policy]' --reporter compact`:
24/24 cases, 19 179 assertions, exit 0; renderer filters
`'[prepared-world],[world-bind-cache],[brush-frame]'`: 3/3,
459 assertions, exit 0. Це включає synthetic prepared blocks/batching,
invalid inputs і OpenGL output controls; actual-assets гаус-fixture
окремо пройшов матрицю color/depth вище. `build\bin\Release\hlclient_core_api_tests.exe --reporter compact`:
531 pass, 1 sky-asset opt-in skip із 532 cases, 636 257 assertions, exit 0.
`pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/test_movement_session_analysis.ps1`:
347 checks, exit 0. `HLCLIENT_SMOKE_TEST_FRAMES=2` та
`build\bin\Release\hlclient.exe --renderer null`: 2 frames, exit 0.
`ctest --test-dir build -C Release -R '^hlclient_game_boundary_guard$' --output-on-failure`:
1/1 pass, exit 0.

Окремий core-only Release cache `build-core-final-22ad3a5` звірено з
тим самим source root, `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`.
`cmake --build build-core-final-22ad3a5 --config Release --target hlclient_core_api_tests hlclient --parallel 4`:
exit 0. Його `hlclient_core_api_tests.exe '[core-only][game-api]' --reporter compact`:
32/32 alternate-module cases, 668 assertions, exit 0; повний core-only
API: 531 pass / 1 opt-in skip, 636 257 assertions, exit 0. Окремі
core-only Null 2-frame smoke і architecture guard 1/1 — exit 0.

Початковий Release broad усередині filesystem/loopback sandbox дав
невідповідні реальному запуску Win32 Temp/junction/socket fixture збої;
повтор у належному контексті ізолював **два справжні** B080 failures:
після збільшення запису кадру ручне F8-вікно закінчувалося передчасно.
Перша capacity-aware правка ще губила останній кадр у часовій комірці;
друга зберігає фактичний endpoint, і весь фінальний broad suite пройшов.
Спроби й причини збережені, не зараховані як pass. Аналогічний початковий
core-only sandbox failure зник у дозволеному файловому контексті.
Debug clean-first збірка
`cmake --build build --config Debug --target hlclient_tests hlclient_gauss_71b97_tests hlclient --clean-first --parallel 4`
та фінальна інкрементальна та сама команда без `--clean-first` пройшли.
Повний Debug
`hlclient_tests.exe --reporter compact` у дозволеному файловому/loopback
контексті: exit 0, 4 110 cases = 4 035 pass і 75 opt-in skips,
5 563 112 assertions pass. Фокусні Debug перевірки: prepared blocks
1/1 (1 222 assertions), world frame 1/1 (52), renderer filters 3/3
(459), B080 24/24 (19 179), actual-assets Gauss Off/F8 1/1 (2 050;
read-only `HLCLIENT_LOCAL_GAME_ROOT=D:\DEV\HLCLIENT-RESEARCH\Half-Life`),
усі exit 0. Початкова інкрементальна Debug-збірка мала stale ABI objects
та аварійне завершення; повна чиста перебудова усунула це. Початковий
sandbox full-suite не пройшов Win32 Temp/loopback fixtures; повтор у
належному контексті пройшов. ASan build/runtime не запускався
(`not_run`), це не pass.

Фінальний source Release `hlclient.exe --version`: exit 0,
`HL Client Engine Version 0.0.1-dev Platform Windows x86`. Байтово тотожний
файл доставлено на launcher-selected path
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`:
5 555 200 bytes і SHA-256
`AB167865F27392ABBA0E59206E3648836C84F941B5BE054D29F4ABE0191972C5`
в обох місцях. Попередній delivery EXE з SHA-256
`6EF226EE86A04C4A2F948B2B9C19C806721BCD263B3C19A1259EA4D8F296B63D`
збережено в task record. Launcher source/path звірено; launcher не
перебудовувався. Offline `Start-HLClient-H2-Manual.ps1 -CheckOnly -SourceOnly`
і `scripts/test_h2_manual_launch.ps1` пройшли. `manual=not_run` до нового
повідомлення користувача.
Нових agent live/Steam/HLDS/game sessions: 0; staging/commit/push: none.

Висновок вимірювань: A1 відхилено після парного контролю; B прийнято для
static-world GPU p50 у всіх чотирьох AA режимах за fixed views,
`diagnostics_ready`; `all_modes_frame_time_gain` для повного живого кадру
**не доведено**. Historical C `not_established`, manual `not_run`.
