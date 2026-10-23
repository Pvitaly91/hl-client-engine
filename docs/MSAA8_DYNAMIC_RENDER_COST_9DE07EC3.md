# 9DE07EC3 — вартість освітленого static-world render

Статус: вузьку production правку зібрано й доставлено для ручного приймання.
Доведено GPU-виграш освітленого світу; залишилися tail warnings та недоступний
ASan runtime. Це не безумовне «усі перевірки пройдено» або гарантія live FPS.
Нових live/Steam/HLDS-сесій агента — 0; manual — `not_run`.

## Baseline і збереження

Source: `D:\DEV\CPP\HL-Client-engine`, `main`, HEAD
`14872565a24d6ca29f9cd4de436d3ff269d6e3c8` **плюс наявні локальні зміни**.
Це не чистий опублікований commit. Пакет не відкочує попередні opaque,
prepared-world, P1/World-RX, launcher або portable зміни.
До правок у `manual-artifacts/task-records/msaa8-dynamic-9de07ec3-20261022`
збережено HEAD archive, binary dirty diff, overlay усіх змінених/нових source
файлів, CMakeCache, baseline client та попередній delivery client.

Actual baseline source EXE: `BA40CE25655632CD6ADA353F894DDAF82E75847BEFE13FEF3FDB15D466228817`.
Попередній delivery EXE: `044271F1B7573AD6574C297EC5B5EEA2373ECA640640FD888A87E9495133A13C`.
Вони не тотожні: source вже містив пізніші portable/CLI зміни.
Renderer baseline SHA256:
`97D9D6B958DF07B0D8FF5E9EA91E582B2A3D055BC4935B9B8F7B6A1E076BAD73`.
CMake source root звірено; лаунчер вибирає `hlclient.exe` поруч із собою.
Staging/commit/push, зміни settings/сервера/ресурсів гри не виконувалися.

## Історична пара — що доведено, а що ні

Оригінальний ZIP має очікуваний SHA256
`C247A3C847789BBB3072659C765092EFE7FC4BF835FE1F33034859F9A5F33474`.
Client hash із його manifest — identity запису, а не повторний hash EXE:
в архіві немає executable. Оригінали не змінені; sanitized extraction
збережено в task record `historical-evidence.json`.

У raw recent window є **516** кадрів 13539–14054. Наведені в завданні
332 кадри — точний піддіапазон 13539–13870: median static world
2.790216 мс, 276 нижче 4 мс, 56 понад 6 мс, max 8.615776 мс.
Primary 13749→13750 має eye `[-952.3203125,1120.1875,-1472]`,
pitch/yaw/roll `[-10.773334,-6.3167515,0]`, selected 49 draws/7097 triangles,
static-world 2.828020→7.699068 мс. Secondary 13848→13849 має той самий eye,
але yaw **−40.81675**, bypass 72 draws/10026 triangles,
static-world 7.032012→2.638116 мс.

У цих записах немає shader/variant IDs, повного ordered material/draw list,
light values/order/lifetime та uniform state. Однакові counts або sequence
не доводять тотожності цих inputs. `historical_pair_root_cause=not_established`.
Незалежне відтворення нижче — не точний replay цього матчу.
Відповідні цьому run `analysis/REPORT_UK.md`, `evidence.json`,
`source_excerpts.txt` локально не знайдено; старі однойменні звіти не використано
як докази 9DE. P1 уже працював: 14054 direct/resolve frames, 0 copies/blits.

## Доведений механізм і production зміна

Normal application передає `RenderScene`; `OpenGlRenderer::render` формує
frame-local light state. Будь-який чинний positive transient light або
непорожній effect-light span вимикав no-light opaque path **для всього світу**.
`draw_package_range` тоді використовував general shader, який містить sky
branch і masked `discard`, навіть для opaque non-sky матеріалів.
Відсутність внеску далекого світла не повертала швидкий shader.

На fixed Crossfire сцені B0 один лише перехід none→one/far→none дає
повторюваний дорогий шлях при незмінних геометрії/камері/часі. Far light
не змінює жодного пікселя, але коштує значно дорожче none. Це доведена
неефективність вибору програми, не доказ її участі в історичній парі.
Не встановлюємо конкретну внутрішню політику driver early-depth за цими
timestamp scopes.

Незалежна перевірка raw signatures: у всіх6 B0/B1×3 primary groups
world color none/far=`13226441085972519326`, one=`14528352022666488759`,
many=`4746506049246745468`; resolved depth усіх=`484747826554614294`.
Отже, one/many справді змінюють освітлення, а far — лише вибір дорогого
шляху. Transition samples перевірені поелементно:48 warm-up, далі
192 samples,16 frames/state,32 samples для кожного none/one/far/many/
moved/expired. Source-based назви program variants не видаються за
відсутній runtime program-ID receipt.

