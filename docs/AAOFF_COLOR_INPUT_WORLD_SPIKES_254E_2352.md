# AA Off color input і дорогі static-world стани — 254e / 2352

## Статус

Фінальна офлайн-кваліфікація B6, пакування й **синхронна доставка завершені**.
25.10.2026 після відновлення доступу до мережевого диска той самий перевірений
normal EXE встановлено у локальний Release, shared destination та canonical
dist/ZIP. SHA-256 і розміри всіх 8 EXE/DLL звірено після закриття exclusive locks.
A відхилено за correctness і повним GPU-часом. Історичну причину frame12854
не встановлено. Новий manual acceptance і Windows 10/MSAA4 target — `not_run`.
Agent live/Steam-auth/HLDS/gameplay sessions = 0. Staging/commit/push = none.

Докази: `docs/evidence/AAOFF_COLOR_INPUT_WORLD_SPIKES_254E_2352.json`;
повні локальні записи, заморожені fixtures, невдалі спроби й rollback:
`manual-artifacts/task-records/aaoff-color-world-254e2352-20261025` (далі T).

## База й межі

База — актуальне dirty working tree, не старий HEAD `6a86ce0`.
Прийнятий client SHA-256:
`dfc0f490e725ad1ae8cc171a07221993173f92d23b8015419bb526b6dcbe4a9a`.
Заморожений manifest T/baseline/source-manifest.json:
`6eb099d88492d868ebab6beb4242702253d72739fc43fdf8e414b5c7b51f89ac`.
Прийнятий frame-hash fix збережено byte-for-byte:
`src/entity_render/entity_scene_render.cpp` SHA
`20b93defeba5a8d1cf029ba6d9c1c46cd31624b089887d57709880b4b7b825e2`.
Сторонні та попередні зміни не відкочувались. Чинний launcher вибирає сусідній
`hlclient.exe`; застарілі checkout paths не є точкою входу.

Build trees прив'язані до цього checkout, Win32/MSVC 14.34.31933. Normal/Debug
game module On; core-only/ASan — Off. OpenGL fixtures: Intel RaptorLake-S Mobile
Graphics Controller, GL 3.3 Core, driver 32.0.101.7026. Це НЕ ноутбук HD4600.

Вхідний ZIP перевірено за manifest; вкладені `no-msaa.zip` та `msaa-8x.zip`
прив'язані до точних Run ID `254e2739c089b2a210b14ae874b6a29b` і
`23527cf8f737b72b3ba1f76cd4b304fa`. Primary manifests/reports підтверджують
однакові client/launcher hashes та normal close. У першому 20127 framebuffer
copies, у другому 9275 direct resolved-texture adjustments і 0 copies.
Маршрути різні: це не matched AA A/B і не вимір ціни лише MSAA/copy.
Переданий `analysis/analyze.py` не виконувався.

## A — одновибірковий color target: відхилено

Реалізовано приватний прототип через чинний SceneTarget owner: texture input
для Off+adjusted, direct neutral, повторне використання attachments,
default-back compatibility guard, старий fallback, depth transfer усередині
повного GPU timer. Випробувано два attachment варіанти, зокрема packed D24S8.
Прототип і нові тести збережені в T/A2-prototype; у production їх немає.

Synthetic checks пройшли, але цього виявилось недостатньо. У реальній Crossfire
same-process old-fallback/new-target порівняння дало **1511 відмінних color pixels**,
**10448 depth pixels**, maximum channel difference 2; є depth різниці приблизно
один float ULP. Допуски не розширювались. Контракт не пройдено.

Короткий Off-adjusted screen, 1920×1080, image 0.1/1.04/1.1:
B0 whole GPU p50 3.081/3.106 ms; A1 3.686 ms; A2 3.658 ms.
Correction pass став дешевшим (~0.83→0.50 ms), але додатковий depth transfer
~0.93 ms не приховано поза таймером. Whole-path виграшу немає.
Retained attachments 16,588,800 bytes у цій роздільності, бюджет 512 MiB,
warmed allocations 0; cold render близько 170 ms включає інші ресурси/компіляцію.

