# MSAA 8× — зменшення вартості static-world render

> Стан цього документа: валідні офлайн A/B-вимірювання standalone opaque shader,
> контроль prepared-bypass, збірки, тести та доставка завершені. Ручний тест
> гри — **not_run**;
> прихований fixture не вимірює FPS на екрані.

## Межі роботи і baseline

Source checkout — `D:\DEV\CPP\HL-Client-engine`, `main`, entry HEAD
`1487256`. До цієї роботи вже були незакомічені зміни лаунчера, CMake і
one-click system diagnostics; цей пакет їх не переробляє і не відкочує.
Попередній normal `hlclient.exe`: SHA-256
`AB167865F27392ABBA0E59206E3648836C84F941B5BE054D29F4ABE0191972C5`,
5 555 200 bytes. Його source і delivery копії збережені в
`manual-artifacts/task-records/msaa8-render-optimization-20261022`.
Baseline renderer source SHA-256:
`3ECC688CF80D10775A3B910254EA84635F53E6A9EC5C84C6CB681B809ECEEE00`.
Фінальний renderer source SHA-256:
`97D9D6B958DF07B0D8FF5E9EA91E582B2A3D055BC4935B9B8F7B6A1E076BAD73`.

Існуючі direct MSAA resolve, no-dynamic-light world shader, prepared-world
selection/bypass, World/RX і попередні renderer-пакети вже були в baseline.
Їх не реалізовано повторно. Історичні live seeds A/B містять camera position
та кути, **не** повний RenderScene: у fixture явно фіксуються 8 synthetic
players, 12 pickups, 0 effects/decals/dynamic lights, однакові animation
frame/time/assets. Отже наведені часи не можна переносити безпосередньо
на довільний live матч.

## Production hot path і зміна

Normal `OpenGlRenderer::render` раніше вибирав загальний
`create_world_program(true)` навіть для звичайного opaque static-world
матеріалу без динамічних вогнів. Цей GLSL передавав world-position varying
і містив умовні гілки masked alpha та sky (dynamic-light код уже був
вимкнений окремим no-light variant), хоча в
даному матеріалі вони не потрібні. Нова окрема програма
`create_opaque_unlit_world_program()` використовує лише position,
base/lightmap UV та MVP; фрагмент залишає ту саму формулу
`vec4(base.rgb * lightmap.rgb, base.a)` і ті самі base/lightmap samplers.
Вона не змінює mip/filter, UV, lightmap, alpha, gamma, MSAA sample count,
draw geometry, PVS чи collision.

`build_gpu_resources()` створює цю програму лише для static-world package,
де вже готується no-dynamic-light variant. У `draw_package_range()` вона
вибирається тільки при **відсутності** позитивного transient light і effect
lights, opaque alpha mode та `!sky_surface`. Masked/sky, динамічно освітлені
поверхні та runtime brushes ідуть колишнім shader path. Також не передаються
camera/model/masked/sky uniforms, яких нова програма не має; лічильники
відображають фактичні GL uploads. Додаткова GL program — одна на відповідний
GPU world resource; shader objects після link звільняються. Compile/link
відбувається в `build_gpu_resources()` під час першого resource use, а не
кожен кадр. Це **може** припасти на перший ігровий render: cold duration і
retained driver memory окремо не виміряні, тому твердження про відсутність
будь-якого першокадрового spike тут немає.

Перший варіант, який змінював існуючий загальний shader source, падав на
першому render після коректної перелінковки. Його відхилено; production
варіант має власні GLSL strings і не змінює старий `create_world_program()`.
Початкові матриці `matrix-shader*` та `bypass-control-0*` були **невалідні
як A/B**: fixture перелінковано без оновлення renderer library. Їх збережено
як невдалі спроби і повністю виключено з чисел нижче. Після окремого build
renderer library новий standalone fixture успішно відрендерив перший кадр.

## Відтворюване вимірювання

