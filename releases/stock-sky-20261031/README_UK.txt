ЗАФІКСОВАНА ДОСТАВЛЕНА ЗБІРКА HL CLIENT — 31.10.2026

Цей каталог архівує ТОЧНИЙ canonical HLClientEngine-Win10.zip, доставлений
кандидат Client3d6ec5e8 / Launcherfce96454 та фактичні input-copy snapshots.
ZIP запускається через єдиний HLClientLauncher.exe після розпакування.
Потрібні власні ліцензійні Steam/Half-Life та драйвер OpenGL 3.3 Core.
Valve assets, steam_api.dll, чужі settings, logs, auth сюди не включені.

ВАЖЛИВО: це delivered-build checkpoint, НЕ твердження, що поточний
root CMake/main з чистого checkout збирає ці самі EXE. Canonical src/apps
не інтегровано цим архівуванням. Історичний HEAD є тільки контекстом.
client-source містить рівно 21 input-copy з фінального Client build;
launcher-source — рівно 11 input-copy з фінального Launcher build.
Це фактичні скомпільовані task overlays, а не пізніше змінена source-папка.

recipes містить точні історичні build scripts як АРХІВНЕ ПОСИЛАННЯ.
Вони мають жорстко задані локальні шляхи й потребують збережених frozen B6
headers, templates, бібліотек, pinned SDL/GLAD includes, MSVC 14.34 та SDK.
B6 binary libraries і повне frozen source tree тут НЕ включені.
Client link: 136 ordered / 117 unique archives; перебудований лише renderer
archive, 116 frozen archives незмінні. Тому цей subtree не є самодостатнім
clean-checkout build/reproduction рецептом. Ідентичності залежностей наведено
без приватних абсолютних шляхів у CHECKPOINT.json.

Correctness: VERIFIED_STOCK_CASES_FIXED — конкретні stock SKY cases.
Performance: GAIN_CONFIRMED_IN_TEST — обмежений статичний Crossfire-контроль
на development Intel RaptorLake-S Mobile, НЕ gameplay FPS і НЕ гарантія
виграшу на інших GPU. Повну HL_PARITY чи production acceptance не заявлено.
Windows10/gameplay нового кандидата ще потребує власного контролю.

Типовий режим — original («Поточне небо»). Новий тестовий режим —
world_sky_non_occluding («SKY без перекриття BSP-площиною — тест»).
Старі token values 0–3 не перепризначено; 4/5 зарезервовані; новий режим=6.
Runtime-text OFF. User settings різних ПК не копіюються між інсталяціями.

CHECKPOINT.json — санітизовані build/decision/delivery identities.
FILES.json — закритий перелік exported payload, без самопосилання.
SHA256SUMS.txt покриває кожен файл subtree, крім самого SHA256SUMS.txt.
Raw runtime/startup logs, settings/test-data/auth, старі тестові ZIP,
rollback binaries та raw include/link manifests не включено.
