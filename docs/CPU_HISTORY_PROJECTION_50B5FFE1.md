# CPU history / projection — 50b5ffe1

Продовження: [Studio pose / frame build](CPU_STUDIO_POSE_FRAME_BUILD_50B5FFE1.md), [evidence](evidence/CPU_STUDIO_POSE_FRAME_BUILD_50B5FFE1.json). Новий exact-signature arithmetic кандидат показав суттєвий CPU p50 gain, але відхилений за RX commit p99 control; production/deliveries знову exact B0. Попередні результати й відмови нижче не переписано.

## Підсумок

`profiling_complete=completed_for_declared_offline_workload`; `production_fix=rejected_and_withdrawn`; `CPU_time_gain=not_established`; `MSAA4_total_frame_gain=not_run`; `target_Win10_100FPS=unproven`; `manual_acceptance=not_run`.

Обхід історії **не виявився головною ціною**: менш ніж 1% `present_entities` у шести оголошених fixtures. `world_entity` не кешувався, жоден callback не пропускався. Реалізовано й незалежно перевірено один production-кандидат усунення повторної Euler→quaternion конверсії. Точні результати збіглися, але кандидат **не пройшов наперед задані timing/memory gates**. Його та тимчасові вимірювачі відкликано через `apply_patch`; усі **754 production source entries** точно відповідають збереженому actual B0. Нової збірки для користувача немає.

[Машинні докази](evidence/CPU_HISTORY_PROJECTION_50B5FFE1.json), [сирі матеріали](../manual-artifacts/task-records/cpu-history-projection-50b5ffe1-20261024), [попередня історія](WIN10_MSAA4_100FPS_50B5FFE1.md).

## База та межі відтворення

HEAD/main: `6a86ce0e3b826341c90bbcee8c603771f1dc58c8`. Actual B0 — робочий стан на початку цього пакета, **включно з попередніми незакоміченими змінами**, не чистий HEAD. Збережено 1322 source/test/script entries, diff/status/CMakeCache, 131 бібліотеку та чотири shipped binaries. Помилку аргументу `git diff --output` intake-виклику виправлено; вже скопійовані inputs повторно звірено. Receipt: `baseline/receipt.json`.

Actual B0 client: `7765414555a74e4ab5cd3bdf7ddce9da03d998c4ba76659acf43fce92022cd22`.

Незалежні CPU drivers, однакові frozen harness sources:

| Driver | SHA-256 |
| --- | --- |
| B0, frozen original headers/libs | `b2633a250090c373561ba3e79d4aeccb80adf399b96926ed6ec6f805edbfd3a1` |
| B1, rotation candidate | `4cded858849d8479125ae216be42adcc80f842be2707bf58a91e7b5e6b7016e3` |

MSVC **14.34.31933**, v143, Release/Win32; перевірено actual `.lastbuildstate` та compiler include paths у read-tlogs, не лише default toolset file. B0/B1 не є новим helper проти самого себе. Frozen B0 libs не перебудовувались для порівняння. B1 source overlays, 131 library snapshot та manifest збережені у `rejected-rotation-candidate`; profiling B0 — окремо `instrumented-B0`.

Історичні runs `0c29f4caaaad3ed789ab8ee1474e4f70` та `50b5ffe11be28794b66dcd524a34fc7d` використано лише як орієнтири: frame7775 Studio11,27 мс без нових committed records; frame660 Studio27,05 мс усередині CPUupdate38,35 мс. Це inclusive scopes, не доказ вартості traversal. Точні історичні entity inputs не відновлювались і не вигадувались.

Попередні 72 GPU runs не повторювались. Їх 11 control regressions не названо pixel mismatches. Старі 4190 passed /75 skipped належать попередній B0 перевірці, **не цьому кандидату й не новому full-suite run**.

## Scope map і початкове профілювання

Normal `apps/hlclient/main.cpp`: перший `studio_ms` охоплює `present_entities`; пізніше до нього додається local viewmodel materialization. Це не один history timer. Основний шлях — **`RuntimeReplayLocalAssets`**, не `RuntimeReplayVisualProjection`.

`present_entities` → lifetime/history classification → player sampling → ordinary sampling → support-context preparation → `project_entities` → validation/setup → current-time entity/appearance/remote-intent/pose/bounds/attachment → frame builder → transactional publication → optional diagnostic capture.

