# Forge 2.5

Модульный игровой runtime: C++17, Python 3.10+, GLFW/OpenGL 3.3, macOS и Windows.

Репозиторий содержит исходники. `.tools`, `build`, `dist` и скачанные библиотеки не входят в Git; в `vendor` хранятся описание и lock-файл. Сначала установите зависимости и скомпилируйте C++-движок. Команды выполняются из корня репозитория.

На macOS нужны Command Line Tools и Python 3.10+ с python.org:

```sh
python3 -m venv .tools
.tools/bin/python -m pip install cmake==3.31.6
.tools/bin/python tools/dependencies.py
.tools/bin/python tools/forge.py compile
.tools/bin/python tools/forge.py dev
.tools/bin/python tools/forge.py build --output dist/MyGame
.tools/bin/python tools/forge.py dev --scene advanced.json
.tools/bin/python tools/forge.py edit --scene editor-empty.json
.tools/bin/python tools/forge.py build --output dist/MyGame.app
```

На Windows установите Python x64 и Visual Studio 2022 Build Tools с Desktop development with C++; используйте Developer PowerShell:

```powershell
python -m venv .tools
.tools\Scripts\python.exe -m pip install cmake==3.31.6
.tools\Scripts\python.exe tools/dependencies.py
.tools\Scripts\python.exe tools/forge.py compile
.tools\Scripts\python.exe tools/forge.py dev
.tools\Scripts\python.exe tools/forge.py build --output dist/MyGame
```

`compile` создаёт бинарный файл движка, `dev` запускает игровой контент без упаковки, `build` создаёт самостоятельную игру в новой папке `dist/MyGame`. Повторно компилировать C++ для изменений Python, JSON и ресурсов не требуется. Интернет нужен для первой установки зависимостей.

A/D — движение; Space — прыжок; Enter — текст новеллы; F5 — сохранение и звук; 2 — 3D; **3 — новый UI с меню, журналом и слотами**; 1 — возврат в 2D; Escape — выход.

[Подробная инструкция](Инструкция.md) · [Конфигурация](engine.json) · [Лицензия Forge](LICENSE) · [Атрибуция](ATTRIBUTION.md) · [Лицензии зависимостей](THIRD_PARTY.md)

Ядро не требует изменений при создании игры. Все папки контента задаются в JSON. Python API: `import forge`. Собранная игра содержит собственный Python runtime.

В Forge 1.2 есть готовые `Canvas`, `Row/Column`, кнопки, ползунки, прокрутка и перенос текста. Крупный текст учитывает HiDPI. Независимые модули предоставляют диалог/авточтение, журнал, меню паузы, сохраняемые настройки звука, слоты с версиями/проверкой целостности/backup и автосохранение, аудиоканалы и crossfade. Обработчики объектов допускают вложенный spawn; неудачный hot reload возвращает управляемое состояние движка.

В версии 1.2.2 усилены проверки упаковки и числовых границ, восстановление слотов доступно из меню, а raycast и коллизии одинаково учитывают отключённые коллайдеры. Исправлен первый переход Shift+Tab. Подробные ограничения и статусы сохранений описаны в инструкции.

Локализация встроена в C++: JSON-переводы, параметры и множественные формы, резервный язык, сохранение выбора и переключение языка в готовом меню. `forge.message()` связывает перевод с UI и объектами; `forge.tr()` возвращает строку. Русский и английский доступны в стартовых сценах. Подробнее — раздел 10.5 инструкции. Для сложных письменностей и RTL требуется расширить рендерер текста.

Встроенные примеры входят в исходную поставку: [2D-сцена](scenes/welcome.json), [3D-сцена](scenes/world3d.py), [интерфейс с локализацией и меню](scenes/interface.py), [расширенная 3D-сцена](scenes/advanced.json), [пустая сцена редактора](scenes/editor-empty.json), [физика с частицами](scenes/simulation.json) и [PBR/иерархия/процедурные меши](scenes/materials.json). Они используют поставляемые модели, текстуры, шрифт и звук; отдельная игра для запуска примеров не нужна. Для прямого запуска выберите `dev --scene interface.py`, `dev --scene world3d.py` или `dev --scene advanced.json`.

Forge 2.0 добавляет камеры с render targets, UV/sprite sheets, batching и атласы глифов, постобработку и uniforms из Python, fallback-шрифты, фоновые asset handles и бюджеты памяти. Встроены action map, контроллеры и запись/воспроизведение ввода, стили и переходы UI, планировщик AI, состояния и таймлайны. Звук поддерживает pan, spatial audio, DSP, ducking, лимиты голосов и субтитры по playback cursor.

3D-набор включает swept AABB character controller, point/directional/spot lights, PCF-тени, импорт OBJ/glTF/GLB/FBX/DAE и skeletal animation на GPU. Команда `edit` открывает редактор с иерархией, инспектором, браузером ресурсов, undo/redo, preview и сохранением JSON. `--scene` выбирает начальную сцену без изменения конфигурации.

Forge 2.1 добавляет нативную систему частиц и необязательную 3D-физику Bullet: вращающиеся тела, box/sphere/capsule, трение, упругость, силы/моменты, CCD, слои столкновений и capsule-контроллер на наклонных поверхностях. Частицы поддерживают непрерывный выпуск и burst, текстуры, цвет/размер по времени, привязку к объектам, billboards и batching. Прежняя AABB-физика остаётся default; для новой достаточно `"physics": {"backend": "bullet"}` в 3D-сцене. Все реализации и шейдеры доступны для изменения; подробности — раздел 19 инструкции.