Однакові read-only Half-Life assets, `HLCLIENT_71B97_VISIBILITY=all`,
1920×1080 hidden OpenGL 3.3 context, **actual** AA Off/2×/4×/8×; 48 warm-up
і 192 виміряні frames на local/remote capture кожного запуску, без swap.
Hardware в усіх матрицях:
`Intel | Intel(R) RaptorLake-S Mobile Graphics Controller |
3.3.0 - Build 32.0.101.7026`.
Baseline fixture SHA-256
`2164AAEEF1020B8FE65EE5A1479879190C6CA894178F4018A2BCE19A201AD732`;
валідний candidate fixture SHA-256
`93996BCFED7C7EF4AD974FE876B6C2F2E2DC1E4CEBB3BA9486F491E00253E652`.
Після видалення тимчасового prepared-mode override фінальний fixture
перебудовано (SHA-256
`1147F063A3A1673C76C15FD52055D642ED8C698023CF49B4F83AD3BBBB5ACE09`)
і повторено smoke A/B при actual 8× з тими самими pixel signatures.
Прогони interleaved baseline/candidate, без паралельної компіляції.
Percentiles — nearest-rank із 192 сирих per-frame значень, не верхні межі
гістограм. Повний машинозчитуваний перелік 56 пар за view/repeat/capture,
кожен p50/p95/p99/max GPU static world, GPU total та CPU
submission/preparation — у
[`MSAA8_RENDER_COST_OPTIMIZATION.json`](evidence/MSAA8_RENDER_COST_OPTIMIZATION.json).
Сирі stdout та manifests у
`manual-artifacts/task-records/msaa8-render-optimization-20261022/matrix-standalone-*`.

Для основного 8× local capture наведено мс як `p50/p95/p99/max`.
Повтори не усереднено: помітна міжпрогонна мінливість baseline хвостів.

| View / повтор | GPU static world baseline → candidate | GPU total baseline → candidate |
| --- | --- | --- |
| A / 1 | 6.729/14.509/18.179/22.135 → 2.983/8.654/9.730/12.353 | 14.221/19.584/23.665/24.872 → 5.812/12.140/16.098/16.152 |
| A / 2 | 6.474/12.868/17.580/18.799 → 2.855/8.209/11.293/12.942 | 9.753/17.415/20.148/21.305 → 5.361/11.190/14.609/15.374 |
| B / 1 | 11.144/26.117/32.672/58.806 → 2.771/7.930/10.036/10.927 | 15.235/28.859/35.588/61.688 → 5.684/11.383/13.680/16.857 |
| B / 2 | 6.823/20.484/26.019/26.534 → 2.732/8.081/10.953/11.427 | 12.352/25.586/29.440/33.389 → 5.528/11.390/14.099/14.376 |

GPU static-world p50 зменшився в усіх 10 локальних 8× парах (A/B і
додаткові views): діапазон відносного виграшу 37.2–75.1%; GPU total p50 —
23.8–62.7%. p95 і p99 GPU static-world/total також нижчі в цих парах.
Це elapsed GPU timestamp scopes, **не** чистий час виконання shader і не
прогноз `1000 / static_world_ms` для FPS.

CPU `render_submit` на основних A/B повторах p50:
`0.882→0.789`, `0.792→0.651`, `0.668→0.574`, але B/2
`0.530→0.548` мс. `studio_prepare` та `owned_subset` — змішані,
переважно невеликі зміни; **стійкого CPU preparation speedup не доведено**.
Сфери CPU/GPU можуть перекриватися та не складаються в frame total.

## AA regressions, якість і хвости

У парних Off/2×/4× A/B (два повтори) GPU static-world p50 зменшився
відповідно на 47.8–60.5%, 52.3–61.2%, 54.6–64.2%;
GPU total p50 — на 35.0–45.1%, 38.0–44.3%, 39.4–48.3%.
Окремий контроль `f8` (відкритий ракурс), `f8_yaw_sweep` (поворот)
і `sloped_surface_not_verified_shotgun` також має нижчий p50 GPU static
world у Off та 8×. Назва останнього view **не** підтверджує, що це саме
Shotgun ramp. У всіх 56 paired local/remote captures точно збігаються
pixel signatures і fixed-scene/draw/triangle gates; actual AA дорівнює
запитаному. При Off точно збігається також depth signature. У MSAA
depth signature fixture недоступна, тож окрему depth-парність 2×/4×/8×
не заявлено. Masked/sky/dynamic-light correctness перевіряється focused
renderer tests, а не цією zero-dynamic-light timing сценою.

