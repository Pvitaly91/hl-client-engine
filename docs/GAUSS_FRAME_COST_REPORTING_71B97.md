# 71B97 — вартість сцени Crossfire та пауза reporting

Статус документа: фінальні offline gates та performance-вимірювання завершені.
Normal Release-збірка готова; новий ручний gameplay acceptance — `not_run`.

## Окремі висновки

| Напрям | Поточний verdict |
| --- | --- |
| `reporting_stall_root_cause` | Точний виклик, який спричинив історичні 180.9471 ms, `not_established`. Доведено незалежну зайву роботу: OS/GL metadata та форматування observation keys на кожному кадрі. |
| `reporting_fix_status` | Gate/cache та appearance gate реалізовано; focused Debug/Release/ASan PASS. Stable metadata benchmark: 1,024 refreshes → 0. Змінені metadata досі можуть вимагати синхронного OS write. |
| `gauss_scene_cpu_cause/fix` | Історичний frame4674 не відтворений причинно. Прибрано зайві appearance wrapper/source-name copies та lowercase texture-name copies; correctness PASS, загальний CPU timing win не доведено. |
| `gauss_scene_gpu_cause/fix` | PVS/frustum candidate відхилено: moving-camera CPU regression і pixel mismatch. All-world production path збережений; frame-local binding cache: 72→1 повторних bindings, 144→2 matrix uploads на F8/frame. Зображення 14/14 груп збігається, загальний FPS/GPU speedup не доведено. |
| `present_wait_causality` | `not_established`: swap wait не розрізняє GPU queue, driver, synchronization і compositor. |
| `historical_replay_scope` | Actual read-only Crossfire та записана камера; реконструйована/synthetic сцена не є повним replay реальних actors кадру. |
| `manual_status` | Нова збірка `not_run`; agent live/Steam/HLDS sessions = 0. |

## Робоча копія та безпека

