# GL 3.3 sampler state — кандидат для Windows 10 / HD 4600

Виправлено три несумісні з базовим GL 3.3 запити sampler binding у normal
renderer. MSAA, P1 resolve, shaders, матеріали та якість зображення збережені.
Кандидат зібраний, перевірений офлайн і доступний у повному portable ZIP та
в погодженій мережевій папці. Після правки реальний HD 4600 ще не перевірений.

## Source та оригінальний збій

Checkout `D:\DEV\CPP\HL-Client-engine`, `main`, HEAD
`14872565a24d6ca29f9cd4de436d3ff269d6e3c8` плюс збережені локальні зміни.
Actual CMake source binding перевірений. Немає reset/clean/stash/branch switch,
staging/commit/push або змін SDK, гри, сервера, Windows чи драйвера.

Run `6c16d56c9d4ac1d2ae7515b10e345b07`, вкладений ZIP SHA256
`1115A877F26FF99044B5FE1F2A26B464D5044913F81337F3A9E755064685CACE`.
Наявний baseline EXE повторно хешований: `83DB6619D782741F6429690F5CB0CADC2475F9FF9A1E9AE3905042AA716FBD56`.
Фатальний запис: `Native scene resolve failed with invalid_enum`; exit1,
без gameplay/receipt. Windows build19045, системний GPU label HD4600.
Run config: borderless, requested1092×614, MSAA4, VSync=true, image0/1/1.
Actual extent/samples та GL vendor/version/extensions у первинному run відсутні.
Очікувані імена video/movement reports не означають, що їх було створено.

Baseline renderer source SHA256
`809960D8DF6B5540543B52BFD87C561D11B7C9DEF6961A072B5083E918077B52`.
Baseline renderer library SHA256
`03C4FEA8E47A0868706F873B144CC157F15FCC10316549D4C58749507B4F5AC4`.
До змін збережено HEAD archive, binary dirty diff, source overlay, CMakeCache,
EXE/launcher та renderer library у task-record `gl33-sampler-hd4600-20261023`.

## API та мінімальна правка

| Місце | Уже вибраний unit | Зміна читання |
|---|---|---|
| HudGlStateGuard: HUD, font, image, sky helpers | 0 | indexed → `glGetIntegerv(GL_SAMPLER_BINDING, …)` |
| Projective decal Restore | 0 | те саме |
| SkyDrawState, у тому числі порожній startup | 2 | те саме |

Ці guards уже володіють active texture selector, тому додаткового перемикання
unit для читання не потрібно. Sampler IDs, texture bindings і відновлення
original active selector у деструкторах збережені. Самі sampler objects працюють.

Базовий query визначає [ARB_sampler_objects](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/extensions/ARB/ARB_sampler_objects.txt).
Indexed sampler query додає [ARB_direct_state_access](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/extensions/ARB/ARB_direct_state_access.txt),
також core4.5. Наявність `glGetIntegeri_v` не дає права на довільний pname.
Сумісний шлях використовується на всіх GPU; blacklist і новий backend відсутні.
Мінімум лишився OpenGL 3.3 Core. Законний indexed UBO query перевірений окремо.

## Перша помилка та ранні записи

Після чистого frame-clear старий unconditional SkyDrawState міг створити
error flag; наступна перевірка після resolve лише виявляла його. Це відповідає
[контракту glGetError](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glGetError.xml).
До actual resolve тепер є startup boundary. Додані вузькі boundaries після
sampler snapshots sky/HUD/image/decal/UI та після відновлення стану.
Детальні перевірки активні тільки для перших трьох render attempts на renderer.
На подальших кадрах ці місця не роблять GL queries або string allocations.
Наявні звичайні перевірки помилок збережені; немає drain-loop чи glFinish.

Один раз на renderer у stderr із flush пишуться bounded `[opengl-startup]`:
реальні GL vendor/renderer/version/GLSL, major/minor, profile mask, advertised
indexed-query capability та обраний GL33 path. Context ID — логічний renderer
instance, прив'язаний до PID журналом запуску. Driver strings обмежені256bytes,
control/newline bytes замінені на `_`. DSA capability читається через legal GL3.3
extension enumeration, а не через відсутні в bundled GLAD extension flags.

Launcher зберігає ці записи навіть до video/movement reports; existing secret
filter і обмеження розміру збережені. Helper не змінений і не перебудований.
Дві нові native journal регресії перевіряють retained facts/fatal та точний ZIP,
відхилення password/ticket/auth/server/chat і bounded truncation.

## Перевірки та межі