Хвости не однорідні: в одному 4× B/повтор 2 `gpu_total` p99/max
`10.068/10.131→13.714/15.643` мс, хоча p50/p95 стали меншими;
`static_world` max `7.793→12.924`. На sloped 8×/повтор 2
`gpu_total` max `13.574→13.824` та static-world max
`8.708→10.176` мс. Це збережені, не викинуті samples. Вони не
підтверджують систематичну регресію median, але й не дозволяють
обіцяти кращий **кожен** frame. CPU `studio_prepare` p50 на Off/2×
частіше дещо більший; джерело цього дрейфу не встановлено.

## Незалежний контроль prepared-bypass

Тимчасовий **test-only** override примусив auto, full selection і full
bypass у B та відкритому `f8`, при 8× і Off; 18 запусків, 192 timed
frames у кожному. Він має бути відсутній у фінальному renderer source.
У B automatic обирає bypass: 13 824 draws / 1 924 992 triangles;
forced selection — 14 400 draws / 1 848 000 triangles. Колір збігається
точно, а в Off і глибина. B 8× `render_submit` p50/p95 у двох
повторах: auto `0.724/1.357`, `0.548/0.939` мс; forced selection
`1.170/3.882`, `0.635/1.322` мс. GPU total p50 auto
`5.896/5.518`, forced selection `6.138/5.700` мс.
`f8` не показав повторюваного GPU-total виграшу від примусової
selection. Тому 16-probe heuristic залишено; менше triangles саме по собі
не означає дешевший кадр. Окремий CPU selection/preparation receipt у
цьому non-diagnostic fixture недоступний; `render_submit` його включає.
Повні p50/p95/p99/max у збережених `genuine-control-*.log`.

## Верифікація і незакриті обмеження

Focused Release renderer suite: 56/56 cases, 23 641/23 641 assertions.
Додаткова Release suite (prepared/sky/MSAA/P1/P6/weapon/light): 33 cases
passed, 1 opt-in asset case skipped, 1 230 949 assertions passed. Debug
renderer suite: 78 cases passed, 1 opt-in asset case skipped, 1 253 729
assertions passed. Ці тести охопили masked, sky, FBO/resize/fallback,
MSAA й динамічне світло. Один суто counter-expectation тест оновлено після
додаткового shader switch і перевірено повторно.

Повна final offline Release suite у звичайній sandbox ізоляції спершу мала
155 filesystem/loopback failures (лог збережено). Повтор із host-доступом
до локальних тимчасових файлів і loopback завершився: **4 237 total, 0 failed,
75 skipped, 4 162 passed**; логи обох запусків збережено в task-records.
Core-only API/architecture — 2/2 passed після окремого повтору з
host-доступом; launcher offline UI — 1/1 passed; no-stock fake-runner,
CheckOnly SourceOnly і full CheckOnly — exit 0, process launches = 0.
Відповідні логи збережено в task-records. ASan відокремлюється як
`not_run/unavailable`, а не `pass`.

Фінальний normal `hlclient.exe` у source checkout і каталозі доставки має
однакові SHA-256
`044271F1B7573AD6574C297EC5B5EEA2373ECA640640FD888A87E9495133A13C`
та 5 558 272 bytes. Попередній доставлений EXE залишився у task-records
як rollback (SHA-256 `AB167865F27392ABBA0E59206E3648836C84F941B5BE054D29F4ABE0191972C5`).
Launcher і helper не перебудовували та не копіювали.

Немає нових live/Steam/HLDS-сесій, змін сервера, системної діагностики,
зниження 8×, commit або push. Ручний контроль через звичний launcher
потрібний лише після доставки, як короткий MSAA8 gameplay check; зараз
`manual=not_run`.