Висновок A: `rejected_correctness_and_total_gpu_regression`, не `FPS_fixed`.
Чинний Off-adjusted framebuffer copy, neutral direct і прийнятий direct MSAA
resolved-texture path збережені. Відхилений neutral-MSAA direct-resolve не повертався.
AB не будується і не доставляється: комбінувати відхилену A з B немає підстав.

Ідентичності незалежних benchmark EXE (це не normal клієнт для гри):

- B0 qualification: `aa9f472bf49a212433a3e35097f773f9b4b0c1692b8648a3465c597797aa2020`.
- A1 screen: `4921e849137cf60682794c44b9262eb0fdb82da2c8331a61e817ac98e20036da`.
- A2 screen: `6ee7a2653dc25438ccb72566a06a31d173cc90aff73da17f6d35615a3c5d2689`.
- B6 qualification: `238f30aac20e603ebaea4f608cb80a0062a925d572159f4fd4a740249d6889fc`.
- AB: `not_built_A_rejected`, ідентичності немає.

Source/harness/library/EXE manifests для всіх проміжних варіантів збережені
в T/isolated-fixtures та evidence; жодний відхилений EXE не є delivery source.

## B — локалізація й зміна

Camera seed frame12854 використано буквально: eye
`[569.95849609375,458.0836486816406,-1791.98046875]`, pitch -3.5, yaw 108.69914.
Кути звірено з production `forward_from_yaw_pitch`/camera producer.
Fixture використовує справжні Crossfire assets, але синтетичні актори й світло:
8 players, 12 pickups, animation frame 32, fixed server time 46.141073.
Це НЕ historical replay 44.838276. FOV, extent, порядок геометрії та матеріалів
зафіксовані; точних historical lights/program inputs немає.

У legacy fixture `Visibility=all` означає відсутність explicit visible_draw_list,
тобто звичайний production prepared-world selector: **38 draws / 5188 triangles**.
Це не all-batch bypass. `selected` — окремий legacy per-surface PVS control,
294 draws/888 triangles. Справжній prepared/all-batch oracle перевіряється
окремими синтетичними pixel/depth tests.

B0 none/one/far/many screen: static GPU приблизно 1.82/2.46/2.45/7.03 ms;
whole GPU 3.17/3.82/3.81/8.54 ms. Вісім активних synthetic lights відтворюють
дорогий клас роботи, але не доводять причину історичного сплеску.
Warmed world/Studio/sprite uploads 0; program binds 3 і light uploads 3 на кадр.
Отже, у цьому fixed-state fixture проблема не була повторним upload/rebuild.

Відхилені незалежні спроби, усі записи залишені в T:

- B1: conservative AABB/all-zero light rejection. Допомагає far, майже не many,
  додає CPU; не прийнято.
- B2: per-range filtering. Many виграш малий, CPU і light uploads3→14 зростають.
  Pixel/depth порівняння exact, але 8 operation assertions не пройшли; це не
  позначено як passed suite. Одна помилкова build invocation з old lib
  позначена invalid і не запускалась.
- B3: per-fragment cube reject — many GPU14.66 ms; відхилено.
- B4: вісім умовних literal-index additions. Медіана краща, але pooled Off p99
  **+5.26%** при наперед заданій межі3%; відхилено, gate не послаблено.
  Короткий Off pair3 p99 також14.99→17.77 ms, не приховано.
- B5/B5b: одна full-eight uniform branch у спільному shader. Many швидший,
  але one-light screen3.71→3.97 ms (~7%); відхилено.

**B6** відокремлює full-eight GLSL у додатковий program чинного
`GpuWorldResources`. Старий 0..7 opaque-lit shader після препроцесора має
початковий loop, без нової fragment branch. Full-eight variant має literal
indices та початковий порядок додавання всіх восьми внесків.
`OpenGlRenderer::render` один раз за кадр перевіряє, що всі 8 entries валідні
й positive, за тими самими умовами, що `EffectLightUniforms::upload`.
`draw_package_range` вибирає цей program лише для звичайного opaque static-world.
Masked/sky/brush/no-light/general paths незмінні. Uniform uploads/geometry,
радіуси, інтенсивність, порядок джерел не скорочені.

