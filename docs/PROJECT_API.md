# Оболочки и открытый формат Forge 2.5

Проект принадлежит разработчику, а не редактору. Базовая ImGui-оболочка, сторонний GUI, терминал и ручное редактирование работают с одной конфигурацией и ресурсами. Скрытой базы редактора нет. Для `run`, `dev`, `validate` и `build` интерфейс и SDK не нужны. Нативный runtime по-прежнему содержит C++/CPython и GLFW/OpenGL для игры.

## Формат и совместимость

`engine.json` — UTF-8 JSON, `schema_version: 1`. Название этого файла можно изменить и передать через `--project`. `paths` описывает группы ресурсов; пути относительны корню конфигурации, разрешаются с учётом symlinks и остаются внутри проекта. `python_paths` расширяет импорты. Python-сцены/скрипты остаются обычными исходниками. JSON-сцены содержат `mode`, `entities`, `camera`, `physics`, `rendering`, `emitters` и необязательный `script`. Сущности хранят локальные TRS, `parent`, ссылки на ресурсы, `scripts`, `data`, animator и morph settings. `parent` ссылается на ID, а не индекс строки. Scene script разрешается относительно группы scenes; Behavior — scripts. Материалы — JSON; изображения/модели/звук/шейдеры сохраняют свои стандартные форматы. `@mesh:` — runtime registry; документ не хранит его геометрию автоматически.

Версия движка, `schema_version`, project API и игровой API независимы. Новая оболочка не должна менять версию формата ради своего интерфейса. JSON Schema в `schemas/{project,scene,entity,prefab,material}.schema.json` описывает базовую структуру; `additionalProperties` разрешены. Семантические проверки ссылок, числовой арифметики, иерархии, физики и поддерживаемых ресурсов выполняет нативный движок. Schema не заменяет `validate`, проверку media или выполнение Python. Defaults и расширенные контракты описаны в `Инструкция.md`. `$schema` в пользовательском JSON необязателен; runtime сохраняет его как обычное поле.

Рекомендуются устойчивые непустые `id` у сущностей и emitters. Для `entities` и `emitters` с уникальными строковыми ID merge сопоставляет записи по ID, поэтому перестановка строк не смешивает объекты. Без таких ID весь массив рассматривается как одно поле. Вектора, scripts, keyframes и остальные массивы также атомарны. Незнакомые поля на любом уровне сохраняются. Данные оболочки удобно помещать в `extensions["org.example.shell"]`; это соглашение для имён, не запрет на другие поля. Состояние панели, приватные кеши и история undo не обязательны для запуска игры; исходный проект не зависит от них.

При сохранении форматирование может стать двухпробельным; порядок ключей и исходные числовые лексемы не гарантируются. Значения неизвестных полей сохраняются. Если документ семантически не изменился, файл вообще не переписывается. JSON с комментариями, дублирующимися ключами, NaN/Infinity не является поддерживаемым обменным форматом. Произвольное состояние Python, closures, внешние файлы и побочные эффекты скриптов автоматически не сериализуются.

## API документов без GUI

Все команды доступны нативно и через launcher:

```sh
build/bin/forge project --project engine.json --request request.json
python tools/forge.py project --project engine.json --request request.json
python tools/forge.py project --project engine.json --serve
```

На Windows укажите `build/bin/Release/forge.exe` или `build/bin/forge.exe`. Без `--request` и `--serve` одна JSON-команда читается из stdin до EOF. `--serve` — локальный JSON-lines протокол по stdin/stdout: один запрос и ответ на строку; завершение stdin завершает сервер. Никакого сетевого listener, аккаунта или GUI. stdout содержит только JSON, diagnostics — stderr и `forge.log`; лог автоматически не открывается. Один ошибочный запрос не останавливает serve. Одна неуспешная команда имеет exit code 1. Размер запроса ограничивается 16 MiB после чтения.

Запрос:

```json
{"api_version":1,"id":7,"op":"read","group":"scenes","file":"room.json"}
```

Успех:

```json
{"api_version":1,"id":7,"ok":true,"result":{"exists":true,"revision":"opaque-content-revision","data":{"entities":[]}}}
```

Отказ:

```json
{"api_version":1,"id":7,"ok":false,"error":{"code":"conflict","message":"...","paths":["/entities/player/name"]}}
```

`id` необязателен и возвращается без изменения; malformed JSON может не иметь id. `revision` — непрозрачный идентификатор JSON-содержимого, не security checksum. Сохранение использует **полный baseline `base`**, а не доверяет совпадению revision.

| op | Аргументы | Результат |
| --- | --- | --- |
| capabilities | — | api_version, engine_version, base_shell, operations |
| inspect | — | Конфигурация проекта и имя settings |
| list | group, необязательно | Относительные имена файлов группы; без group — весь корень |
| read | file, group необязательно | exists, revision, data; для отсутствующего файла data/revision=null |
| check | file, group, data | Проверка кандидата без записи: valid=true или ошибка |
| commit | file, group, base, data | Объединение с текущим диском, проверка, атомарная запись и новый snapshot |
| patch | file, group, base, data | data — список RFC 6902 операций относительно base; затем тот же commit |
| apply_patch | data, patch | Применение RFC 6902 к JSON в памяти; без проверки сцены/записи |
| merge | base, local, disk | Трёхстороннее объединение в памяти; без проверки ресурсов/записи |

`file` без group разрешается от корня; с group — относительно её настроенного каталога. Семантический тип сцены/материала/объекта определяется фактическим расположением, в том числе без group. Config check проверяет новый набор paths и проект; scene check — JSON/числа/hierarchy/physics/resource references; prefab — наследование/структуру локальных ID; material — поля/карты. Эти проверки не запускают пользовательский Python и не создают GPU-контекст; полный `validate` дополнительно проверяет media, animation assets и Python syntax. Перед поставкой используйте `validate`/`build`.

`base: null` означает создание нового файла; существующий файл не заменяется. Для обновления сначала `read`, затем передайте полученный data как base. Для удаления сущности используйте patch `/entities/N`, для удаления поля — JSON Pointer; нельзя молча перезаписывать целый документ по старому снимку. Файловое удаление, перемещение изображений, импорт файлов и текстовое редактирование Python остаются обычными операциями файловой системы: скрытого индекса редактора для них нет. Общая API обеспечивает все операции встроенного редактора над документами, а не пытается заменить системный файловый менеджер.

Если local и disk изменили разные поля — изменения объединяются. Если одно поле изменено несовместимо, объект удалён с одной стороны и изменён с другой, или порядок одних и тех же записей изменён несовместимо — `conflict` с путями. ID в conflict path обозначает ключ записи; `/@order` — порядок. Черновик и файл сохраняются. Прочитайте новую версию, покажите конфликт человеку и сформируйте новый baseline; сервер никогда не выбирает победителя по времени.

Запись использует временный файл рядом с документом и атомарную замену (`MoveFileExW` на Windows, rename на POSIX). Проверки выполняются до замены. Кооперативные writers исключают одновременную запись через `.имя.json.forge-lock`; оставшийся после аварии lock удаляется вручную после остановки writer. Перед заменой также проверяется новое содержимое диска. Произвольный внешний редактор не соблюдает lock: остаётся узкое окно между финальной проверкой и rename. Это оптимистическое согласование, не распределённая блокировка и не гарантия fsync/восстановления питания. Ручные изменения, уже присутствующие при проверке, учитываются; конфликт никогда не приводит к delete-before-rename.

## Python SDK и расширения

SDK находится в `sdk/forge_editor`, использует только стандартную библиотеку. Для отдельного инструмента можно установить `python -m pip install ./sdk` или добавить `sdk` в Python search path. Launcher `shell` делает это сам. GUI toolkit выбирает автор оболочки: Qt, GTK, web UI с собственным локальным host или терминал. Любой язык может использовать JSON-lines протокол без SDK.

```python
from forge_editor import Client, ProjectError

with Client("/path/to/forge", "/path/to/project/engine.json") as client:
    document = client.document("room.json", "scenes")
    document.patch([{"op":"replace", "path":"/entities/0/name", "value":"Player"}])
    document.undo()
    document.redo()
    try:
        document.save()
    except ProjectError as error:
        print(error.code, error.paths)  # draft remains available
    process = client.launch("dev", scene="room.json")
    process.wait()
```

