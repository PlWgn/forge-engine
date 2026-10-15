# VR: OpenXR, SteamVR and Meta Quest

Forge 2.11 adds optional OpenXR for **Windows PC VR** (SteamVR or Meta Quest Link/Air Link) and an **Android arm64 standalone host** for Quest 3 and Quest 3S. The same C++ runtime, Python scenes, world entities, physics, assets and VR APIs serve both targets. Flat-screen projects keep XR disabled. OpenXR is independent of Steamworks: no Steam account, Steam App ID or Steam SDK is required by this module.

The implementation targets OpenXR primary stereo, Windows OpenGL 4.5 and Android OpenGL ES 3.2. Metal/Direct3D XR bindings, Linux XR hosting, hand skeletons, passthrough, eye tracking, spatial anchors, composition-layer UI and foveation are not implemented. Headset and store verification remain separate from compilation and headless tests.

## PC VR

On Windows, in the engine checkout and your activated Python development environment:

```powershell
python tools/dependencies.py --openxr
python tools/forge.py compile --openxr
```

Add to your game's `engine.json`:

```json
{
  "entry_scene": "vr.py",
  "renderer": {"backend": "opengl"},
  "xr": {
    "enabled": true,
    "reference_space": "local",
    "near": 0.05,
    "far": 1000,
    "meters_per_unit": 1,
    "eye_budget_bytes": 268435456
  }
}
```

This fragment supplements a complete Forge project. Generate one with `init` or adapt the bundled project; keep the existing paths, scripts and required fields. `scenes/vr.py` is an editable world-space room/controller example, also usable as a desktop preview with `xr.enabled=false`. The example chooses a 1.6-meter rig offset for local space as an editable game policy, not a measured floor. It supplies an attribution label as example content; a public game must supply its own startup and menu/About notices under [ATTRIBUTION.md](../../ATTRIBUTION.md).

Set SteamVR as the active OpenXR runtime for SteamVR headsets. For Quest over USB/Wi-Fi, select the active OpenXR runtime for the installed Quest Link software and connect the headset. Forge asks the system's active runtime; it never edits the user's runtime registry or launches a platform service automatically. Connect and wake the headset before running:

```powershell
python tools/forge.py run --project ../MyVRGame/engine.json --no-open-log
python tools/forge.py dev --project ../MyVRGame/engine.json --no-open-log
python tools/forge.py build --project ../MyVRGame/engine.json --output dist/MyVRGame
```

Directory packaging retains `openxr_loader.dll`, private desktop Python and all loader notices. Keep them beside `Game.exe`. Distribute that Windows build through your Steam application using your Steamworks distribution configuration. Building an executable does not upload it to Steam or configure its store page. A Steamworks-enabled game may use the existing independent [Steam module](networking.md).

A build without OpenXR reports an explicit error when graphical XR startup is requested. Config validation and headless simulations work on other development hosts without a headset. OpenXR's runtime requirements are queried before session creation. Unsupported OpenGL versions, missing runtimes/headsets, unavailable reference spaces, swapchain formats or resource budgets report errors; Forge does not silently switch to a flat view.

## Standalone Quest APK

Standalone Quest runs **Android**, without a PC or desktop Python installation. Use the same project with a 3D entry scene. Install these developer tools yourself:

- JDK 17 or a compatible newer JDK, Gradle 8.11.1, Android SDK platform 35 and SDK CMake 3.31.6.
- Android NDK r28c (`28.2.13676358`), with Android SDK licenses accepted in your own development setup.
- An official **aarch64 Android CPython 3.14** embeddable distribution, or a compatible 3.13+ target build with headers, shared library, standard library and license. Supply an Android ABI build: the tool rejects non-arm64/non-ELF libraries, and architecture checking alone does not certify compatibility. Set `--python-version` to match the supplied prefix.
- The normal Forge source dependencies plus `python tools/dependencies.py --openxr`. The desktop engine must be compiled for project/media/Python syntax validation.