Changed production functions/types: `kOpaqueLitWorldFragmentShader`,
`create_opaque_lit_world_program`, `GpuWorldResources`, `build_gpu_resources`,
`FrameDrawState`, `draw_package_range`, `OpenGlRenderer::render`.
Frozen B6 source SHA:
`0d3acc1918f085b0ece689137641e658672c2f4d84ffd47f3fa2aad0600e44b2`.
Причина на рівні source — уникнення indirect indices/count branches для
повного набору. Точну driver ISA не дизасемблювали: це inference, не факт про ISA.

Пам'ять B6: один додатковий ProgramState/GL program на підготовлений world
resource set, RAII lifetime. Нових textures/buffers/targets немає; shader strings
алокуються лише під час cold resource creation. Розмір driver binary невідомий;
не підміняється розміром GLSL. Немає нового warmed allocation або per-frame
compile/glGet/readback/logging. Prediction, network, фізика та gameplay незмінні.

## Метод приймання й результати

Спочатку exact oracle, потім short screen. B6 screen whole GPU p50:
Off many8.28→6.18 ms; MSAA4 many8.85→6.77; MSAA8 many9.30→7.21;
Off one3.72→3.75 ms (у межах3%). Це preliminary screening, не фінальний FPS.

Final frozen campaign: 2 B0/B0 controls на Off/4/8, потім 3 interleaved reversed
пари, 96 warm + 1024 measured frames/run. CPU і GPU scopes окремі; whole-GPU
включає scene→output, correction та HUD. Async query ring, bounded test-only
drain поза CPU scope, readback після timed region, без swap. Вкладені scopes
не додаються; `1000/pass_ms` не називається FPS гри.

Gate до вимірів: whole-GPU p50 gain >max(3%,2×B0/B0 p50 variability), CPU
p50 та pooled GPU p95/p99 без регресії понад ту саму відповідну noise bound.
3072 frames/variant/mode; кожну пару та maximum збережено окремо. Не дає
гарантії відсутності всіх рідкісних кадрів у грі.

B4 campaign мала runtime interruption перед останнім run (>9h). 23 completed
runs не переписано; orphan B0 збережено, останню MSAA8 пару завершено свіжим
reversed B4/B0 pair після відновлення. Деталі T/long-run-interruption.json.
Це не повторна B4 campaign до бажаного pass; B4 все одно відхилено.

Завершено рівно одну фінальну B6 campaign: 24 runs, без відкинутих timing
samples і без паралельної компіляції. Exact color/depth/light signatures збігаються.
Нижче pooled **whole-GPU** мілісекунди (3072 samples на кожну клітинку):

| AA | Варіант | p50 | p95 | p99 | max |
|---|---|---:|---:|---:|---:|
| Off | B0 | 8.260512 | 8.286564 | 8.559564 | 9.622652 |
| Off | B6 | 6.194708 | 6.237348 | 6.465472 | 7.356336 |
| 4× | B0 | 8.866260 | 8.933236 | 9.148828 | 10.050456 |
| 4× | B6 | 6.766136 | 6.829472 | 7.062640 | 8.180432 |
| 8× | B0 | 9.293752 | 9.353084 | 9.594884 | 10.506236 |
| 8× | B6 | 7.223580 | 7.341464 | 7.759596 | 9.019608 |

Медіана GPU зменшилась на 25.01% / 23.69% / 22.27%; p95/p99 також поліпшились.
Застосовані noise bounds 3% / 3% / 5.20%, отримані з B0/B0, не змінені після run.
Це виграш B-only для конкретної fixed eight-light сцени, не FPS гри чи A.

Власне static-world GPU-pass з тих самих збережених samples:

| AA | B0 p50/p95/p99/max, ms | B6 p50/p95/p99/max, ms |
|---|---|---|
| Off | 6.935552/6.940804/7.218692/8.284172 | 4.865276/4.879056/4.907188/5.997888 |
| 4× | 7.187544/7.199972/7.453004/7.831772 | 5.093764/5.110768/5.270252/6.466980 |
| 8× | 7.368244/7.380620/7.654868/8.483540 | 5.294172/5.328492/5.581628/7.065292 |

Усі інші CPU/GPU/subpass quantiles є в T/B6-all-scopes.json. Це додаткова
агрегація тих самих runs, без нового вимірювання або зміни acceptance gates.