`Document.data/base` возвращают глубокие снимки; `Document.replace(data)` проверяет и глубоко копирует данные; patch/check/undo/redo не пишут на диск. История ограничена 100 снимками. `save()` обновляет baseline/data, включая объединённые внешние поля, и очищает локальную историю. `reload()` явно отбрасывает черновик. `dirty` сравнивает текущие данные с baseline. Если нового файла нет, data=None; replace создаёт черновик. Client сериализует обмен одним lock, протокол не multiplex. `launch()` возвращает Popen: оболочка отвечает за отмену, progress и reporting exit code. CLI run/dev/validate/build остаётся источником их поведения и ошибок.

Расширение — явно подключённый доверенный Python-файл:

```python
API_VERSION = 1

def register(client):
    client.register("studio.tag", tag)

def tag(client, file, value):
    doc = client.document(file, "scenes")
    data = doc.data
    data.setdefault("extensions", {})["studio.tag"] = value
    doc.replace(data).save()
    return doc.data
```

`client.extension(path)` вызывает register; `client.command("studio.tag", file="room.json", value="work")` исполняет команду. Имя требует namespace, уникально; `forge.*` зарезервирован. При отказе регистрации registry откатывается, но произвольные побочные эффекты импортируемого Python не откатываются. Plugin не является security sandbox. Не загружайте плагины автоматически из неизвестных проектов. Нативные FORGE_MODULE и игровые Python-модули продолжают использовать прежний публичный API.

Один и тот же `examples/editor/labels_extension.py` работает во внешнем Client и в базовой оболочке через RuntimeClient: это адаптер общего документного API, без второго Python runtime. Встроенный интерфейс показывает зарегистрированные команды в **Extension commands**, принимает JSON arguments и вызывает их. В edit добавьте конфигурацию:

```json
{"editor":{"extensions":["tools/my_extension.py"]}}
```

Эти пути относительны проекту. Либо явно `edit --extension path/to/plugin.py`; CLI-путь относителен текущей папке/может быть абсолютным. `run`/`dev`/`build` не загружают editor plugins. Обновление файлов оболочки/плагина требует перезапуска editor session; игровой hot reload не заменяет host автоматически. Для размещения визуальных панелей внешняя оболочка использует свой toolkit; builtin host предоставляет список команд, а не произвольный ImGui widget ABI.

## Выбор оболочки и runtime editing API

```sh
# Официальная оболочка по умолчанию
python tools/forge.py edit --scene editor-empty.json
# Viewport без builtin UI
python tools/forge.py edit --shell none --scene editor-empty.json
# Python-controller собственного viewport внутри проекта
python tools/forge.py edit --shell examples/editor/viewport_shell.py --scene editor-empty.json
# Самостоятельная оболочка через SDK
python tools/forge.py shell --shell examples/editor/terminal_shell.py --extension examples/editor/labels_extension.py
```

Viewport shell — проектный `.py` с `API_VERSION=1`, optional `on_start()`, `on_update(dt)`, `on_destroy()`. on_update работает и при паузе игры. Пользуйтесь `forge` input/objects и `editor_command`; храните ID, а не указатели на старые сущности. Shell живёт до завершения edit, отдельно от смены игровой сцены. Состояние UI, созданное как игровые объекты, является частью мира и может попасть в экспорт; отдельный GUI через SDK этого ограничения не имеет. Внешняя оболочка может запускать native preview отдельным процессом; API 1 не предоставляет embedding GPU viewport в Qt/GTK и live RPC управления работающей игрой. Runtime API доступна коду внутри edit, документная RPC — внешним процессам.

`forge.editor_command(dict)` доступен любой viewport shell, включая `--shell none`, в headless edit и при сборке без ImGui:

| op | Аргументы | Действие |
| --- | --- | --- |
| snapshot | — | scene, selected, preview, размеры undo/redo |
| select | id или пустая строка | Выделение текущей сущности/снятие выделения |
| preview | enabled | Включение/пауза симуляции; не reset |
| apply | scene | Подготовка/проверка полного JSON-кандидата, lifecycle transaction и commit |
| patch | patch | Изменение snapshot через RFC 6902, тот же apply |
| undo / redo | — | До 100 снимков; отказ сохраняет историю и рабочую сцену |
| load | file | Явная отложенная загрузка сцены (отбрасывает несохранённый черновик) |
| save | file | Сохранение/экспорт JSON через общий document commit |
| commands | — | Имена команд подключённых extensions |
| command | name, arguments | Вызов общей extension command |