See [Python's Android embedding instructions](https://docs.python.org/3.14/using/android.html) and [official Android packages](https://www.python.org/downloads/android/). Extract the archive to a directory containing `include/python3.14/Python.h`, `lib/libpython3.14.so`, `lib/python3.14/encodings` and the CPython license. Preserve its additional `lib*_python.so` dependencies and licenses. Supply Android wheels for any additional native Python modules; desktop extension modules cannot run on a headset.

Point `ANDROID_HOME` at your Android SDK and put Gradle on PATH, then run from the engine checkout:

```sh
python tools/forge.py build-quest \
  --project ../MyVRGame/engine.json \
  --output dist/MyQuestGame \
  --python-prefix /path/to/android-python-prefix \
  --python-version 3.14 \
  --application-id com.example.myvrgame
```

On PowerShell, use one line or PowerShell backticks for continuation. Paths containing spaces/Unicode work when quoted. `--gradle /path/to/gradle` selects an installed Gradle executable. `--version-code 2` sets the Android package update counter; this is separate from Forge's engine/API/schema versions.

The command validates the project with the desktop engine, stages a Gradle project in a **new output directory**, compiles the same engine sources for `arm64-v8a`, runs `assembleDebug`, checks the exact NativeActivity library name and exported entry point/arm64 dependencies and content hashes, and writes `Game.apk` there. It never replaces an existing output. `--prepare-only` stages the project without invoking Gradle; that produces **no APK**. A failed Gradle build leaves its project and diagnostics available for repair. `tools/quest.py --help` exposes the lower-level staging/build options, including a different installed NDK version.

The staged game selects OpenGL ES, enables XR and forces user storage; saves/preferences/logs are outside replaceable content in the app's private data directory. Content paths and `python_paths` retain their configured meanings. Nested symlinks/cycles/output aliases are validated before copying. Packaging includes hashes, source/dependency notices, private target Python and its license; it does not substitute host Python. The developer-supplied prefix must preserve notices for its bundled native libraries too.

Install a development APK on your developer-enabled headset:

```sh
adb install dist/MyQuestGame/Game.apk
adb logcat -s Forge
```

The manifest declares Quest 3 and Quest 3S (`quest3|quest3s`), OpenXR runtime discovery, arm64 and head tracking. Android activity pause/resume/window/destroy events are pumped while the OpenXR session controls focus and frame submission. Native rendering uses a persistent EGL pbuffer context; the headset compositor owns the display. There is no Android desktop mirror or builtin editor. Android log messages go to both the app log file and logcat. Read screenshots from a desktop preview; Android default-framebuffer screenshots are unsupported.

The generated APK uses Gradle's **development/debug signature**. For public distribution configure the generated project's release signing with your own keystore outside Git, build a release APK, and complete Meta's current validation/submission requirements. This module does not publish, buy access, create accounts, embed certificates, or promise store acceptance. Configured `native_modules` are compiled for Android from their confined project source paths; their code/dependencies must support the target. Android also requires Android-compatible external Python packages. Steamworks and the desktop GLFW/ImGui shells are excluded from the Quest host; optional Android networking adapters can be added independently.

## Configuration contract

| Field | Default | Meaning |
| --- | --- | --- |
| `xr.enabled` | `false` | Enable graphical OpenXR startup |
| `xr.reference_space` | `"local"` | `local` or `stage`; unavailable stage is an error, not a guessed floor |
| `xr.near` | `0.05` | Near clip in game world units; finite `0.001..1000` |
| `xr.far` | `1000` | Far clip in world units; finite `0.002..100000`, greater than near |
| `xr.meters_per_unit` | `1` | Meters represented by one world unit; finite `0.0001..10000` |
| `xr.eye_budget_bytes` | `268435456` | Estimated two-eye swapchain color + owned depth budget, integer 1 MiB..1 GiB |

OpenXR provides poses in meters; Forge divides their translations by `meters_per_unit`. Rotations are unchanged. Coordinates are right handed, +Y up, -Z forward. A `local` origin is runtime-defined, often near the initial head location, and does not guarantee floor height. `stage` uses the runtime's floor space. Your game chooses its rig offset and movement rules.

All XR configuration changes require a runtime restart and are rejected during live graphical reload, preserving the session/resources. Unknown configuration metadata is retained. Headless runs do not create a session. Enabling XR requires a 3D scene; invalid 2D scene candidates are rejected before replacement.

The eye-buffer budget is additional to `renderer.gpu_budget_bytes`, which covers Forge's intermediate eye color target and shared scene resources. It estimates runtime color image storage and owned depth textures; it is **not** a whole-device VRAM limit or compositor/process-memory cap. Eye images follow the runtime's recommended dimensions and use one sample. XR color presentation converts Forge's display-referred scene output into linear RGBA8 after blending. A runtime that does not offer RGBA8 swapchains reports an error. Eye postprocessing is currently limited to that color conversion; desktop post effects apply to the mirror, not the headset.

## Python API

`forge.capabilities()['openxr']` reports compilation support, not headset availability. `forge.xr_state()` returns a detached native snapshot; it also appears in `forge.input_snapshot()['xr']`. Outside a graphical XR session the snapshot is `{'enabled': False}`.

A running snapshot contains `enabled`, `running`, `focused`, `runtime`, numeric `session_state`, `should_render`, `views_valid`, `reference_space`, `predicted_time_ns`, `predicted_period_ns`, `head` and `hands`. Nanosecond times belong to the OpenXR clock; do not compare them with Unix time. Check `valid` before reading a pose. Valid poses contain world `position`, quaternion `orientation` in **[x,y,z,w]** order, and `tracked` (both position and orientation actively tracked). A valid pose can be runtime-estimated when tracking is temporarily lost.

When focused, `hands['left']` and `hands['right']` contain `grip`, `aim`, `trigger`, `squeeze`, `stick` `[x,y]`, and boolean `select`, `primary`, `secondary`, `menu`. Unsupported controls are neutral. Outside focus the hands map is empty; stale button values are never returned. Suggested interaction profiles include Khronos simple controllers, Oculus Touch, Valve Index and HTC Vive. `select` is the simple-controller select/Vive trigger click; Touch/Index gameplay uses the analog `trigger` or face buttons. Menu bindings are exposed only where the runtime permits them; reserved system buttons are never intercepted.

```python
import forge
from vr_input import ControllerInput, aim_ray

controls = ControllerInput()

def on_update(dt):
    frame = controls.update()
    if not frame.get('focused'):
        return
    right = frame['hands']['right']
    if right['aim']['valid']:
        origin, direction = aim_ray(right['aim'])
        # Use your game's raycast/interaction/controller policy here.
    if controls.pressed('right', 'trigger'):
        forge.xr_haptic('right', amplitude=0.4, duration=0.08)
```

The independent, editable `modules/vr_input.py` provides `ControllerInput` (threshold/edge detection and refocus suppression), `pose_rotation` (valid quaternion to Forge Euler degrees) and `aim_ray` (world origin/direction). It owns no runtime, persistence or frame listener. Call `update` once per game frame; pass an explicit snapshot for deterministic tests. Keep any needed UI updates running when gameplay is paused.

`forge.xr_set_origin([x,y,z], yaw=0)` moves the player rig without changing tracking space. Position and yaw must be finite and bounded to +/-1e9; yaw is degrees around +Y. Set it during `on_update` after initialization. The next frame poll applies the rig to both eyes and pose snapshots together. It does not move colliders, perform locomotion, recenter the runtime or persist settings. Pose/input snapshots stay fixed for the sampled frame; a new origin appears in the next snapshot.

`forge.xr_haptic(hand, amplitude=.5, duration=.1, frequency=0)` targets `left` or `right`. Amplitude is `0..1`, duration `(0..5]` seconds, frequency `0..1000` Hz (`0` means runtime default). It returns false outside focus/running, otherwise submits the bounded request; runtime/device support determines physical vibration. Initialization, reload/authoring candidates and teardown reject origin/haptic side effects. This prevents failed candidates from moving a live rig or sending physical feedback. Nothing can undo feedback already sent during ordinary gameplay.

## Rendering and lifecycle

Native polling handles session READY/STOPPING/EXITING/loss events, waits/begins one predicted frame, locates both views and samples actions **before** script updates. Scene updates/physics/animation/particle simulation execute once. Eye submission renders the shared scene twice with separate runtime poses, asymmetric FOV matrices and per-eye optimization identities. Gameplay must never update inside an eye draw callback.

World-space meshes/text/particles, materials, lights, shared shadows and camera render targets use the existing scene path. Screen-space UI stays in the PC mirror; use world-space UI or supply a future composition-layer extension for headset menus. Desktop camera settings control the mirror, while tracked eyes use `xr_set_origin` plus OpenXR poses. Eye images are acquired, waited, rendered, flushed and released before projection submission. `shouldRender=false` or invalid views submit no projection layer. Draw failures cancel outstanding frames; session shutdown releases swapchains/spaces/actions before destroying the graphics context. Paused gameplay continues polling/submitting XR frames.

No built-in locomotion, teleportation, player capsule, interaction rules, guardian replacement, networking replication or genre-specific menus are imposed. Implement those in game modules using Forge's normal APIs. OpenXR adapters (`xr.hpp`, `xr.cpp`, `xr_openxr.cpp`), Android lifecycle/window hosting and Python helpers are adaptable components. The Android interpreter bootstrap retains its `main.cpp` Core origin, as documented in the [Core boundary](../../CORE.md); the exhaustive LICENSE list is unchanged.

## Verification

```sh
ctest --test-dir build -C Release --output-on-failure -R "^(xr_native|xr_bridge|quest_packager)$"
```

Native tests verify asymmetric projection boundaries/depth and numeric/configuration rejection. Bridge tests exercise real embedded Python, headless capability/defaults and portable validation. Packaging tests cover source/output/symlink confinement, target ELF rejection, Unicode paths, private Python, licenses and hashes. Existing lifecycle/rendering/project/authoring tests protect desktop compatibility. CI compiles the optional Windows adapter and Android APK target; automated builds without a headset do not verify tracking or displayed pixels.

Before shipping, run on actual Windows/SteamVR, Quest Link, Quest 3 and Quest 3S: eye separation/FOV/orientation/color/depth, controller profiles/haptics, focus loss/refocus, sleep/wake, runtime quit, stage/local spaces, scale/rig movement, rendering failure/recovery, paused reload/rejection, asset budgets, standalone launches and persistence across APK updates. Check frame timing and thermal behavior for your content on each target. A Windows cross-compile or macOS preview cannot establish Quest pixels, performance, controller mapping or store compliance.

Protocol/platform references: [Khronos OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html), [Khronos tutorials](https://github.com/KhronosGroup/OpenXR-Tutorials), [Meta Android manifest requirements](https://developers.meta.com/horizon/documentation/native/android/mobile-native-manifest/).
