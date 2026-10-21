# 51BEB30E — статичний світ і загальний кадр

Статус цього документа: offline Release-перевірка й доставка завершені.
Production-кандидат G2 пройшов перевірку зображення в actual-assets fixture,
але повторні вимірювання часу не доводять сталого прискорення. Нове ручне
приймання користувачем, windowed/borderless present та diagnostics Off/On —
`not_run`.

## Обсяг і походження baseline

Source checkout: `D:\DEV\CPP\HL-Client-engine`, branch `main`, HEAD
`22ad3a5bf98ff5213ea8e6992d94d07fe9a9a130` **плюс локальні World/RX
правки**. HEAD сам по собі не позначає bytes робочого baseline. Збережено
`manual-artifacts/task-records/general-frame-51beb30e-20261021/head-source.zip`
(SHA-256 `4FD91E2059653F0E274C6483879FDCC26BC04B1C8565E1493B6EE54A7A2033E5`),
окремий source overlay і baseline EXE. Baseline build/delivery `hlclient.exe`
мають однаковий SHA-256
`F97074BF4BF1131A8AD48074F9330E0BB065A21C5B1517ED4D9081B4F776FA50`.
Це також client hash історичного run `51beb30eba1678572ffa9ee25dfc1f94`;
його архів має SHA-256
`0881ded0e7de7d4ea1b7cba50a3ad45065df1911be3336e8f1aea6cd7b2f6f77`.
Run свідчить лише про повідомлене користувачем покращення плавності
(`manual_performance_improvement=passed`, `attribution=user_report`), не про
повне gameplay acceptance або паритет з оригінальним HL.
У документованій P1-гілці цього run — 17 302 direct-adjustment frames і
resolved-texture reads, але 0 output-color blits/copies; вже прибраний
round-trip не враховується повторно як новий G2/G3 виграш.

Поточний candidate не прирівнюється до чистого HEAD: він містить dirty overlay.
Для відтворюваної часткової source identity п'ять production-файлів G2
відсортовано за repo-relative шляхом із `/`; для кожного взято SHA-256 bytes.
Маніфест — UTF-8 **без BOM**, рядки `path<TAB>UPPERCASE_SHA256<LF>`;
SHA-256 конкатенації =
`9BBB94095CD68630FCD1BC76F8AA06BCFAB810D5E48DF3417C20FA3B296B6700`.
Це hash **лише цих п'яти файлів**, не всього checkout:

| G2 production source | SHA-256 |
| --- | --- |
| `include/hlclient/renderer/opengl/opengl_renderer.hpp` | `DE92DD6D7908D6E2A42A6578FBE6C0EE6CFF656CB2D711D11BFF8BC7F49C2F2A` |
| `include/hlclient/world_render/world_render_types.hpp` | `2101E5B282B771879CFC2B12D9760AEC832CEBE31E8A95E71DAC70236C68364A` |
| `src/renderer/opengl/opengl_renderer.cpp` | `AB86D656D8C699AAB6B2B953652FFB306A06E33746BED265776DB9AE2F01FA99` |
| `src/world_render/world_render_package_builder.cpp` | `B8F9BDD80766E0A5D9DAC356FD57B901EBCC23A8DD258765EBE426ACAF87B7AE` |
| `src/world_render/world_render_types.cpp` | `AD79C8A641A92232944CE8F7C51ACD46D309E0D50ED189CC4F76726EABB63CCE` |

Контрольна збірка — Visual Studio 17 2022, Win32, Release (`/O2 /Ob2 /DNDEBUG`).
Actual-assets fixture використовує Crossfire, прихований OpenGL 3.3 контекст
Intel RaptorLake-S Mobile Graphics Controller, driver `32.0.101.7026`,
1920×1080, без swap/present. У кожній із 9 камер є два capture-режими;
на групу — 192 прогріті вимірювані кадри. Час сервера, анімація і synthetic
actors фіксовані (8 players, 12 pickups); effects/decals/dynamic lights = 0.
Це **не** контрольований FPS реального матчу, не VSync/present test і не
повний replay історичних actors. Окрему cold-preparation latency не ізольовано.

## G1 — фактичний шлях і відхилений старий selector

Normal application передає `scene_package` зі статичним `WorldRenderPackage`,
але не `visible_draw_list`; тому рендерився шлях повних material batches.
Старий `RuntimeWorldVisibility` живе у fixture/допоміжному шляху, не є
доведеним production fix. Історично він дав mismatch 10/14 груп і
yaw-sweep visibility CPU median ≈5,23 ms; одноразове GPU 10,20→7,76 ms не
було прийнятим виграшем.

У розширеному Off-контролі 9 камер × 2 capture старий selected path проти
оригінального all-world відрізняється за RGBA у **12/18** груп і за depth у
**10/18**. Readback нового тестового негативного контролю:

| Камера | Інші RGBA пікселі | Інші depth пікселі |
| --- | ---: | ---: |
| F8 | 163 | 10 098 |
| F8 yaw +90° | 207 | 1 973 |
| F8 yaw +180° | 0 | 0 |
| Approach 128 | 959 584 | 959 576 |
| Adjacent 128 | 556 | 6 203 |
| F8 yaw sweep | 283 | 14 132 |
| Frame 3538 fixed camera | 5 | 0 |
| F8 marker fixed camera | 0 | 0 |
| Sloped surface control | 0 | 0 |

Для кожної камери два capture-режими дали ті самі наведені counts. Frame 3538
доводить, що **принаймні той** mismatch може виникнути при незмінному depth
(5 color-only pixels, можливі equal-depth/order ties); решту різниць не можна
пояснювати однією неперевіреною причиною. Старий selector не ввімкнено в
normal path. Поза цим контролем точна причина кожного старого пікселя лишається
`not_established`.

## G2 — підготовлений owner і консервативний вибір

`WorldRenderPackageBuilder::build` один раз, після валідації render buffers,
готує immutable `WorldPreparedDrawBatch/Block`: до 16 суміжних source
surfaces у блоці, AABB за фактичними indexed vertices, точні index ranges і
порядок material batches. Підготовка обмежена загальним CPU-byte budget і
транзакційна: переповнення, брак бюджету або невалідний range залишають
незмінений шлях повних batches; частковий owner не публікується. Життя даних
прив'язане до `WorldRenderPackage`/resource revision, не до камери; після
заміни пакета чи reset старі blocks не використовуються. На Crossfire збережено
272 blocks / 11 008 bytes підготовлених vectors; startup high-water і
окремий cold-time measurement — `not_measured`.

`OpenGlRenderer::render` бере final view-projection matrix того самого кадру.
Підозрілий bounds лишається в рендері (консервативний запас на похибку 1 unit
плюс float roundoff). Спочатку не більш як 16 детермінованих рівномірних
проб: якщо менш ніж дві точно поза frustum, малюються первинні batches.
За докладного відбору contiguous surviving blocks зливаються в початковому
index/material порядку; понад чотири фрагменти на batch повертають повний
batch. Немає per-frame сортування, нового GPU upload, локального PVS або
тимчасового стану між кадрами. `visible_draw_list`, sky, dynamic brushes,
decals/effects і all-world oracle не переписані. PVS restriction лишається
`deferred`: correctness старого helper не підтверджено.

У поточному actual-assets fixture для кожного з Off/2×/4×/8× кандидата
збіглися з первинним all-world RGBA signatures **18/18**; Off також
world-only default depth signatures **18/18**. Це 9 камер, включно з F8,
поворотами, approach/adjacent, frame3538, marker і sloped control, помножені
на два capture-режими. Запитане AA відповідає фактичному 0/2/4/8. Synthetic
OpenGL integration окремо порівнює повні RGBA **і depth buffers** із
оригінальною no-scene-package гілкою на трьох розмірах (96², 48², 96×48),
п'яти варіантах projection/FOV/near/far, двох позиціях і трьох layouts:
33/288 surfaces та двоматеріальний пакет.
Hash/signature контроль actual assets не є доказом повного live gameplay.

## G5 — операції для всіх AA і час без надмірних висновків

Наведені counts сумуються по **18 групах × 192 кадри** кожного режиму,
з однаковими fixed scene/actors/extent. Зміна кількості submitted трикутників
не дорівнює зміні реально зафарбованих пікселів або GPU time.

| Фактичний AA | RGBA групи | Off depth | World draws baseline→candidate | World triangles baseline→candidate | Base binds baseline→candidate |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off | 18/18 | 18/18 | 248 832→178 176 | 34 649 856→22 663 296 | 248 832→168 960 |
| 2× | 18/18 | n/a | 248 832→178 176 | 34 649 856→22 663 296 | 248 832→168 960 |
| 4× | 18/18 | n/a | 248 832→178 176 | 34 649 856→22 663 296 | 248 832→168 960 |
| 8× | 18/18 | n/a | 248 832→178 176 | 34 649 856→22 663 296 | 248 832→168 960 |

Для кожного AA lightmap binds = 10 368→10 368, material uniform uploads =
24 192→24 192, Studio draws = 439 410→439 410, timed world/studio uploads і
target allocations = 0. Кандидат має 178 944 selected-block і 343 296
rejected-block counts; 110 592 full-batch fallbacks відповідають чотирьом із
дев'яти near-all-visible ракурсів у двох capture-режимах. Це зменшує draws
на 28,4%, triangles на 34,6% і base binds на 32,1% у **цій** матриці;
не гарантує зменшення кадру в кожній камері.

Для F8 зроблено послідовність B1/C1/C2/B2 в кожному AA, без паралельної
компіляції; у клітинках — **діапазон двох окремих p50**, мс, не pooled
percentile чи confidence interval:

| AA | CPU render-submit baseline / candidate | GPU static-world baseline / candidate |
| --- | --- | --- |
| Off | 0,527–0,544 / 0,568–0,581 | 4,230–4,231 / 4,234–4,245 |
| 2× | 0,599–0,811 / 0,650–0,671 | 4,700–4,733 / 4,708–4,716 |
| 4× | 0,880–0,905 / 0,757–1,173 | 5,191–5,191 / 5,179–5,201 |
| 8× | 0,724–0,909 / 0,796–0,801 | 6,127–6,305 / 6,194–6,208 |

У Off F8 кандидат має **більший** CPU submit p50 попри менші aggregate
operation counts; там adaptive path подає повні batches і працює тільки
коротка probe. GPU p50 розходиться мало відносно міжпрогінного шуму. У 4×
кандидатські CPU p50 розходяться між собою на 0,415 ms. У 8× два candidate
GPU-total max ≈14,16 ms проти baseline max ≈10,08 ms; без причинного
відтворення це tail-ризик, а не підстава відкинути outlier. Повні p95/p99/max,
GPU total/other scopes і raw scene rows залишено у
`manual-artifacts/task-records/general-frame-51beb30e-20261021/adaptive-abba-*.log`.
Додатковий 4× yaw-sweep B1/C1/C2/B2 теж змішаний. З цих вимірювань
`robust_frame_time_speedup=not_established`, а AA Off performance gate ще не
пройдено. Hidden/no-swap fixture не дає live FPS чи present latency.
У Off F8 ширший CPU `owned_subset` p50 baseline 1,569–1,603 ms проти
candidate 1,696–1,738 ms; це окремий сигнал overhead, який не можна
замовчати лише через менші draws.

## G3, G4 і незакриті gates

- **G3 — `already_fixed` / `not_established; deferred`:** попередній World/RX
  binding/material reuse збережено; у candidate `draw_package_range` є вузький
  frame-local GL state reuse, а GPU world розкладено асинхронними timestamp
  scopes на target/sky/static/brush. Окремий shader bottleneck і сталий
  static-world GPU win не доведено, тому спеціалізованого shader variant,
  light-filtering чи нового GL capability не додано. Зниження binds не слід
  приписувати окремому G3 speedup без ізольованого контролю.
- **G4 — `not_established; deferred`:** Studio/update costs помітні, але немає
  відокремленого профілю повторної незалежної CPU-операції; speculative
  RX/animation/command cache не додано. Наявні A–D/P1/P2/P3/P5/P6 та
  World/RX контракти не відкотилися.
- **G5 — `in_progress`:** offline actual-assets correctness для чотирьох AA
  пройдено; це не фінальний performance/production acceptance. Focused
  Release після доданих projection/aspect тестів — **1 501 assertions / 2
  cases passed**. Повний Release CTest без sandbox-обмежень: exit 0, JUnit
  **4 208 cases, 0 failures, 75 skips, 205 s**. Ще 23 раніше пропущені
  CTest-регістрації (`Wrong/Yaw/Zero/model` prefix) запущено окремо:
  exit 0, 23/23; сумарно покрито 4 231 зареєстрований тест. Для Debug
  перебудовано 12 ABI-залежних бібліотек і `hlclient_tests.exe` (exit 0):
  `[prepared-blocks],[prepared-world]` — 1 501 assertions / 2 cases,
  `[runtime-visibility]` — 71 assertions / 3 cases, усі пройшли.
  Core-only HL-disabled Release зібрав `world_render_api`, package і
  `core_api_tests` (exit 0): `[prepared-blocks]` — 1 218 assertions / 1 case,
  architecture guard CTest — 1/1; `*alternate*` — 473 assertions / 21 cases,
  exit 0, без skips. Windowed/borderless present, diagnostics Off/On і нове
  manual gameplay — `not_run` у цьому зрізі.
  Нові agent live/Steam/HLDS/game sessions = 0; staging/commit/push = none.

Normal Release `hlclient.exe` зібрано в source checkout і перевірено
`--version`; bytes = **5 547 520**, SHA-256
`6EF226EE86A04C4A2F948B2B9C19C806721BCD263B3C19A1259EA4D8F296B63D`.
Копію доставлено до фактично вибраного launcher шляху
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`;
SHA-256 збігається. Попередній delivery EXE з SHA-256
`F97074BF4BF1131A8AD48074F9330E0BB065A21C5B1517ED4D9081B4F776FA50`
залишається в backup. Після доставки пройшли source-only `CheckOnly`,
no-stock manual-launch **script** (exit 0) і launcher `--offline-ui-check`
(exit 0). Скрипт не є ручним gameplay-прийманням.

Перший restricted-sandbox прогін CTest мав 154 failures через заборонені
файлові операції/loopback; його збережено як provenance, а не видано за
production-regression. Повний unsandboxed повтор наведений вище пройшов.

Підсумковий verdict: **pixel-safe G2 candidate з доведеним зменшенням
операцій у fixture; загальний виграш часу/FPS наразі не встановлено**.
Фінальна передача має спершу закрити AA Off/tail performance gate і
перезапустити gates після останньої production-правки. Нова збірка не є
«протестована користувачем», доки не буде його нового звіту.
