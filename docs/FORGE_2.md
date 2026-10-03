# Forge 2.0 — реализация отчёта об опыте

Исходный [отчёт Forge 1.2.1](FORGE_EXPERIENCE.md) сохранён как описание опыта игры. Новый выпуск реализует перечисленные в нём возможности и подтверждённый пользователем дополнительный 3D-набор. Публичные API и примеры: [Инструкция.md, раздел 18](../Инструкция.md).

| Запрос из отчёта | Реализация | Проверка |
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
| AI/state/timeline | Scheduler budget/clock, StateMachine, Timeline/Sequence | features: порядок/пауза/loop/отмена |
| Reload/persistence | Автоматический defer для core save/SaveManager, explicit deferred callback | features: отказ кандидата не меняет три файла, успех меняет |
| .app / подпись | Bundle/Info.plist/icon/private Python; ad-hoc build и команды Developer ID/notarytool | features: codesign/manifest/launch; настоящая нотарификация требует credentials |
| User dirs/window/crash | Application Support/AppData APIs, preferences/UI, crash JSON/native marker | features: storage вне игры, error reports, UI preferences |
| Character controller | Swept AABB slide, gravity/grounded jump | features: большое движение блокируется стеной, вертикальная опора/прыжок |
| Свет/тени | 16 directional/point/spot, ambient, одна PCF directional shadow map | GPU: кадры с тенями/без теней различаются |
| Skeletal animation | Assimp hierarchy/TRS clips, 128 bone GPU palette / 4 weights | оригинальный glTF fixture + pose/GPU кадры |
| glTF/GLB/FBX/DAE | Assimp importers, материалы и embedded/external textures | glTF fixture + FBX box; остальные варианты зависят от импортера |
| Редактор | ImGui hierarchy/inspector/assets, transform/collider/text/light/camera, add/delete/duplicate, JSON/undo/preview | окно editor + serialization; ограничения Python state описаны |

Минимальная сложность для игры: `engine.json` + сцена; все новые модули необязательные. Defaults прежних проектов сохраняются: project storage, базовый свет при пустом lights, прежние play_sound/Entity/UI API. Пример `advanced.json` показывает расширения вместе; `editor-empty.json` — начало авторской сцены.

Границы: AABB без capsule/slope/navmesh, базовое lighting без PBR, одна shadow map без cascades, TRS skinning без blend tree/morph/retarget, text без bidi/shaping, редактор без visual scripting/gizmos. Режим replay фиксирует ввод/dt, а не внешние сервисы и RNG. Crash reporter не отправляет данные автоматически. Windows должен пройти CI/локальные тесты на своей машине: исполнение macOS не подтверждает Windows binary.