Strict fixture використовує actual hidden GL context і production renderer;
GLAD dispatch seam відхиляє лише indexed SAMPLER_BINDING як pre-DSA contract,
решту запитів передає справжньому драйверу. Це не другий renderer і не
hardware-negative reproduction. Старий renderer: 1case pass,3fail/45assertions,
зокрема точний startup `Native scene resolve failed with invalid_enum`.
Після цього fixture доповнений first-error case; decal input виправлений на
straight-alpha, щоб доведено виконувався production decal draw.

| Gate | Результат |
|---|---|
| Strict Release / Debug | кожен5cases,210assertions passed |
| Startup GL journal Release / Debug | кожен2cases,44assertions passed |
| Debug renderer/video/state | 223passed,12explicit skips;2277349assertions passed |
| Full offline Release CTest | 4250 registrations,0failed,75skipped;583.66s |
| Final candidate EXE binding / no-stock / architecture / focused checks | 6/6 passed після оновлення build/bin |
| Core-only renderer build, API | API531passed,1skip;636257assertions passed |
| Core-only architecture + idle no-assets | 2/2 passed |
| Normal MSAA4 startup matrix | Release/Debug × windowed/borderless × neutral/nonneutral:8/8 exit0 |
| Clean extracted portable | 12cases passed,16file allowlist/SHA checked; helper112assertions passed |
| Shared launcher → exact new client + logs/system ZIP | passed; налаштування користувача не змінені |
| ASan | build passed; runtime `0xC0000142` до тестів, unavailable, не pass |

Actual host: Intel RaptorLake-S Mobile, GL3.3.0/driver32.0.101.7026, GLSL3.30,
profile1; DSA advertised. Тому цей host не є реальним pre-DSA HD4600 контролем.
Strict tests: active unit5, різні nonzero samplers0/2, normal/exception restoration,
startup, sky, HUD, font/image, correction, projective decal, lawful UBO query.
Injected scalar query error на frame1 має точний sampler snapshot stage,
context/frame/1280 та не називається resolve error.

Full renderer gates охоплюють actual Off/2/4/8 за можливістю GPU, Studio,
world silhouettes, thin rails, caller FBO/read/draw/MRT, resize, neutral і
nonneutral correction, color/depth expectations та P1 paths. Нової perf/FPS
обіцянки немає. Opt-in assets/audio/reparse/input capability skips не є passes.

Збережені невдалі перевірки інфраструктури: перший isolated Debug lacked SDL
(`0xC0000135`), після копіювання власного Debug SDL CLI ще використовував стару
Debug core library (`Unknown --width`, exit2). Оновлення Debug dependencies
без production правок дало фінальну8/8 матрицю. Початкові записи не видалені.
Core-only tree не реєструє full-game focused target; замість нього перевірені
core-only renderer/API/architecture/idle, без вигаданого core strict pass.

## Доставка і приймання

Client SHA256 `B6F7A70150827E836E9A4B97EBADEC32123E70183CC78EB7A2DA3123A95CA0C1`.
Launcher SHA256 `AA152570DA99364D59A86E35B71C9DA19262918A14FEF2887B8A73CA9AE4CA9C`.
ZIP `dist/HLClientEngine-Win10.zip`,4179317bytes, SHA256
`C78BFB5B9EAF98086FA60DEB8101D2AE2567C89806F265B579C0E841F40D8834`.
Це новий кандидат, а не повторне приймання E044/83DB; packaging metadata
тепер явно розрізняє rebuilt client та його base SHA.
У ZIP немає особистих налаштувань/логів/auth або Valve assets.

Мережевий комплект також оновлений:
`\\192.168.0.104\storage\HLClient-Win10-Test\HLClientLauncher.exe`.
User test-data і launcher.json збережені;17payload SHA перевірені.
Старий portable відновлюється з `dist/archive/before-gl33-sampler-20261023`;
старі мережеві binaries — у baseline/shared-before-gl33 task record.
Scheduled tasks/служби/видалений monitor не створювалися. Live/Steam/gameplay
sessions агента0; in-game English не змінений.

Ручне приймання: розпакувати повний ZIP або відкрити чинний shared launcher,
MSAA4 → «Відкрити клієнт» → заставка/консоль → підключитися вручну → штатний
вихід. Старий запис не повторювати заради діагностики: його вже збережено.
`source_contract_fixed=true`, `strict_negative_reproduced=true`;
`actual_Intel_startup_verified=not_run`, `actual_Intel_MSAA4_verified=not_run`,
`actual_MSAA4_verified=not_run_on_target_HD4600`,
`manual_gameplay=not_run`, `runtime_original_cause=not_verified_on_Intel`.
