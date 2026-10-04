# Forge 2.x — карта возможностей и проверок

Карта связывает возможности движка с реализацией и воспроизводимыми проверками в `tests/`. Она описывает покрытие тестами, а не результат конкретного локального запуска. Публичные API, примеры и команды проверок: [Инструкция.md, разделы 16, 18, 19, 20, 21 и 22](../Инструкция.md); границы ядра: [CORE.md](../CORE.md).

| Возможность | Реализация | Проверка |
| --- | --- | --- |
| Параллельные камеры | FBO render targets, 2D/3D, слои, texture @target:name | features_graphics: камеры/monitor |
| Sprite sheets / batching | Entity.uv, SpriteSheet, совместимые CPU quad batches | GPU: пиксели двух UV-регионов, ≤4 draw calls для 100 sprites + 200 glyphs |
| Постобработка / uniforms | Grain, bloom, aberration, CRT, fade/gamma; глобальные/объектные uniforms | GPU: сравнение кадров и custom GLSL |
| Fallback fonts | Цепочка TTF, одинаковое измерение/отрисовка, WARN U+code | measure_text + реальные glyph tests; отсутствие shaping/RTL описано |
| Assets / memory | 4 workers, handles/pins, scene preload, LRU CPU, GPU budget | features: статус/bytes/preload/отказ по бюджету |
| UI стили/переходы/навигация | Theme, Widget.style/opacity, ScreenStack/state, action navigation | features + прежние UI regression tests |
| Actions/controllers/events/replay | ActionMap contexts/remapping, GLFW gamepads, input snapshots и versioned record/replay | features: клавиши/мышь/pad/context/press/release/dt |
| Glyph atlas / profiler | Атлас по font/raster/code, quad batches; CPU timers и render/audio/asset stats | GPU batch test, profile API |
| Pan/spatial/DSP | Настройки sound handle, listener, lowpass/highpass/delay nodes | native silent PCM + прежние desktop audio tests |
| Субтитры | SRT/JSON/key/params по native playback cursor | features: pause/resume/stop |
| Ducking / voices / streaming | Channel rules, priorities/limits/overflow, decode/stream/auto | features: лимиты/DSP/duck/stream/cursor |
| Physics toggle/collider lists | Independent physics_enabled, активные AABB в solver/raycast/controller | прежние regressions + controller test |
| Частицы | Native bounded pools, seeded emission, lifetime/color/size, attachments; camera billboards/UV/alpha/additive batching | simulation: lifetime/capacity/seed/scene teardown; simulation_graphics: 5000 quads ≤3 calls, pixel curves/UV, cameras/depth/sorting/shader rollback |
| Bullet 3D | Double-precision box/sphere/capsule, rotation/forces/torque, friction/restitution, sleep, CCD, masks/triggers | simulation: exact overlap/rays, fast body/thin wall, bounce/friction, sleep/wake/gravity, numeric rollback, fixed-step/collision callback forces |
| Capsule controller | Convex sweep/slide, slope normals, bounded penetration recovery | simulation: landing/sliding on rotated ramp; example simulation.json |
| AI/state/timeline | Scheduler budget/clock, StateMachine, Timeline/Sequence | features: порядок/пауза/loop/отмена |
| Reload/persistence | Автоматический defer для core save/SaveManager, explicit deferred callback | features: отказ кандидата не меняет три файла, успех меняет |
| .app / подпись | Bundle/Info.plist/icon/private Python; ad-hoc build и команды Developer ID/notarytool | features: codesign/manifest/launch; настоящая нотарификация требует credentials |
| User dirs/window/crash | Application Support/AppData APIs, preferences/UI, crash JSON/native marker | features: storage вне игры, error reports, UI preferences |
| Character controller | Swept AABB slide, gravity/grounded jump | features: большое движение блокируется стеной, вертикальная опора/прыжок |
| Свет/тени | 16 directional/point/spot, ambient, одна PCF directional shadow map | GPU: кадры с тенями/без теней различаются |
| Skeletal animation | Assimp hierarchy/TRS clips, 128 bone GPU palette / 4 weights | оригинальный glTF fixture + pose/GPU кадры |
| glTF/GLB/FBX/DAE | Assimp importers, материалы и embedded/external textures | glTF fixture + FBX box; остальные варианты зависят от импортера |
| Редактор | ImGui hierarchy/inspector/assets, transform/collider/text/light/camera, add/delete/duplicate, JSON/undo/preview | окно editor + serialization; ограничения Python state описаны |