Мінімальна зміна в `src/renderer/opengl/opengl_renderer.cpp`:

- `create_opaque_lit_world_program()` готує один додатковий stable shader
  для static-world opaque non-sky матеріалів із динамічним світлом.
- Збережено original vertex transform, base/lightmap sampling, alpha та
  **дослівні** point/effect light expressions і порядок накопичення.
  Вилучено тільки непридатні для цього матеріалу sky/discard гілки.
- `build_gpu_resources()` створює програму разом із immutable resource;
  поява/рух/зникнення світла не компілюють її повторно.
- `draw_package_range()` обирає новий shader лише для підтриманого opaque
  матеріалу. Masked, sky, runtime brushes зберігають general path; старий
  no-light opaque path лишається без змін. Program switch скидає всі
  program-local material/transform caches; light uploads не пропускаються.
- Не передаються camera/sky/mask uniforms, яких нова програма не має.
  Радіуси, число/порядок/кольори світел, bounds, геометрія і MSAA незмінні.

GPU static-world aggregate та його frame attribution не перенесено:
він охоплює command section, можливі submission gaps і cold resource build,
а не чистий shader-busy time. Немає нового normal-path readback, glFinish,
JSON/disk I/O або додаткових per-draw clocks. Спекулятивне light culling,
зміни selection heuristic, prediction чи gameplay не внесено.

## Метод і межі виміру

B0 — frozen executable із actual pre-task renderer; B1 — кандидат із тим
самим harness. B0 fixture SHA256 `8076335A4038DC88295A6FAB8150133B250464F08D3CA0381F915AB242D354EB`,
B1 `36E03C2B93AE2A9F525D715443C678A9840D0D6667597DD65EC922093F639B38`;
shared harness SHA256 `9F30E39C7397D070C4FB4EE942C3CDF3DBBE3060B89C53915ED3CFE33A7E9C7A`.
Win32 MSVC Release, однакові assets read-only з дозволеного Half-Life root.
1920×1080, прихований GL3.3, без swap/present; 48 warm-up +192 measured frames.
Diagnostics Off вимірює лише CPU: GPU queries вимкнені, відсутні GPU дані
не зараховуються як нуль. GL: Intel / RaptorLake-S Mobile Graphics Controller /
3.3.0 Build 32.0.101.7026.
Актори/час/анімація фіксовані: 8 synthetic players,12 pickups.
Камери використовують production pitch-up basis; historical FOV не
записаний, тому HFOV90-at4:3 — явний fixture input, не доведений live FOV.

Світла — synthetic контроль: one використовує existing beam profile
radius192/intensity.65×.7/color[.5,.5,1]; many — 8 bounded варіантів.
Повні ordered light values збережені в `[9de-equivalence]`, послідовність
light-input hashes — у raw samples. None/one/far/many/moved/expired та
transient/transition не видаються за історичні inputs.

Runs чергуються B0/B1 зі зворотним порядком у парних повторах, без
одночасних компіляцій. Протокол шуму визначено до B1: span повторних B0
p50 або 5% B0 median (більше з них), плюс0.1мс; для p95/p99/max
ця сама формула застосовується до відповідного квантиля. Усі повтори й tails
зберігаються; це операційний noise envelope, не statistical confidence interval.
Різні GPU/CPU scopes не додаються; `1000/static_world_ms` не є FPS гри.
Readback виконується після timing loop. MSAA depth перевіряється після
GL_NEAREST resolve **власного** world-depth FBO, не порожнього default
framebuffer. Це resolved-depth equality, не порівняння кожного MSAA sample.

Reproduction: `scripts/benchmark_msaa8_dynamic.ps1` і
`scripts/summarize_msaa8_dynamic.ps1`; exact env/argv/exit/hash/raw logs
зберігаються в кожному matrix manifest. Перша спроба компіляції harness
впала через Windows macro `near`; перейменування test variable виправило
її до freeze B0. Невдалий build log збережено, не зараховано до gates.

## Вимірювання і фінальні gates

Три primary MSAA8 парні повтори: median run-level p50 static-world
none2.7877→2.7775мс (у межах шуму),one7.2003→3.3232,
far7.4300→3.3495,many13.2507→8.0849. Освітлені сцени перевищують
predeclared noise envelope. Це не обіцянка такого ж live FPS приросту.
Контрольні median run-level p50 static-world, мс (B0→B1):