Repository `Pvitaly91/hl-client-engine`, authoritative worktree
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`. Початковий HEAD:
`a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`.
Це Git identity, **не identity нової dirty-local збірки**.

Збережено наявні незакомічені E33, graphics, movement та performance правки.
Перед змінами створений hash-verified rollback: 1,457 source files та п'ять
бінарних файлів у
`manual-artifacts/task-records/gauss-reporting-71b97-20261017/before` і
`bin-before`; manifests — `source-before.csv`, `binary-before.csv`.
Original run та вкладення обліковані у `preserved-inputs-before.csv`.

Без staging/commit/push/reset/clean/restore/stash/rebase/amend/branch switch.
Без змін сервера, bot count/config, Steam, maps/assets, network rates,
firewall чи saved launcher settings. Agent не запускає live-сесію.
Offline/fake-peer/actual-assets fixtures не замінюють ручного acceptance.

Початковий normal client:
`5301A7AAF3B7A50E62DB0DD53E659455E17118044B9CA272DE1AC876B2DC8C59`.
Launcher:
`4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688`.
SDL DLL:
`AD833F09FAD8D99EADFDB1A765A76F9F20872A3AB6FADBEF648F09334F2FAC87`.
Фінальні binary identities наведені в розділі передачі; hash audit inputs/
assets/settings/source scope — PASS.

## Оригінальні докази

Run `71b97df8a0d03e564f868391d2b6f3a1`:
`C:\Users\admin\AppData\Local\HLClientEngine\runs\71b97df8a0d03e564f868391d2b6f3a1`.
25 файлів; normal exit 0, `application_closed`,
`server_disconnect_confirmed`, primary/cleanup failure `none`.

ZIP SHA-256:
`627E10AD230648053724E478483404853BDFA1FA63FD011B21C813D3236695CD`.
`external-movement.json` SHA-256:
`8213F2B8E3D775188E81498ED142EAC6CD7AD2D609D3B3EF443DA04162411FD5`.
`external-video-final.json` SHA-256:
`6237762319010884EAEF36D25495524DFB09502E23CB04894135B1ACD3847324`.
Original reports залишаються незмінними; похідний локальний audit —
`manual-artifacts/task-records/gauss-reporting-71b97-20261017/original-audit.json`.

Endpoint саме цього історичного run: `192.168.0.102:27016`; карта
`maps/crossfire.bsp`, game `valve`, protocol48, maxclients16.
Збережена поточна адреса користувача не перезаписується.
Maxclients та observed_players не встановлюють кількість ботів.

Фактичні settings: 1920×1080 borderless, MSAA8 без fallback; VSync On,
actual swap interval1; OS refresh144Hz; VRR unknown; software cap0,
sleep_calls0, limiter wait0. Brightness0.1, gamma1.04, contrast1.1,
volume41%. Scene/resolve/postprocess — 1920×1080, storage allocations1.
Це OS/driver reports, не вимірювання фізичного scanout.

### F8 і окрема рання пауза

F8 — 46.181597 s, frame4768, incident ordinal4, `incidents[0]`.
Delivered window: 42.183726–47.174379 s; 350 retained samples,
29 background omissions, exact/encoded omissions0. Detail проріджений;
його середнє не замінює повний FPS summary.

Presented eye `[-983.0078125,712.1748657226562,-1472]`, final angles
`[-0.4,-36.000854,0]`, render sample time46.141073 s.
«Біля Gauss» — user-reported location, не доведена причина саме Gauss-моделі.
Рампа Shotgun — окремий secondary контроль, у цьому run проблема там не
була підтверджена.

| Кадр | Frame interval | CPU update | Parse/commit | Studio | Render submit | Present | Log/report | GPU interval |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4768, F8 | 20.9006 | 4.4262 | 0 | 2.9449 | 2.3249 | 13.822 | 0.258 | 11.211252 |
| 4674, 45.040645 s | 25.5502 | 21.4883 | 4.4658 | 6.5444 | 3.5397 | 0.2234 | 0.2236 | 11.300120 |
| 998, 8.889986 s | 189.4931 | 1.4384 | — | — | 6.4117 | 0.6405 | 180.9471 | 5.821608 |

Усі числові тривалості — ms; nested/inclusive CPU scopes, GPU та present
не підсумовуються. Frame998 — controlled/alive кадр після входу в гру,
а не лише loading. GPU results прив'язані до original submitted frame ID,
не до кадру пізнішого poll.

Повні controlled/alive секундні кошики:
43.650899–44.650899:66.4695 FPS-equivalent, GPU mean13.589 ms;
44.650899–45.650899:74.928;
45.650899–46.650899:82.0325;
46.650899–47.650899:70.0294.
FPS-equivalent = 1000 / mean(frame_interval_ms), не exact HUD FPS counter.
За весь controlled/alive interval:82/4079 інтервалів понад20 ms = 2.0103%.

У F8 detail prediction active, fallback/startsolid/allsolid відсутні.
Це не доказ ідеальної фізики. Correction10.337891 units на39.809956 s
має окремий retained witness; причинний зв'язок із FPS-плато не доведений.
Hull, physics, replay/input semantics та smoothing thresholds/durations
для цього FPS task не змінюються.

Попередня локальна база «приблизно5%» — run
`1b7d438cbd1b8799139e0835b5b7162b`:124/2485 = 4.9899%, не `b0805b6d`.
Наданий b080 archive має24/9054 = 0.2651%; c6d має97/5222 = 1.8575%.
Маршрути, actors та GPU work різні, тому жодна з цих різниць не є
контрольованим before/after доказом speedup.

## P0: усунення зайвої роботи reporting

Початковий `log_report` об'єднував два main-thread scopes:
`connection_progress()` та post-render `external_render(...)`.
Перший охоплює facts/milestone/critical receipt; другий — window/display
metadata, формування observation keys та optional MSAA/video publication.
Appearance summaries формуються в CPU update **поза** цим старим timer.

Доведений механізм зайвої роботи: аргумент `window.video_state()`
обчислювався на кожному кадрі **до** внутрішньої перевірки unchanged state.
Він перераховував до32 displays та читав SDL geometry/density,
GL framebuffer attributes, refresh mode і swap policy. MSAA/video keys
також форматувалися рядками щокадру.

Тепер typed scene/HUD/window-revision gate працює до metadata gathering,
JSON і output. Стала сцена має source-derived0 refresh/sink operations.
Window cache інвалідується sole SDL event pump на geometry/fullscreen/
display/scale/reset events; чужі window events його не змінюють.
Process-unique revision відрізняє replacement window/context на reconnect.
`video_state()` залишається fresh API для startup/final receipts та direct
SDL callers; GL queries не перенесено у worker thread.

Appearance gate порівнює views усіх15 encoded fields перед owning copy/JSON.
Ліміти32 slot keys/128 observations збережені. Pose/motion/light не є
appearance key. Kill-feed `row_width` — поточний текст, не зміна HUD layout;
він не витрачає video observation budget, але залишається у final receipt.

Помилка optional video publication має явний bool/durable failure flag,
не позначається успіхом gate і не породжує повторний write щокадру.
Critical initial/final/journal writes та existing MSAA bounded retry policy
збережені. **Новий asynchronous writer не додавався:** диск не доведений
джерелом181 ms. Реальний changed-metadata OS write досі може затримати
потік; немає заяви про bounded OS write latency чи усунення всіх пауз.

Final video report тепер має cumulative count/total/max та maximum
frame/network/map identity для connection receipt, post-render aggregate,
metadata, appearance, MSAA encode/publish та video encode/write.
Nested scopes не додавати повторно. Final JSON не може містити час
власного завершального запису; video encode/write counters описують
завершені in-game observational writes.

### Pipe і focused checks

`BoundedProcessLogCapture` продовжує `ReadFile` після retention cap,
обліковує observed bytes та обрізає лише retained buffer. Історичні
observed_bytes1070788/byte_truncated не доводять backpressure.
Existing test читає≥128 KiB при retention1 KiB та перевіряє bounded
startup/terminal windows. `finish(timeout)` має caller timeout + existing
2 s cancellation grace; невдале завершення чесно повертає `WAIT_TIMEOUT`,
без необмеженого join.

Додано delayed/failing/throwing sink gate,10,000 unchanged observations,
appearance field/owning-key invalidation, HUD text/layout distinction,
SDL geometry/display/context replacement та held-writer deadline checks.
Gate/cache checks після останнього production edit пройшли в Release, Debug
та ASan: 12 cases / 44,147 assertions у кожній конфігурації. Pipe/normal-close
та reconnect acceptance у повному Release-наборі — PASS, включно з
held-writer deadline та continued drain після retention limit.

Implementation paths: `apps/hlclient/main.cpp` (`LaunchReceipts` і обидва
report scopes), `include/hlclient/app/external_observation_gate.hpp`,
`include/hlclient/platform/sdl_window.hpp`, `src/platform/sdl_window.cpp`.
Коментар до `ExternalMsaaObservationPublisher` уточнений: bounded retry
count не є гарантією bounded OS-write latency.

## P1: matched-camera fixture і межі відтворення

Fixture використовує read-only actual Crossfire package та F8 eye/angles;
сцени: F8, yaw+90/+180, approach/adjacent offsets, yaw sweep.
Time/animation phase фіксовані. Actors/pickups синтетично задані;
повний RenderScene checkpoint історичної сесії відсутній.
Current actual map:
`D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\crossfire.bsp`, SHA-256
`6222243E0839022F3041E6B9E97776CE54D0CF87D4C3810E627B8B2815B555F5`.
Це поточний read-only asset, **historical map hash невідомий**.
Current `models/playert.mdl`: 73,776 bytes, SHA-256
`EB1A0DA4BD9DAFBFB0A3C2C206BFC0D0070796E3ED27458C12539123842F21BF`.
Решта WAD/sky/model identities — `actual-assets-before.csv`; фінальне
звіряння підтверджує незмінність, а не historical asset identity.

Derived sloped-surface control не називається точною Shotgun ramp без
записаної камери/підтвердженої локалізації. Матриця all/selected та
capture On/Off має однакові camera/actors/phase/settings у парі.
Фінальні pixel eye/target/yaw/FOV/near/far та UInt64 pixel signatures
зберігаються без округлення до double.

Метрики: BSP leaf/PVS/frustum/visible surfaces, actual world/Studio draws,
triangles/material binds, uploads, pose work, dimensions/allocations;
CPU preparation окремо від submission; GPU world, Studio/viewmodel,
effects/decals, MSAA resolve, image adjustments та HUD/console.
Network entities75/observed_players9 не є фактичними draw counts.

GPU timestamps — bounded asynchronous queries з original frame identity;
немає nested elapsed queries, blocking result read на pending query,
`glFinish` чи normal-frame readback. Pending/disjoint/unsupported/invalid
не перетворюються на0 ms. Failure/reset semantics пройшли focused Release,
Debug та ASan. Pool: 8 elapsed slots та 128 timestamp query names; до16
boundaries на slot, capacity overflow має явний статус. `GL_EXT_disjoint_timer_query`
перевіряється, коли доступний; без нього застосовуються monotonic validity
checks, а не вигаданий vendor GPU-busy counter.
Runtime query failure завершує outstanding slots явними `invalid` results
зі старими frame IDs, а не залишає їх назавжди pending; context reset не
перепризначає старі результати новим кадрам. Це diagnostic contract,
а не зміна prediction чи rendering content.

Hidden fixture без swaps не відтворює VSync/compositor/present wait live
сесії. Це окремий діагностичний workload, не зміна saved settings та не
доказ нового gameplay FPS. Pixel/readback correctness checks виконуються
поза perf interval. Для normal before/all→after/all точні UInt64 pixel
signatures збігаються у14/14 view/capture групах, у кожній по3 повтори
before та after. Окремі renderer fixtures перевіряють byte equality,
moving brush/camera/light/resource invalidation. Hash match не доводить
тотожність історичному кадру з іншими actors.

### Відхилений PVS candidate — baseline-only контроль

Завершені `perf-before-1-all.log` та `perf-before-1-selected.log` — лише
по одному прогону кожного варіанта, не фінальний repeated before/after.
На F8/captureOff median GPU total зменшився10.1956→7.7584 ms, але
на yaw-sweep CPU visibility median виріс0.0001→5.2307 ms, а measured
CPU subset2.7956→8.1937 ms. Ці CPU/GPU інтервали не додаються.
Зменшення GPU work саме по собі не виправдовує такий CPU regression.

Порівняння exact UInt64 signatures при однакових фінальних camera/settings
провалилося в10/14 view/capture групах. Yaw180 та sloped-surface control
збіглися в обох capture режимах; F8, yaw90, approach, adjacent та yaw-sweep
не збіглися. Причину pixel difference цим hash не локалізовано;
цей кандидат **не отримав** pixel-equivalence acceptance і не підключається
до normal production path. Derived summary навмисно завершився nonzero
після збереження порівнянь, а не приховав невідповідності.
Відхилений helper переміщений до `tests/gauss_world_visibility_control.hpp`
та `.cpp`. `RuntimeWorldVisibility` не підключений до `apps/hlclient/main.cpp`
або production app library; він залишений лише як відтворюваний negative
performance/pixel control, а не вимкнена прихована gameplay feature.

Baseline GPU sample counts:192 у27/28 scene rows; adjacent/captureOff/all
має191 ready sample, unobserved0 і test-slot-timeout0. Відсутній результат
не підставляється як0 ms; точні per-run `n` збережені.

Actual fixture `base_model_mesh_value_bytes=0` та `appearance_owners=0`.
Actual `player.mdl` використовує shared skeletal geometry і не має
remappable `DM_Base` ranges. Тому не заявляється усунення geometry deep-copy
чи animation deep-copy на цій карті.

### Обмежені production CPU changes

`src/app/runtime_replay_local_assets.cpp::resolve_player_appearance`
тепер позичає immutable fallback лише на час синхронної перевірки palette
та cache lookup. Color-only path не робить `make_shared<ModelAsset>` перед
перевіркою, що remappable ranges взагалі відсутні. Для actual fixture
з8 color-only players це source-derived8 зайвих wrapper/control-block
allocations, source-name copies та shared-pointer retain/release на кадр,
не копіювання skeletal geometry. Cache miss із реальним remap досі створює
власника; назовні borrowed pointer не зберігається. Identity/fingerprint,
palette ranges, variant/byte budgets і зміна кольору зберігають invalidation.

`src/games/halflife/player_appearance.cpp::player_palette_ranges` порівнює
`DM_Base`/`DM_Base.bmp` ASCII без створення lowercase `std::string`.
Для source workload8 players×28 texture names =224 тимчасові name copies
на кадр прибрані. Це не224 доведені heap allocations: короткі рядки можуть
використовувати small-string storage. Contract128 textures, exact supported
names та original hue ranges160–191/192–223 збережені.

Project-owned remap regression перевіряє reused owner/pixels, зміну кольору,
повернення до старого key та очищення userinfo без canonical-state mutation.
ASCII cases охоплюють exact/mixed-case і rejected names. Actual no-DM_Base
workload та project-owned remap workload явно розділені. Фінальні measured
CPU correctness — PASS; effect за repeated fixture timing неоднозначний,
тобто немає заяви про причинне усунення історичного Studio spike.

### Мінімальний world/brush GL binding cache

`src/renderer/opengl/opengl_renderer.cpp::FrameDrawState` існує лише
в межах contiguous world/brush draw section одного `render()`.
Повторні material ranges того самого `GpuWorldResources` не повторюють
`glUseProgram`, VAO bind, sky cube bind, camera/effect-light uniforms.
Resource transition інвалідує program-local state і transform cache;
новий кадр починається з порожнього state. MVP/model matrices оновлюються
при зміні, тому рух brush, камера, світло й replacement package не стають
застарілими. Texture/material selection, draw order, ranges, triangles та
per-material alpha/lightmap state не прибираються.

Для72 world material ranges із одним resource measured counters
program/VAO/sky/camera/light operations переходять72→1 на кадр,
matrix uploads144→2. World draws72/triangles10,026 не змінилися.
Загальний timing effect неоднозначний. Ця зміна не
вмикає PVS і не приховує жодні collision/network/render entities.

## Вимірювання продуктивності

Завершено три before/all та три after/all Release runs послідовно після
warm-up, без паралельної компіляції. Джерела — `perf-before-{1,2,3}-all.log`
та `perf-after-{1,2,3}-all.log` у task record. Окремий
`perf-before-1-selected.log` — відхилений PVS контроль, не production after.
Per-run p50/p95/p99/max — nearest-rank: індекс `ceil(p*N)-1`.
Повторні результати — медіана per-run quantiles, **не** quantiles
об'єднаних samples; ranges maxima та фактичні sample counts зберігаються.

Фактичний GL adapter усіх семи fixture runs: **Intel(R) RaptorLake-S Mobile
Graphics Controller**, vendor Intel, version `3.3.0 - Build 32.0.101.7026`.
Це adapter саме offline fixture; його результати не переносяться на інший
GPU live-сесії чи заявлену RTX-конфігурацію. Усі runs: 1920×1080, MSAA8,
hidden window без swaps. На кожен view/capture — 192 measured frames/run.
GPU ready counts — 192, за винятком before1/all adjacent/captureOff:191;
unobserved frame IDs, unavailable results і test-slot wait timeouts — 0.

Normal before/all→after/all: **14/14 точних pixel comparison groups PASS**
(7 views × captureOff/On), кожна група містить усі три before і три after.
Rejected all/selected має 10/14 невідповідностей, збережених у summary.

Нижче всі значення в **ms**, формат `before → after`, округлення до чотирьох
знаків; `max` — медіана трьох per-run maxima, не pooled maximum. Повна
точність, кожен окремий run і min/max діапазони квантилів — у JSON.
`CPU subset` охоплює тільки виміряну fixture-owned роботу, не весь кадр;
Studio та render — вкладені/пов'язані scopes, їх не додавати до subset.
GPU intervals також не додаються до CPU. Capture означає diagnostic capture.

| View / capture | Scope | p50 | p95 | p99 | median(max) |
| --- | --- | ---: | ---: | ---: | ---: |
| F8 / Off | CPU Studio prepare | 1.8103 → 2.6614 | 4.4382 → 4.4143 | 5.9655 → 5.5086 | 6.0401 → 6.0805 |
| F8 / Off | CPU render submit | 1.1442 → 1.4149 | 2.6379 → 2.2695 | 3.3108 → 3.3274 | 3.7153 → 4.2849 |
| F8 / Off | CPU subset | 3.3357 → 4.3642 | 7.3064 → 6.9339 | 9.5139 → 8.5197 | 9.7885 → 10.0804 |
| F8 / Off | GPU total | 10.1375 → 10.0425 | 11.5885 → 11.4242 | 12.3450 → 11.8476 | 12.5272 → 11.9230 |
| F8 / Off | GPU world | 6.9858 → 6.8976 | 8.2954 → 8.2310 | 9.0282 → 8.5531 | 9.3325 → 8.8083 |
| F8 / On | CPU Studio prepare | 3.2106 → 2.6033 | 4.7888 → 4.6852 | 5.1249 → 5.5358 | 5.4605 → 5.5551 |
| F8 / On | CPU render submit | 1.7317 → 1.4846 | 2.5618 → 2.5884 | 3.6162 → 3.4375 | 3.7317 → 3.6800 |
| F8 / On | CPU subset | 5.3095 → 4.5627 | 7.6580 → 7.6824 | 8.9169 → 8.7884 | 10.9356 → 10.4547 |
| F8 / On | GPU total | 10.0644 → 10.1095 | 11.5401 → 11.5714 | 12.4028 → 12.3372 | 12.8890 → 12.9244 |
| F8 / On | GPU world | 6.8826 → 6.9592 | 8.2405 → 8.4051 | 8.9012 → 8.9576 | 9.2750 → 9.2376 |
| Yaw sweep / Off | CPU Studio prepare | 2.6137 → 3.0603 | 4.5049 → 4.5580 | 5.5313 → 5.0474 | 5.6607 → 5.5645 |
| Yaw sweep / Off | CPU render submit | 1.6535 → 1.7016 | 2.8238 → 2.5649 | 3.2562 → 3.1853 | 3.5281 → 3.8437 |
| Yaw sweep / Off | CPU subset | 4.5817 → 5.0144 | 7.3725 → 7.4942 | 8.4512 → 8.4676 | 8.8092 → 8.8923 |
| Yaw sweep / Off | GPU total | 10.3146 → 10.2897 | 11.9664 → 11.8150 | 12.9959 → 12.6593 | 13.7052 → 12.8507 |
| Yaw sweep / Off | GPU world | 7.0827 → 7.0911 | 8.5094 → 8.4251 | 8.9542 → 8.8021 | 9.1467 → 9.4662 |
| Yaw sweep / On | CPU Studio prepare | 1.9942 → 2.7592 | 3.8464 → 4.9270 | 4.7130 → 6.0641 | 5.1434 → 6.0720 |
| Yaw sweep / On | CPU render submit | 1.5457 → 1.6449 | 2.4761 → 2.8745 | 3.1536 → 3.6748 | 5.9120 → 4.1211 |
| Yaw sweep / On | CPU subset | 3.8288 → 4.7738 | 6.4228 → 8.1099 | 7.6454 → 9.1391 | 10.9629 → 9.5327 |
| Yaw sweep / On | GPU total | 10.2698 → 10.3784 | 11.9691 → 12.0686 | 12.8343 → 13.4514 | 13.3996 → 13.6768 |
| Yaw sweep / On | GPU world | 7.0645 → 7.1053 | 8.5305 → 8.7729 | 9.3929 → 9.7993 | 9.4158 → 10.0466 |

**Загальний CPU/GPU speedup цими числами не доведений:** частина квантилів
краща, частина гірша. Зокрема yaw-sweep/captureOn CPU subset p95 зріс
6.4228→8.1099 ms. Доведено конкретне усунення повторних операцій і збереження
pixels, а не причинне усунення історичних FPS-плато чи Studio spike.

F8 measured counters за192 кадри, однакові в усіх трьох repeats:

| Операція | Before | After |
| --- | ---: | ---: |
| World draws | 13,824 (72/frame) | 13,824 (72/frame) |
| World triangles | 1,924,992 (10,026/frame) | 1,924,992 (10,026/frame) |
| Кожен із program/VAO/sky/camera/light bind/upload counters | 13,824 | 192 |
| Transform matrix uploads | 27,648 | 384 |
| Кожен із base-texture та lightmap bind counters | 13,824 | 13,824 |
| Model binds | 49,920 | 49,920 |
| World/Studio/sprite uploads | 0 | 0 |

Actual fixture: color-only players8; appearance owners0 і model mesh value
bytes0. No-remappable-palette workload не видається за measured geometry
copy saving або за actual remap-cache reuse. Effects/decals/lights у цій
сцені0: наявність GPU pass boundaries не означає stress coverage цих ефектів.

Task-local `summarize-perf.ps1` створює `perf-summary.json` та
`perf-summary.txt`, зберігає input hashes, усі per-run rows, operation
counts, unavailable states та exact signature comparisons.
P0 metadata benchmark порівнює preserved fresh API зі stable gated path,
64 warm-up +1,024 samples у ABBA order; одиницяµs. Це не повний
historical reporting replay і не FPS promise.
ABBA blocks групуються лише в межах одного log/invocation. Ранні focused
test logs не об'єднуються з фінальним `reporting-cache-perf-final.log`.
Нульовий виміряний quantile у tiny gate path означає роздільну здатність
таймера, не фізично нульову вартість виконання.

Фінальний metadata microbenchmark: два blocks для кожного mode в ABBA order,
по1,024 samples; значення нижче — медіани per-block nearest-rank quantiles,
**µs**, не ms. Source `reporting-cache-perf-final.log`:

| Mode | p50 | p95 | p99 | median(max) | Metadata queries/block |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fresh baseline | 47.00 | 75.70 | 155.05 | 836.60 | 1,024 |
| Gated stable | 0.00 | 0.10 | 0.10 | 0.15 | 0 |

Це підтверджує відсутність metadata gathering на unchanged path, але не
визначає точний виклик історичних180.9471 ms і не гарантує latency changed
metadata/final synchronous writes. Середня вартість metadata та максимум
цього microbenchmark не підставляються замість historical reporting scope.

Опція parser `-AllowRejectedVisibilityControl` дозволяє завершити аналіз
із exit0 лише для вже відхилених cross-selection all/selected відмінностей.
Їхні failed pixel comparisons залишаються в JSON. Same-selection mismatch,
включно з normal before/all→after/all чи нестабільністю повторів, завжди
залишається помилкою. Відсутність потрібної пари не вважається pass.

Після freeze baseline додано консервативні error-isolation checks до GPU
queries. After instrumentation тому не ідентична baseline за кількістю
GL error checks: CPU submission comparison включає їхній overhead.
Це треба показати як обмеження атрибуції, а не відняти вигадану вартість
чи назвати всі differences чистим ефектом binding cache.

## Build/acceptance та історія спроб

Початковий fixture compile failure: explicit optional uint16 conversions
та missing SDL_events include у нових тестах; виправлено без послаблення
warnings. Другий compile failure: unreachable return у throwing sink
test; замінено explicit bool return type без unreachable statement.
Початкові logs збережені; retry не приховує initial failure.
Наступні fixture setup/compile retries до `build-fixture-baseline-06.log`
включали missing server-time metadata, неправильне використання error
`.message`, shake amplitude32 при дозволеному16, far-plane10 для geometry
висотою60 та хибне очікування remap owners для actual no-DM_Base моделі.
Виправлені fixture assumptions/compile issues не подаються як знайдені
production failures; усі failed attempts залишаються в task artifacts.

| Gate після останнього production edit | Результат |
| --- | --- |
| Focused Release/Debug + reporting/cache | PASS: 12 cases / 44,147 assertions кожна конфігурація; 0 skips. Pipe PASS у повному Release. |
| Matched-camera/perf/GPU/correctness | PASS normal pixel/counters: 3 before + 3 after, по13,756 assertions на invocation. Timing effect mixed; rejected PVS pixel control не accepted. |
| Movement/console/reconnect/normal close | Debug movement142/536,512; effects17/24,518; console250 passed +15 opt-in skipped, 64,444 assertions; idle PASS. Release reconnect/normal-close PASS у повному suite. |
| Core-only / alternate module / architecture | PASS: WX ON, HL1 module OFF, 45/45 CTest entries. Child aggregate482 passed +1 opt-in skipped /436,379 assertions. Alternate filter29/598, overlaps aggregate. Graph guard PASS. |
| CheckOnly/no-stock / diff-check | PASS: diff-check0, HEAD/branch unchanged; CheckOnly/SourceOnly, manual-launch fake runner та E28 receipt/close controls PASS. |
| Full final Release suite | PASS: 3,901 CTest entries = 3,828 passed +73 explicit skips, 0 failed, 466.58 s. `build_source.ps1 -Configuration Release -Parallel 2 -TestOnly` exit0, post-CTest checks PASS. |
| ASan build | PASS: Win32 Debug, MSVC14.34, final focused Gauss + movement targets, exit0. |
| ASan runtime | PASS: Gauss12/44,147 та movement142/536,512; 0 skips, no ASan findings. Це focused ASan, не весь suite. |
| Saved settings/original artifact/source scope hash verification | PASS: 31 preserved inputs, current asset manifests, 1,457 backup source hashes; no original source removed. 19 existing source files changed by this task. Final manifest includes new files. |

Debug console15 skips — approved-original-asset opt-ins, яких немає у
цьому gate. Core child skip — original sky asset opt-in. Вони не
рахуються виконаним acceptance. Actual-assets performance запуски вище
були окремими явними opt-ins, а не зміною default CTest environment.
Повний CTest73 skips включає asset/runtime/playback/WFP opt-ins та відсутні
symlink/reparse/second-volume/hidden-window-input capabilities. Вкладені
aggregate tests також можуть мати власні explicit skips (наприклад sky);
3,828 — кількість passed CTest entries, не сума всіх вкладених test cases.
Повні назви та причини збережено у `release-LastTest-final.log`; жоден
capability skip не видається за виконану перевірку. Подальші CLI version/
help/null-renderer, transport self-test, log-observation validator, G/H
summary та source-only manual-launch checks завершилися успішно.
Жодна Debug/ASan діагностична microbenchmark цифра не використовується
як порівняльний Release performance result.

Логи acceptance у `manual-artifacts/task-records/gauss-reporting-71b97-20261017/`:
`build-final-release-all.log`, `focused-release-final.log`,
`build-final-debug-focused.log`, `test-final-debug-*.log/json`,
`build-asan-final-focused.log`, `asan-final-*-runtime.log`,
`core-summary-final.txt`, `core-*-final.log`, `final-release-suite.log`.
Два незалежні read-only reviews task-local delta від `before` не знайшли
blocking regressions у reporting/cache/appearance ownership та GPU state/
error boundaries/frame identity. Попередні dirty changes цим task не
приписуються. Немає production edits після фінальної збірки/перевірок.

## Передача

Готовий normal client:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`

SHA-256: `218DD21E345C006F10D23B67AFF0CCF42EC56C8F510A46F0500EB810A1230165`.

Звичайний GUI launcher:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe`

SHA-256: `4E01A6F33DE394F716B939736E818CD6289EB83D6046B3D2FCCF6481937F7A3F`.
Launcher app source не змінений; executable перебудований разом зі
зміненою platform library. Його старий binary збережений у `bin-before`.
SDL3.dll не змінений: `AD833F09FAD8D99EADFDB1A765A76F9F20872A3AB6FADBEF648F09334F2FAC87`.

`final-audit.json` містить exact paths/hashes original inputs, current
assets, task-changed sources та binaries. Saved launcher settings SHA-256
залишився `67F37A64BF91214C189D6222C239EAD58AE3DA45C21BD748572082619295B9EC`.
Немає нового обов'язкового test executable для користувача.
Нова ручна перевірка — `not_run`.

Для ручної перевірки: звичайний GUI launcher, збережені user settings,
короткий маршрут біля Gauss із поворотами камери; Shotgun ramp окремо.
F8 лише при залишковій просадці, потім штатний вихід. Повторний повний
тест арсеналу не потрібний. Не обіцяється144/100 FPS на будь-якому hardware.