| PBR | GGX/Smith/Schlick, albedo/normal/MR/AO/emissive; JSON/inline и glTF maps | rendering: validator atomic; rendering_graphics: карты/факторы меняют пиксели, embedded glTF emissive |
| Прозрачность | opaque/mask/blend; opaque first, center-depth sort, no depth writes для blend; cutout shadows | rendering_graphics: порядок/alpha/mask по пикселям |
| Родители/дети | Native local TRS, world matrix/position/orientation; cycle checks, reparent, subtree lifecycle | rendering: JSON/Python, rollback shear/overflow, colliders в обоих backend; GPU: дочерний sprite |
| Процедурные меши | Scene-owned @mesh registry, revision, normals/UV/vertex colors, CPU/GPU budget/release | rendering: validation/lifetime/budget/reload; GPU: update/free, одно имя в разных сценах |
| Масштабирование | Hash index find; legacy sweep по X, Bullet sync через индекс | rendering: 10000 boxes / 30000 lookups; tools/benchmark_world.py: локальные измерения без timing gates |
| Texture sampling/captures | nearest/linear, mip chain с учётом GPU bytes; user_screenshot | rendering_graphics: mip budget и пользовательский capture |
| Границы модулей | editor.cpp, hierarchy.cpp, geometry.cpp, material.cpp; lightweight scene/model headers, explicit renderer Runtime | прежние native/GPU regressions; один Python runtime остаётся ограничением |
| Snapshot metadata | Deep JSON copy для отложенного SaveManager.write | rendering: настоящий успех/отказ reload, nested metadata/data, восстановление меша/дерева |

| Particle instancing | OpenGL 3.3 instance stream (52 bytes/particle), один frame snapshot, alpha CPU sort/additive grouping, кеш путей | simulation_graphics: UV/color/rotation/depth, 5001 instances/260052 bytes, ноль повторных path resolutions, custom shader/explicit fallback, rollback |
| Transform cache/bulk | Scalar setter проверяет и меняет только subtree; set_positions атомарно строит общее состояние | rendering: 2000 roots/2001 pose updates, no per-setter audit, parent/child bulk, duplicate/NaN/overflow rollback, destroy/id reuse |
| Physics cache/batch | Raw shape/pose snapshots, unchanged Bullet bodies retained; raycast_many — одна sync на batch | simulation: оба backend, cached body counters, changed pose/collider, destroy/id reuse, batch validation |
| Python bridge | Native dict/list/scalar conversion, независимые copies; JSON fallback для нестандартных ключей/типов | rendering: nested copies, Unicode, tuples, uint64/big ints, legacy key coercion, rejected NaN/Infinity/cycles/types |
| Разделение реализации | python_api/scene_runtime/reload/python_bridge; particle_render/gpu_resources/text/media | все прежние native/GPU contracts; relocated core сохраняет происхождение, один Python runtime остаётся границей |

Минимальная сложность для игры: `engine.json` + сцена; все новые модули необязательные. Defaults прежних проектов сохраняются: project storage, базовый свет при пустом lights, прежние play_sound/Entity/UI API. Пример `advanced.json` показывает расширения вместе, `simulation.json` — Bullet и частицы, `materials.json` — PBR, иерархия и живые меши; `editor-empty.json` — начало авторской сцены.

Границы: legacy-физика остаётся AABB; Bullet поддерживает только box/sphere/capsule без mesh-коллайдеров, joints, navmesh и автоматического step climbing. Частицы — CPU-симуляция без столкновений, GPU compute и теней; локальная привязка переносит позицию без вращения/масштаба владельца. Остальные границы: PBR без IBL/environment maps и HDR pipeline, одна shadow map без cascades, TRS skinning без blend tree/morph/retarget, text без bidi/shaping, редактор без visual scripting/gizmos. Режим replay фиксирует ввод/dt, а не внешние сервисы и RNG. Crash reporter не отправляет данные автоматически. Windows должен пройти CI/локальные тесты на своей машине: исполнение macOS не подтверждает Windows binary.