| Actual AA | Без світла | Одне джерело | Вісім джерел |
|---|---:|---:|---:|
| Off | 1.977→1.970 | 5.129→2.612 | 9.635→7.276 |
| 2× | 2.100→2.105 | 5.439→2.737 | 10.068→7.470 |
| 4× | 2.247→2.280 | 6.004→2.936 | 10.664→7.682 |
| 8× | 2.788→2.778 | 7.200→3.323 | 13.251→8.085 |

У primary8 GPU total: none4.860→4.818, one9.352→5.418,
far9.911→5.485, many15.777→10.534мс. У secondary/bypass8:
static-world one7.392→3.155, many13.662→7.819,
transition7.317→3.163мс. Детермінований camera/light sweep:
Off5.151→2.620, 8×6.927→3.270мс. Це окремі matched cases, не сума scopes.

Початкова повна матриця: 92 runs /46 matched full-scene pairs;
88 world-only color/resolved-depth pairs — exact. Усі46×192 ordered
light witnesses звірено окремо, поелементно. Timing On:15360 ready samples,
0 invalid/omitted/unobserved/slot-wait-timeout. Diagnostics Off:12 runs,
2304 samples без GPU queries, exact picture controls також пройдені.

Негативні результати не вилучено: у початковій матриці31 metric/quantile
flags перевищили окремі baseline envelopes (це не31 поганих кадрів).
Серед них AA4/no-light GPU p99: world3.269→3.754мс,
total5.220→5.839мс; решта переважно CPU p99/max. Primary8 one CPU
submission p99 .838→1.162мс, none max1.091→1.618мс.
Median CPU submission — в межах шуму. Один наперед визначений повторний
набір завершено нижче; вихідні flags залишаються у JSON.

Новий synthetic `[opaque-dynamic]` тест порівнює RGBA та world-depth із
незміненою general-програмою: у reference frame test-only draw hook
пропускає всі static-world draws і залишає тільки identity runtime brush
на тих самих primitives. Кількість пропущених/виконаних draws перевірена;
кандидат не може приховати reference своїм depth. Перевіряються opaque alpha93,
lightmap/unlit/masked/sky,0/1/8lights,зміна values/order,far/boundary/invalid,
expired,resize,zoom, overlays і reset. Додатковий `[opaque-dynamic-failure]`
інжектує compile/link failures, перевіряє cleanup, transactional resource
publication, відновлення FBO/scissor/MSAA та exact recovery наступного кадру.
Final Release new tags:2199 assertions/3cases, exit0,40 exact pairs для
кожного actual AA0/2/4/8. Capability skips не означають pass режиму.

Brush alternate texture bank +0↔+A залишається на general path;
його synthetic regression `[live-brush][opengl][material]` входить у broad
renderer gate. Animated static-world material sequence не використано
як timed factor: такого selector/phase в перевіреному static-world path
не встановлено. Це не заява про timed coverage усіх анімованих матеріалів.

Cold cost: +одна linked GL program на static-world GPU resource, shader
objects звільняються після link. Перший render може включати її compile.
Driver-retained GPU program memory не доступна звичайними ownership
counters і не прирівнюється до нуля. Cold first-render CPU зберігається
окремо; warmed uploads не є cold-time виміром. Початкові матриці показали
median cold first-render CPU penalty +17–22мс; B1 max219.878мс.
Це весь перший render, не окремо shader compile. Warm world uploads0,
scene-target allocation0/1, prepared draw storage12096bytes у B0 і B1.
Вони не доводять нульову resident-memory overhead.

## Офлайн-перевірки після production правки

| Gate | Результат |
|---|---|
| Release normal client + main/renderer/core tests build | 4 targets, exit0 |
| Release new shader parity/failure/cache | 3 cases,2199 assertions,0 skips |
| Release renderer/OpenGL/video | 46 passed,3 explicit opt-in skips,6689 assertions |
| Release prepared/capacity | 29 cases,20912 assertions |
| Release core API, host | 531 passed,1 opt-in skip,636257 assertions |
| Debug builds | 4 targets,exit0 |
| Debug new shader parity/failure/cache | 3 cases,2199 assertions,0 skips |
| Debug renderer/OpenGL/video | 46 passed,3 opt-in skips,6662 assertions |
| Debug prepared/capacity | 29 cases,20912 assertions |
| Full Debug main | 4041 passed,75 skipped,5572694 assertions |
| Debug core API | 531 passed,1 skipped,636257 assertions |
| Core-only/alternate module | 32 cases,668 assertions; full API531 passed,1 skip |
| Core-only architecture / Null | 1/1; exactly2 frames,exit0 |
| Normal OpenGL idle / Null | bounded no-stock idle; exactly2 Null frames,exit0 |
| Launcher/H2/no-stock/CheckOnly | exit0, including SourceOnly and full CheckOnly |
| Portable offline/layout compatibility | exit0;11 settings visible and hit-testable |