CPU submission та ширший owned subset наведені окремо; subset включає
preparation/submission, але не весь engine update. Вкладені scopes не додаються.

| AA | CPU scope | B0 p50/p95/p99/max, ms | B6 p50/p95/p99/max, ms |
|---|---|---|---|
| Off | submit | .2530/.3286/.4089/1.1312 | .2524/.3330/.5039/.9303 |
| Off | owned subset | 1.0951/1.2737/1.7835/3.1767 | 1.0970/1.3316/2.3289/3.3142 |
| 4× | submit | .2885/.3872/.5346/1.9862 | .2739/.3844/.6204/1.1298 |
| 4× | owned subset | 1.1417/1.3701/1.9502/3.5923 | 1.1310/1.3931/2.3984/3.3961 |
| 8× | submit | .3052/.4235/.5511/1.3601 | .3016/.4236/.6833/1.1389 |
| 8× | owned subset | 1.1717/1.4148/2.0469/2.9754 | 1.1751/1.5023/2.4870/3.3518 |

**Обмеження: CPU p99 став гіршим**, хоча попередньо оголошений CPU median
non-regression gate пройдено, а GPU p95/p99 стали кращими. Не називаємо це
усуненням усіх CPU tails або загальної нерівномірності гри. CPU median зміни
не перекривають виміряний GPU виграш. Причину різниці рідкісних CPU samples
окремо не встановлено; не приписуємо її автоматично ні shader, ні ОС.

Cold render CPU (включає всі cold resources/driver compilation, не лише новий
program), по три парні runs:

| AA | B0, ms | B6, ms |
|---|---|---|
| Off | 148.6335,151.0978,146.8737 | 171.9995,721.1347,744.9192 |
| 4× | 175.1436,156.7569,177.8308 | 184.8537,722.1413,683.1864 |
| 8× | 205.0056,170.0607,708.3680 | 191.3573,724.3288,206.9727 |

Додаткова компіляція має реальну cold ціну; ці числа не ізолюють точну ціну
одного program і залежать від driver cache. Cache ОС/драйвера не очищався.
У warmed runs compile/resource uploads/target allocations не зростають.

Окрема correctness matrix: 12 B0/B6 пар, 24 runs, усі exact. Покриває Off
none/one/far/moved/expired/transient/transition, MSAA2 many, neutral Off/4/8,
legacy selected visibility. Її timings не є новою speedup campaign.
Фінальний shader oracle: 560 exact color/resolved-depth порівнянь
(35 states ×4 layouts ×4 AA), compile/link failure обох opaque-lit variants,
release/recovery; 0 failures. A diagnostic stdout окремо відновлено з frozen
EXE без GPU timing: `T/A-actual-difference/fixture.stdout.log`. Catch pass там
означає виконання діагностики, а не пройдений A-equivalence gate.

## Фінальна якість і доставка

Normal Release зібраний **один раз** з authoritative project compile options
і поточних Release libraries у приватний output. SHA-256 нового `hlclient.exe`:
`c349dc7beb24a28ed45b1d1b6ed7da2a2eed80587e4a8d0cdca8bb34489850bc`,
5,586,432 bytes. Це B-only, без прототипу A. Launcher/helper/SDL не перебудовано.

Release/Debug/core-only build пройдено. Перша ASan-tree build спроба завершилася
MSVC C1902 (Program database manager mismatch). Ідентичний повтор поза
sandbox, без зміни flags/source, ручної заміни PDB або завершення чужих процесів, пройдено;
обидва receipts збережені. ASan runtime: **645 executed, 1 skipped, 0 failures**.
Skip — optional original sky assets. Це існуючий instrumented core API suite;
OpenGL renderer не входить до його ASan instrumentation/runtime coverage,
і перевіркою GL-драйвера цей результат не є.

Focused Debug: 403 executed / 6 skipped / 0 failures, 36 groups (зокрема два
CheckOnly processes). Focused Release: 409 executed / 6 skipped / 0 failures,
37 groups. Full offline Release: **4267 accounted = 4192 executed + 75 skipped,
0 failures, exit 0**, 11:04:40–11:14:36 UTC. Registration та кожен original
CTest command/property/cwd звірені; лише 13 точних client-path tokens спрямовано
на приватний candidate. Процесні opt-in environment values відновлено.
Це кількості виконань, не число унікальних
тестів між перекривними suites і не Catch assertion total.