Вимірювачі були opt-in, без per-bone logs, без перенесення роботи за межі `studio_ms`. Allocation counter перехоплює C++ new/new[]/aligned-new у вибраній фазі; це **requested allocation traffic, не RSS, не live retained heap і не всі можливі CRT malloc**. Окремо збережено palette capacity та recorder capacity. Вкладені timers inclusive; їх не можна додавати до батьківських.

Шість початкових cases: 4/8/16 remote actors × 2/16 observations, 64 warm-up +256 samples, Diagnostics Off. Synthetic BSP, production importer/HL host/materializer, locally installed Gordon +p_shotgun, synthetic local viewmodel. Власні resource copies лише в disposable fixture root; Steam installation не змінювалась. Між RX змінюється application/sample time; це не заморожена сцена за publication revision.

| Actors / history | History p50, мкс | Present p50, мкс |
| --- | ---: | ---: |
| 4 /2 | 0,8 | 358,3 |
| 4 /16 | 2,9 | 383,1 |
| 8 /2 | 1,4 | 722,2 |
| 8 /16 | 5,9 | 748,6 |
| 16 /2 | 2,8 | 1411,9 |
| 16 /16 | 12,2 | 1428,7 |

Для 16/16: player sampling30,5 мкс; ordinary10,0; support0,3; validation3,0; setup2,9; entity work1023,8; frame build314,3; publication5,2. Усередині entity work: **pose615,2**, posed bounds121,5, attachment219,1, remote intent15,3, appearance1,7 мкс. Lighting/material/copies та інша entity preparation залишаються у residual scope, не перейменовані на history.

Operations на 256 frames для 16/16: 65536 history visits, 78336 exact history lookups, 69888 фактичних classification calls у цьому fixture (усі lifetime rows наявні), 65536 approved-model lookups. Support rebuilds0 на warmed no-RX фазі. Загальні відсутні-row/lifecycle випадки не отримали нового profiling-count certification.

`world_entity const` **не є pure contract**: HL читає `known_absent_player` та збережені `selected_models_`; вони можуть змінюватися без нового snapshot. Alternate modules також не обіцяють відсутності side effects. Capability/revision contract не додано, складний history cache не будувався. Попередні A–D exact lookup/candidate-list/pose/summary fixes не перероблялись.

## Кандидат та чому його відхилено

У `evaluate_blend_pose` перша та друга endpoint rotation часто bit-identical. Кандидат повторно використовував лише `quaternion_from_euler` для цього одного bone/sample; порівняння — по bits, включно зі signed zero. Для різних bits — вихідний шлях. Обидва endpoints sampling, validations, `slerp`, composition/gait/transition, world hierarchy, bounds, support, світло та sockets залишалися current-time. Міжкадрового cache/нових owners/commit роботи не було; history eviction/invalidation тут не застосовується.

Змінювались `evaluate_blend_pose`, агрегування `StudioPoseStatistics` та тимчасовий normal-path profiler. Лічильники не імітували старі operations: у 256 no-RX frames parent poses реально виконали375337 conversions та205271 reuses замість580608 conversions. Цей counter не включає весь child/viewmodel pipeline. Менший operation count **не прийнято як доказ CPU gain**.

Independent oracle:162 scripted inputs (90 successful /72 очікувано rejected), 1/2/4 blends, changing frames/channels, signed zero, близькі float values, extreme/nonfinite/invalid channel inputs. Exact matrix/discrete/error signature в B0/B1 однаковий: `994319084254938540`; existing float tolerances не змінювались. У всіх36 CPU runs збіглися exact frame signatures та no-RX pose-change witnesses.

CPU campaign: **36 processes, 9216 warmed samples**; 3 interleaved pairs для малого 4/2 і дорогого16/16, окремо Diagnostics Off/On; додатково16/16 з new RX кожні4 renders. Динамічна фаза проходить append та eviction. Commit, present, local host/viewmodel, diagnostic collection і parent visual-update subset вимірювались окремо; new-RX та no-RX розділені. Parent subset **не включає весь normal CPU update, prediction, renderer чи swap** — повний FPS ним не доводиться.

Gates оголошено до campaign у `cpu-campaign-plan.json`: primary paired p50 gain >max(20 мкс,B0 p50 span)+5 мкс і ≥2/3 позитивних пар; recurring p50/p95/p99 regressions за власними gates відхиляють; allocation bytes/calls не повинні зростати. Усі p50/p95/p99/max/raw values збережені, викидання samples або timing retry не було.