Повний Release CTest виконав4243 registrations; **перший запуск exit8,
два failures**. Обидва незмінені тести пройшли окремий serial rerun:

- `hlclient_stock_runtime_evidence_threshold`: спостерігає timestamp каталогу
  `stock-runtime`, поки паралельний `research_manifest` створює/видаляє там
  власні fixtures. Overlap зафіксовано в log; repeat1/1,3.14s,exit0.
- `Owned loopback readiness rejects wrong process port map and source`:
  отримав timeout замість очікуваної source-mismatch відповіді; repeat1/1,
  1.63s,exit0. Така ж нестабільність parallel4 уже описана в E21.
  Точний чинник цього timeout не встановлено; ASan-конкуренцію не видаємо
  за доведену причину. Мережеву реалізацію або test deadlines не змінено.

Це не «чисті4243 pass за один запуск» і не виправлення самих fixture races.
Повні initial failures та argv/exit обох повторів збережені. Також збережено
перший sandbox core-API exit42:7 filesystem cases/8 assertions не змогли
записати свої temp fixtures; host повтор пройшов, assertions636257.
Серед75 Debug skips — explicit actual-assets/audio/WFP opt-ins та недоступні
symlink/reparse/second-volume/hidden-mouse capabilities, не skips нових GL modes.

Початковий CMake Release wrapper зупинено після зависання на File Globs:
перевірено exact owned PID/command і завершено лише його дерево.
Послідовний direct MSBuild `/m:1` успішний. Чужі й peer builds не завершувалися.
Усі невдалі/перервані спроби залишені окремо, не зараховані як pass.

## Фінальний ізольований повтор і хвости

Після всіх builds/tests виконано один наперед зафіксований host follow-up:
72 runs (Off/2/4/8 × none/one/many ×3 interleaved B0/B1 repeats) і6 far8 runs.
Перед кожним запуском host CIM перевіряв відсутність compiler/test/game
процесів; недоступний inventory зупиняє runner. Поріг шуму не змінювався.
Попередні92 runs не замінено, outliers не вилучено; повторів «до pass» немає.

Фінальна median-of-run-p50 static-world GPU, мс:

| Actual AA | None, B0→B1 | One, B0→B1 | Eight lights, B0→B1 |
|---|---:|---:|---:|
| Off | 2.004→1.995 | 5.658→2.657 | 13.911→12.051 |
| 2× | 2.128→2.130 | 6.470→2.784 | 14.931→12.007 |
| 4× | 2.313→2.305 | 7.189→2.969 | 15.491→12.343 |
| 8× | 2.854→2.871 | 11.785→3.378 | 18.005→12.899 |

Far8:11.633→3.426мс. Фінальний GPU total8: none5.380→5.122,
one14.224→6.041, many20.942→15.289, far14.376→5.969мс.
У кожному AA lit-world і GPU-total medians мають виграш понад baseline
envelope; no-light GPU та CPU submission medians залишилися в межах шуму.
Абсолютний час між початковою й фінальною серіями відрізняється; це не
причина вибирати лише кращу серію. Обидві наведені окремо. Причину зміни
host/driver поведінки не встановлено, системні налаштування не змінювалися.

Фінальний повтор додав12 flags:2 GPU та10 CPU. У сумі43 metric/quantile
flags у170 AB runs; це не43 окремих поганих кадри. Початкові AA4/no-light
GPU p99/max flags не повторилися, але залишилися такі важливі застереження:

- Off/no-light GPU p95: world6.999→7.729мс при envelope0.450;
  total8.682→9.568мс при envelope0.844. Це не median regression, але
  перевищення наперед обраного порогу не оголошуємо «просто шумом».
- AA4/no-light CPU submission p99 .765→1.187мс, envelope.210;
  AA4/many max .782→1.160мс, envelope.264.
- AA8/no-light CPU submission max .800→1.063мс, envelope.184;
  AA8/one max .862→1.326мс, envelope.430. Є також Studio/owned-subset tails.

Це unresolved tail warnings, а не доведена причинна регресія нового
шейдера; no-light GPU path і CPU Studio підготовка не змінені. Водночас
source-незмінність не дозволяє приховувати виміряні гірші значення.
Повну відсутність регресій усіх квантилів не заявляємо.

