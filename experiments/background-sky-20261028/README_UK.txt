НЕАКТИВНИЙ АРХІВ ВИХІДНИХ ЗМІН ТЕСТОВОГО SKY-КАНДИДАТА

candidate.patch — мінімальний вихідний diff проти точної Git-бази
97fd41e0f9d709255dc71723f035d88400c4d2e2. Не застосовувати його поверх
поточного dirty/checkpoint renderer: там є інші незалежні експерименти.
Архів не підключено до CMake й не змінює робочі або мережеві EXE.

У patch: apps/hlclient/main.cpp, src/renderer/opengl/opengl_renderer.cpp,
чотири нові приватні headers у experiments/background-sky-20261028/include/.
Для відтворення використовуйте окремий checkout бази, застосуйте patch,
додайте цей include directory до приватних client/renderer compile targets.
Дельта сама по собі не гарантує byte-identical EXE: також потрібні точні
compiler/link flags та frozen B6 dependencies/libraries оригінальної збірки.
source-identity.json зберігає початкові Git blobs і SHA256.

Механічна перевірка: patch застосовано тільки до нової локальної scratch
копії base files; усі 6 отриманих Git blobs збіглися з реально скомпільованими
frozen source inputs. Поточні root sources при цьому не змінювались.

Реально тестований client SHA256:
9f4616bd8a23430f0369993fbe0ff4a4d029605defdee490a6f824435db85679.
Native launcher SHA256:
727cb99d5bb910f3825eb411fd87fdb981b15fd5ab9e133917e8f10f7c6490a9.
Лаунчер/current tests збережені у звичайних apps/include/src/tests репозиторію.

Вибір original/depth — startup policy того самого клієнта. Original default.
Новий late path НЕ ПРИЙНЯТО як прискорення: у двох останніх живих тестах
RTX4060/1920x1080/MSAA4 він виконався 0 кадрів; depth повертався через gl_state.
Захисні перевірки не послаблювались. Фізику/prediction цей policy не змінює.
Не видавати цей source archive за новий accepted production release.

Повні приватні build/trace/receipt/rollback лишаються локально в
manual-artifacts/task-records/background-sky-real-client-20261028/ та
sky-native-checkbox-20261028/. Логи/auth/settings/Valve assets/EXE у Git відсутні.