Пропуски full-suite: explicit installed-assets/Steam inspection/audio/timing
opt-ins, capture/WFP та symlink/reparse capability cases. Вони не оголошені
passed. Потрібні actual-assets Crossfire/performance/render-equivalence fixtures
виконано окремо й прив'язано до своїх frozen identities; optional Steam/auth
та реальний WFP не вмикалися.
Спочатку доставка була pending: UNC недоступний, SMB TCP/445 — timeout 5s.
Після повідомлення користувача «Запустив» повторна перевірка встановила доступ
і точні попередні B0 hashes. Без повторної збірки виконано доставку; жодних
налаштувань мережі/системи не змінювали. Попередні pending receipts збережено.

Audited packaging завершено: dependency audit, clean Unicode folder smoke,
system-only PATH, startup-failure journaling, launcher child binding,
helper fake-child checks (112 assertions) і повторна перевірка всіх ZIP leaves — passed.
Clean-folder smoke: 12 checks, включно з очікуваними nonzero exit codes
для відсутніх assets/arguments/SDL; це не приховані failures.
Переносний пакет: 16 allowlisted files; shared profile: 17 files, без settings,
auth, logs або resources. Усі 8 shipped EXE/DLL мають однакові SHA-256/size
у portable staging, shared staging, ZIP, активних local/shared/dist.
Companions/CRT не змінено; серед бінарних файлів змінено тільки client.
Local metadata прив'язана до exact portable/shared BUILD_INFO, canonical ZIP
та незмінного pre-publication acceptance receipt.

Опублікований canonical ZIP: 4,186,577 bytes, SHA-256
`26fba5f3f5fc523023d9de78bf71c6ae28ed8d94aeb2f1f900545e14fca2f72a`.
Повні фінальні binary identities: T/post-delivery-verification.json.
Staged package/profile збережено у T/portable-stage та T/shared-stage;
canonical `dist/HLClientEngine-Win10.zip` точно збігається зі staged ZIP.

Rollback: T/baseline/Release-binaries збережений. Усі змінювані delivery leaves
скопійовано й звірено до першого overwrite під exclusive locks:
T/delivery-rollback-20261025T114649155Z-4dfd10b7/backup.
Поруч backup-receipt.json і publication-receipt.json зі статусом
`published_all_destinations_verified`. Після зняття locks окрема read-only
перевірка підтвердила всі 8 binaries в усіх трьох директоріях та canonical ZIP.
Settings/auth/resources/logs і shared test-data не читалися й не копіювалися.

Фінальний `git diff --check` — passed; staging/commit/push не виконувалися.
Незмінний pre-publication acceptance snapshot: T/acceptance-evidence.json,
SHA-256 `a4d831578ed3f3bce21a905e3aeac7d8458574ab295242154c25021295472b39`.

## Межі висновку

`historical12854_cause=not_established`. Не заявляється 400 FPS, 100 FPS,
Windows10 acceptance, matched stock-HL parity або повне усунення stutter.
Цільовий ноутбук і новий gameplay потребують нового user report після доставки.
Лаунчер/helper/SDL не потребують перебудови. Settings/resources/auth/logs
між ПК не копіюються; shared test-data не змінюється.

## Наступний ручний тест — збірка доставлена

1. Потужніший ПК: ті самі 1920×1080, AA Off, image 0.1/1.04/1.1,
   VSync Off/fps_max 0; потім окремо MSAA8, без прихованої зміни diagnostics.
2. Windows 10: колишні 1920×1080/MSAA4/neutral, окремий результат ноутбука.
3. F8 у контрольній точці; після нормального закриття — повна папка Run ID.
   Нових обов'язкових flags або датчиків немає.

Чинні точки входу (обидві доставки перевірені, client SHA `c349dc7beb24…`):

- [Локальний лаунчер](D:/DEV/CPP/HL-Client-engine/build/bin/Release/HLClientLauncher.exe).
- [Лаунчер Windows 10](//192.168.0.104/storage/HLClient-Win10-Test/HLClientLauncher.exe).

Тестувати потрібно доставлений B6; новий manual verdict залишається `not_run`
до звіту користувача.