CI запускает все шесть CTest suites на macOS/Windows и четыре GPU suites на Linux с Mesa/Xvfb и виртуальным аудиоустройством. Workflow описывает будущие проверки; его наличие не доказывает успешное исполнение. Графические наборы отдельно от CTest. Контракты PBR/иерархии/геометрии и воспроизводимые команды — раздел 20 инструкции.

Динамическое физическое тело должно быть корнем дерева; визуальные дети разрешены, дочерние коллайдеры — статические/кинематические. Матрица рендера наследует scale, размеры коллайдера задаются явно. Blend сортируется по центрам объектов, без OIT и общей сортировки с частицами. В материале фиксированы стандартные каналы и один UV set; произвольный vertex layout требует изменения графического модуля. Процедурные меши не сериализуются в scene JSON. Состояние Python, ввод/окно/GPU пока не полностью разделены: множество параллельных Python runtimes не поддерживается.

Профилирование 2.3: `tools/benchmark_hotpaths.py` создаёт временный проект и измеряет scalar setters, повторные raycast, Python/JSON roundtrips; `--particles` добавляет настоящий GPU-прогон. Это локальный benchmark без timing assertions, а не гарантия FPS. Bullet и нативные публичные поля по-прежнему требуют линейного аудита, legacy raycast сканирует коллайдеры; `raycast_many` сокращает число аудитов. IBL/HDR, cascaded shadows, mesh colliders, joints, navmesh и bidi/shaping в этом этапе не добавлены.

## Forge 2.4

| Возможность | Реализация | Проверка |
| --- | --- | --- |
| Prefab hierarchy/inheritance | prefab.cpp, modules/prefabs.py, JSON Merge Patch, local IDs/atomic create | authoring: independent copies, attachment, cycles, duplicate IDs, world overflow rollback |
| Layered animation/playback | animation.cpp/api, masks, TRS blends, interruptible crossfade, markers/finished queues | authoring: weighted poses, authored bone keys, pause/seek/loops/events/budget rejection |
| Retarget | source-to-target names, local rest TRS delta, translation_scale | authoring: renamed/proportioned skeleton; rendering_graphics: actual movement pixels |
| Morph targets | Assimp deltas/default/animated weights, CPU deformation before GPU skinning | authoring: named weights/numeric failures; rendering_graphics: deformation and per-instance pixel isolation |
| Property clips/state machine | animation.py tracks, events, parameters, triggers and exit_time | authoring: 2D properties/playback and native-backed graph transitions |
| Animation editor | animation_editor.cpp, Inspector layers/scrub/markers/morphs/retarget JSON/prefab export | rendering_graphics: native panel opens/render; authoring tests cover applied API; button interactions are manual QA |
| Dispatch | dispatch.cpp, listener IDs and per-entity contact changes | authoring: 5000 listeners/removal/addition; integration/simulation: lifecycle/contact behavior |
| Background watch | file_watch.cpp worker; prepared snapshots of paths/python_paths | authoring/integration: reload, path changes/rollback; metadata scan, no content hash guarantee |
| Stable extensions/build | api_version/capabilities, light contracts, shader configure dependencies, Threads | compile/CTest; CI verify_package hashes/notices/three private-Python launches |

Семь CTest suites и четыре GPU suites. Retarget не включает IK/anatomy inference/foot locking; morphs используют CPU upload, до 32 targets/part. Визуальный редактор редактирует playback/layers/markers/settings и TRS bone keyframes; node-graph state machine отсутствует. Один активный Python runtime сохраняется; renderer остаётся крупным GPU orchestration модулем. Остальные ранее описанные функциональные границы действуют. Подробные defaults, budgets и миграция legacy playback — раздел 22 инструкции.