| Primary, 16/16, diagnostics off | B0 median p50 | B1 median p50 | Median paired gain | Gate |
| --- | ---: | ---: | ---: | ---: |
| present, мкс | 1479,8 | 1446,5 | **24,6** | 49 |
| parent visual subset, мкс | 1489,7 | 1452,4 | **26,2** | 49 |

Median paired differences не дорівнюють різниці двох окремих medians. Operational gates у PowerShell analyzer округлені до цілих мкс; без округлення primary gates48,9/48,7 мкс також не пройдені. Для16/16 Diagnostics On paired gain present61,4 мкс; для малого4/2 On лише0,9 мкс. RX Off/On paired present gain70,9/51,5 мкс, але **чотири RX p99 scope-controls** мали повторні2/3 regressions понад власні gates. Це CPU tails, не graphics/pixel failures. Max values не приховані; жодного висновку з одного найкращого run.

Commit-new-RX p50 Off58,9→60,2 мкс, On61,0→61,5 мкс; workload не перенесено в commit. Allocation call counts та published matrix/recorder capacities не зросли, проте diagnostic counters збільшили розмір pose results: для16/16 steady present requested bytes101889536→101977600 на256 frames, **+344 bytes/frame**. У18 pair-level memory comparisons це порушило оголошений gate. Хвости й недостатній primary gain окремо також не дозволяють прийняти кандидата.

Process guard: inventories до/після та polling під час runs, жодного busy event. Polling не доводить відсутності процесу, що повністю вклався між polls; guard не збирав command/auth data. Чужі процеси не зупинялись. Власна початкова /m:2 збірка застрягла після `Checking File Globs` із повторним створенням MSBuild nodes; точна причина не встановлена. Для ідентифікації лише її процесів окремо прочитано build command line. Тільки ці PID зупинені; /m:1 retry завершився. У timed runs builds0.

## Перевірки, withdrawal та доставки

Нові setup attempts збережені: compile warning-as-error для `getenv` виправлено через `_dupenv_s`; перший installed-player smoke з0 instances failed і **не включений у timings**, окремо оголошений Gordon fixture passed. Це не приховано як безпомилкова перша спроба.

Після withdrawal: Release normal presentation/pose **37 passed +1 skipped**; F8/recorder **26 passed**,0 failed. Normal tests включають lifecycle/slot/reset/history/model/light/lift/animation cases; oracle також виконався на actual B0. Source equivalence754/754; rollback library build exit0. Нова повна Debug/Release/ASan/core-off/architecture/launcher/GPU acceptance-матриця **not_run**: прийнятої production-правки немає. Не видаємо попередні suite або ASan runtime pass за нову перевірку кандидата. Розширені no-RX policy/readiness/alternate-module/staging controls не сертифіковані для rejected candidate.

Залежні targets тимчасово перебудували SDL3.dll. Це виявив фінальний SHA audit: regenerated copy збережена окремо, перевірено відсутність запущеної гри, **активну DLL відновлено з audited B0 backup**. Після цього всі12 local/shared/dist binaries і canonical ZIP точно збігаються з B0. Client/launcher/helper не замінювались, settings/resources/logs/auth/shared test-data не чіпали. ZIP `67b9b79ae631b34efb747cbc42f7f144fb6eaab5b1253da190007e9e66515412`,4183966 bytes, незмінний. Новий package/candidate на share не публікувався.

Production baseline, обидві доставки й rollback збережені. Staging/commit/push none; live/Steam-auth/HLDS sessions0. Новий user test незмінного B0 **не потрібний**.

## Конкретний незакритий етап

Подальша робота — **current-time local channel sampling/composition у `StudioPoseEvaluator::compose`**, який у цьому workload коштує615,2 мкс, а не history cache. Усередині нього треба окремо довести ціну channel lookup/validation та trigonometric interpolation; цей пакет не встановив їх індивідуальної частки. Наступні підтверджені великі scopes — `EntityRenderFrameBuilder::build`314,3 мкс та child attachment219,1 мкс. Не заморожувати pose і не повертати відхилений endpoint reuse без нового причинного доказу.

Ціль100 FPS на Windows10/1920×1080/MSAA4/VSyncOff/fps_max0/neutral image залишається **не доведеною**. Налаштування не знижувались. До graphical controls та нового target-PC manual test цей кандидат не допускався.