```sh
python tools/forge.py dev --scene simulation.json
```

WASD — движение капсулы, Space — прыжок, E — искры, R — перезапуск шара.

Forge 2.3 ускоряет частицы: кеширует разрешённые пути текстур, собирает снимок один раз за кадр и использует OpenGL 3.3 instancing вместо шести CPU-вершин на частицу. Прежние пользовательские vertex shaders автоматически используют совместимый путь. Изменение трансформации пересчитывает только затронутое поддерево; `set_positions` и `raycast_many` позволяют обновлять и запрашивать мир пакетно. Bullet обновляет только изменившиеся тела, Python bridge передаёт обычные данные без промежуточных JSON-строк. Lifecycle, Python API, reload, шрифты, медиа, GPU-ресурсы и рендер частиц разделены на реализации. Контракты, диагностика и воспроизводимый benchmark — раздел 21 инструкции. Один активный Python runtime, CPU-симуляция/alpha-sort частиц и остальные функциональные границы сохранены.

Forge 2.2 добавляет PBR metallic/roughness с albedo, normal, ORM/AO и emissive-картами, прозрачный проход, иерархию объектов с локальными и мировыми координатами, обновляемые процедурные меши с цветами вершин, фильтрацию/mipmaps и снимки в пользовательский каталог. `find` использует индекс; legacy-физика отбирает пары по X вместо полного перебора. Редактор вынесен в отдельный модуль. Вложенные metadata отложенных сохранений фиксируются независимой копией. Прежние defaults сохранены; контракты и ограничения — раздел 20 инструкции.

```sh
python tools/forge.py dev --scene materials.json
python tools/benchmark_world.py build/bin/forge
```

На macOS `build --output dist/MyGame.app` создаёт приложение с приватным Python, иконкой и пользовательскими каталогами. Есть команды Developer ID signing/notarization, настройки окна и crash reports. [Карта возможностей и проверок 2.x](docs/FORGE_2.md) связывает подсистемы с проверками. Точные границы рендера, физики, редактора и отката описаны в инструкции.

## Состав исходного репозитория

Git хранит C++-исходники, изменяемые Python-модули, инструменты, встроенные примеры с исходными ресурсами, тесты, документацию, лицензии и lock-файл зависимостей. Сборки, окружения, скачанные библиотеки, сохранения, логи, скриншоты и локальные отчёты исключены через [.gitignore](.gitignore). Шрифт, модели `.obj`/glTF, изображения и WAV примеров — необходимые исходные ресурсы и остаются в Git.

После компиляции `python tools/forge.py validate` проверяет ресурсы и синтаксис, `python tools/forge.py test` запускает интеграционные наборы. Полный CTest запускает восемь наборов, включая numeric, packager_paths, rendering, authoring и project API: `ctest --test-dir build -C Release --output-on-failure`. Workflow macOS/Windows собирает варианты с базовым редактором и без него, выполняет CTest, отдельный Linux job — четыре графических набора на Mesa/Xvfb. Software OpenGL не подтверждает работу физических GPU или аудиоустройств этих ОС. Команды — [инструкция, раздел 20.6](Инструкция.md).

## Лицензия и изменения

Forge распространяется по пользовательской **Forge Attribution License 1.0** с открытым исходным кодом и обязательным указанием движка на загрузочном экране и в меню игры. Для собственного изменённого ядра нужна подпись «Создано на основе движка Forge (ядро изменено)»; для обычного ядра — «Используется движок Forge». Коммерческие и закрытые игры разрешены.

Всё вне [перечня файлов ядра](CORE.md) можно изменять для своей игры, если отдельная лицензия не устанавливает другие условия. Это включает шейдеры, физику, графические и звуковые модули. Изменение ядра тоже разрешено, но обычным разработчикам игр рекомендуется использовать конфигурацию и расширения: так проще сохранять совместимость с обновлениями движка. Лицензии сторонних компонентов сохраняются.

Лицензия пользовательская и не имеет статуса OSI-approved. Полные условия: [LICENSE](LICENSE).

Forge 2.4 добавляет prefab-иерархии и независимые экземпляры, property clips, layered skeletal animation с переходами/событиями, state machine, retarget по rest pose и morph targets. В сценовом редакторе доступны клипы, bone keys, layers, scrub, marker timeline, morph sliders и retarget JSON. Пример: `python tools/forge.py dev --scene authoring.py`; API/границы — раздел 22 инструкции. Watcher работает в фоне, listener/contact dispatch избегает повторных полных обходов; лёгкие C++ контракты сохраняют совместимый engine.hpp. CI проверяет также запуск самостоятельной поставки через `tools/verify_package.py`.


Forge 2.5 отделяет оболочку редактора от открытого формата проекта и API. Базовый ImGui-интерфейс необязателен (`compile --without-editor`); JSON-lines CLI, Python SDK, общие extension commands и runtime editing работают без него. Сохранение учитывает неизвестные поля и внешние правки, обнаруживает конфликты и использует атомарную замену файла. [Руководство по оболочкам, формату и API](docs/PROJECT_API.md) · [JSON Schema](schemas/project.schema.json) · [Пример сторонней оболочки](examples/editor/terminal_shell.py).

```sh
python tools/forge.py shell --shell examples/editor/terminal_shell.py --extension examples/editor/labels_extension.py
python tools/forge.py edit --shell examples/editor/viewport_shell.py --scene editor-empty.json
python tools/forge.py project --serve
```