Підсумок усіх AB series:170 runs/85 paired cases;85 full-scene exact pairs,
127 world-color/resolved-depth proof pairs exact;85×192 ordered light
witnesses exact. Timing On:30336/30336 ready samples,0 invalid/omitted/
unobserved/slot-wait-timeout. Timing Off:2304 expected missing GPU samples
із12 runs. Окремі12 B0-mechanism та12 memory-only runs не додано до170AB.
Synthetic parity:160 layout/state pairs у actual Off/2/4/8 — exact.
Resolved depth equivalence не є перевіркою кожного окремого MSAA sample.

## Пам'ять і cold cost

Окрема memory-only серія:12 hidden/no-swap runs, MSAA8, timing queries Off,
none/one/many,2 paired repeats. Вона не використана як performance acceptance.
50мс polling читає OS process cumulative peak counters лише поки процес
живий:36–88 successful samples/run, без sample errors. Останній непокритий
інтервал85.617–149.563мс може містити невиміряний пік. Це observed high-water,
не гарантований lifetime maximum і не GPU-resident memory.

| Профіль | Median observed PeakWorkingSet, MiB B0→B1 |
|---|---:|
| None | 240.865→240.211 |
| One | 248.141→242.980 |
| Eight lights | 242.719→248.957 |

Many має +6.238MiB observed PeakWorkingSet та +5.902MiB PeakPagedMemory
(186.014→191.916MiB). Максимальний B1 observed PeakWorkingSet249.383MiB.
Це process totals із driver/runtime, не чистий розмір додаткової програми.
GPU-resident bytes не виміряно. Ownership доказ: +одна GL program на ресурс;
warm world geometry uploads0 і prepared storage12096bytes не змінилися.

Фінальний per-process first-render CPU median allAA50.791→52.029мс;
far8 51.777→52.228мс. Початковий +17–22мс penalty і max219.878мс збережено.
Driver cache не очищався. Це весь перший render із resource creation,
не ізольований shader compile і не гарантовано cold driver-cache тест.

## ASan — build пройшов, runtime не виконав тестів

Ізольовані fixture і core-API EXE зібрані, exit0 без warnings/errors.
Compiler tlogs підтверджують instrumented production renderer:
`/O2 /fsanitize=address /Z7`. Лише величезні test translation units у
generated vcxproj переведено на `/Od /Ob0` із тим самим ASan; source/CMake
і optimized production libraries не змінювалися.

Sandbox та один host retry з exact runtime MSVC14.34.31933 обидва завершилися
`0xC0000142` до Catch2/ASan output. Runtime cases0; sanitizer findings0
не означає sanitizer pass. Статус — **runtime_unavailable**.
PATH змінювався лише в процесі; установок або системних змін не було.
Невдала explicit14.42 configuration та дві перервані власні build спроби
збережені як failed/interrupted_not_passed, не active або passed.

## Delivery та окремі verdicts

Normal Release зібраний після останньої production правки і доставлений у
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
Source/delivery:5568000bytes, однаковий SHA256
`E044684FE00E688F555988478E83E7DEF037EC35844C05F19E7B540C45CAA32C`.
Renderer source SHA256:
`809960D8DF6B5540543B52BFD87C561D11B7C9DEF6961A072B5083E918077B52`.
Попередній delivery EXE збережений, перевірений за baseline hash вище.
Чинний launcher запускає сусідній EXE; launcher/helper/settings не змінені.
Перед заміною host process inventory не містив жодного `hlclient.exe`.
`git diff --check --ignore-submodules=all` — exit0; staging/commit/push — none.
Нових agent live/Steam/HLDS sessions —0; вхід до сервера не перевірявся.

| Verdict | Остаточний офлайн-висновок |
|---|---|
| historical_pair_root_cause | not_established: історичних shader/material/light inputs немає |
| reproduced_render_cost_mechanism | confirmed: глобальне світло переводило opaque world у general sky/discard shader |
| production_fix | implemented, built, delivered; correctness passed, ASan runtime unavailable |
| MSAA8_time_gain | виміряний lit-world і total GPU median gain у двох серіях; не live FPS claim |
| AA_Off_2x_4x_regressions | lit medians швидші, no-light/CPU medians у noise; tail warnings залишилися |
| image_depth_equivalence | exact у всіх перевірених pairs; resolved, не per-sample MSAA depth |
| manual_status | not_run, потрібен один користувацький контроль |

Ручний контроль після доставки: один короткий MSAA8 Crossfire запуск через
звичайний launcher, Gauss/рампа, кілька пострілів із видимим світлом,
F8 у контрольній точці та при залишковому ривку, штатний вихід.
До user report `manual_status=not_run`.