Add/delete/duplicate, parenting/TRS, assets/material/camera/light/physics/text, animator/morph/prefab authoring выражаются через snapshot+patch либо существующие публичные функции `forge`, `prefabs`, `animation` и document API. Для процедурных объектов также требуется регистрация геометрии. `forge.scene_data()`, `forge.save_scene(file)`, `forge.editor_select(entity)` сохраняют привычный путь; selection теперь общий для UI/API. `forge.project_request(dict)` возвращает result либо native exception; `forge.project_response(dict)` возвращает ту же структурированную envelope, что RPC. RuntimeClient использует вторую, обеспечивая общий ProjectError контракт.

Document commit/patch и save_scene через runtime недоступны во время hot reload: используйте их после commit.

apply/patch/undo/redo требуют остановленную и полностью инициализированную editor session, вне reload/teardown. Замена запускает стандартный lifecycle, освобождает прежние entity references и может вызвать пользовательские callbacks заново. Отказ подготовленного кандидата оставляет прежний world/scripts/resources/history; произвольные внешние эффекты callbacks требуют стратегии автора игры. Это не сериализация состояния интерпретатора и не undo файловых записей плагина.

При сохранении исходной JSON-сцены вычисляется разница с runtime baseline и применяется к исходному документу. Неизменённые defaults/унаследованные prefab значения не материализуются. Затем результат объединяется с диском. Save As создаёт новый полный snapshot; в инициализированном edit он становится текущим документом, следующий Save использует его baseline. Экспорт из run/dev не переключает сцену. Существующий неоткрытый target требует сначала read/load — он не перезаписывается. Для Python-сцены экспорт выбирает новое JSON-имя; исходный `.py` не меняется. Неизвестные root/entity/camera/extension поля сохраняются. Обновлённые извне поля после сохранения находятся на диске; для отображения их в viewport выполните явный Reload.

Автоматический hot reload не заменяет мир с несохранёнными правками editor session. Он выдаёт сообщение и сохраняет работу; сохраните/undo или явно reload, затем измените watched file для повторной попытки. При пустом черновике остаётся прежний hot reload. Play preview не возвращает сцену к исходному состоянию. Прямые изменения через Python setters доступны для низкоуровневого controller, но общий undo гарантируется для editor_command и builtin authoring, а не для произвольного Python.

## Сборка без базового интерфейса

```sh
python tools/dependencies.py --without-editor
python tools/forge.py compile --without-editor
python tools/forge.py run
python tools/forge.py edit --shell none
python tools/forge.py build --output dist/Game
```

CMake: `-DFORGE_WITH_EDITOR=OFF`. ImGui не скачивается bootstrap с этим флагом и не участвует в такой сборке; `editor.cpp`/`animation_editor.cpp` не компилируются. JSON service, runtime editing, сцены, renderer, animation API, CLI validate/build/run остаются доступны. `capabilities.base_shell`/`forge.capabilities().base_editor` отражают фактическую сборку. `edit --shell builtin` без неё в оконном режиме сообщает понятную ошибку; выбирайте none/свою оболочку. Headless edit допускается без UI. Обычный `compile` возвращает ON. Собранная игра не загружает SDK/plugins и не требует установленного редактора.

## Проверки

`tests/project_api.py <binary>` проверяет две оболочки, ручные правки, unknown fields, ID merge/order conflict, atomic failure, Unicode/paths/symlink, config/material/prefab validation, SDK history, общий extension adapter, CLI stdout/exit codes и native paused lifecycle/rollback. Включён в CTest и launcher test. GUI-наборы проверяют прежний viewport/ImGui/animation editor. CI собирает macOS и Windows с ON/OFF, а GPU suite использует базовый интерфейс. Один активный Python runtime, текущие renderer/physics/animation ограничения и сторонние лицензии сохранены.
