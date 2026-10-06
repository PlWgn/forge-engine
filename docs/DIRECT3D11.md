# Direct3D 11 and Editable Shaders (Forge 2.6+)

Forge keeps OpenGL 3.3 and adds an optional Direct3D 11 backend on Windows. Forge 2.7 also adds [Metal on macOS](METAL.md); auto prefers it on builds with Metal enabled. Game scenes, Python modules, materials, geometry, animation, text, particles, cameras, and editor commands keep the same API. Select the device in `engine.json`; do not put platform-specific drawing rules in game scripts.

## Selecting a Backend

```json
{
  "renderer": {
    "backend": "direct3d11",
    "direct3d11": {
      "driver": "auto",
      "debug": false
    }
  }
}
```

This is a configuration fragment, not a complete project. Preserve the other renderer settings when adding it.

| Setting | Behavior |
| --- | --- |
| Omitted `backend`, or `opengl` | Existing OpenGL 3.3 path on macOS/Windows; previous default is unchanged |
| `direct3d11` | Direct3D 11; an unavailable platform/build fails with a clear error |
| `auto` | Direct3D when compiled in; Metal on macOS when compiled in; otherwise OpenGL. This selects by build capabilities, not by benchmarking drivers |
| `direct3d11.driver: auto` | Try hardware, then WARP with a warning and driver diagnostics |
| `hardware` | Require a hardware Direct3D device; no software fallback |
| `warp` | Windows software Direct3D; useful for tests and machines without a suitable GPU |
| `direct3d11.debug: true` | Request the Windows debug layer; Graphics Tools must be installed |

Direct3D requires Windows 10 or newer, feature level 11_0, and Shader Model 5.0. The implementation uses the Windows SDK's D3D11/DXGI/compiler APIs; no DirectX service, account, network access, or separate proprietary shader tool is required at runtime. It is Direct3D **11**, not Direct3D 12, DXR, or a new lighting/physics system. macOS retains OpenGL.

```python
import forge

print(forge.graphics_backends())             # Compiled backends, not a GPU probe.
print(forge.capabilities()['graphics_backends'])
# After a rendered frame:
print(forge.renderer_stats()['backend'])
```

Direct3D stats also expose `driver`, `adapter`, `feature_level`, `debug`, `translated_programs`, `native_programs`, and `presentation_bytes`. The editor-neutral `project` API's capabilities response includes `graphics_backends`, so every shell can discover build support without creating a window.

Changing `backend`, the Direct3D driver, or the debug layer requires restarting the runtime. Hot reload rejects these changes and retains the current device, scene, and resources. Shader, material, and other supported content edits still hot reload transactionally.

## Building and Launching

Windows builds include both backends by default with Visual Studio 2022 and the Windows SDK:

```powershell
python tools/dependencies.py
python tools/forge.py compile
python tools/forge.py dev
```

For a Windows OpenGL-only build:

```powershell
python tools/dependencies.py --without-directx
python tools/forge.py compile --without-directx
```

`--without-editor` is independent: Direct3D run/dev/build and custom shells work without ImGui. CMake exposes `FORGE_WITH_DIRECT3D11` and `FORGE_WITH_SHADER_TRANSLATOR`. Explicitly enabling Direct3D on a non-Windows target fails configuration. Disabling its translator while retaining Direct3D also fails rather than silently losing GLSL support.

On macOS in Forge 2.7, ordinary bootstrap/compile includes OpenGL and Metal. Use --without-metal for OpenGL-only; the optional shader compiler can be tested without a Direct3D device:

```sh
python tools/dependencies.py --shader-tools
python tools/forge.py compile --shader-tools
ctest --test-dir build -C Release --output-on-failure -R shader_compiler
```

The translator is statically linked into the Windows runtime. Its pinned glslang/SPIRV-Cross sources are downloaded by bootstrap; CMake performs no downloads. OpenGL-only builds do not require them. Game packaging retains compiler licenses and includes shader sources. Windows 10 supplies `d3dcompiler_47.dll`; no Microsoft runtime DLL is copied from a developer's SDK into the project.

`--silent-audio` runs PCM audio without opening a hardware audio device, independently of graphics. It is available through native run/dev/edit, the launcher, and SDK `Client.launch(silent_audio=True)`. It validates engine audio processing, not speaker playback.

## Portable GLSL: Existing Projects Keep Their Shaders

Keep editing files under the configured `paths.graphics` directory. Existing fields remain available:

- `vertex_shader` / `fragment_shader`: scene drawing;
- `post_shader`: postprocessing;
- `particle_vertex_shader` / `particle_instance_shader` / `particle_fragment_shader`: particle paths;
- `shadow_vertex_shader` / `shadow_fragment_shader`: optional custom shadow stages, added in 2.6.

OpenGL compiles GLSL with its driver. Direct3D links GLSL stages with glslang, converts SPIR-V to HLSL with SPIRV-Cross, then compiles Shader Model 5 through the Windows compiler. It does not rewrite user shaders using string substitution. Linked varyings, custom uniforms, skeletal arrays, derivatives, texture queries, and particle instancing follow the existing draw contract.

`forge.set_shader_uniform(...)`, entity uniforms, and postprocess uniforms keep their API. Missing/optimized-out uniforms are ignored. Shader failures include compilation/link diagnostics in the terminal/log. A rejected development reload releases the candidate and retains the previous programs/resources.

Backend portability covers Forge's draw API: vertex/fragment stages, float/int/uint vertex attributes, scalar/vector/mat4 uniforms and arrays, and up to 16 sampler2D resources per stage. The backend is not a complete OpenGL emulator. Compute/geometry/tessellation stages, SSBOs, texture arrays/cubes, arbitrary uniform-buffer APIs, and GPU-specific extensions require an extension or backend changes. Unsupported translation/resource contracts produce errors rather than a fallback that changes drawing silently.

## Native HLSL Overrides

Native HLSL is optional. Keep a GLSL path for OpenGL and provide complete HLSL stage pairs for selected Direct3D pipelines:

```json
{
  "renderer": {
    "backend": "auto",
    "vertex_shader": "default.vert",
    "fragment_shader": "default.frag",
    "direct3d11": {
      "shaders": {
        "scene": {
          "vertex": "direct3d11/unlit.vert.hlsl",
          "fragment": "direct3d11/unlit.frag.hlsl"
        }
      }
    }
  }
}
```

Both files are editable examples. This override deliberately supplies **unlit** scene shading; it does not replace the portable default PBR shader unless configured. Imported skeletal transforms, UV regions, vertex colors, textures, and alpha mask/blend remain available in that example.

Supported pipeline keys are `scene`, `post`, `shadow`, `particles`, and `particles_instanced`. Unspecified pipelines use their GLSL implementation. Each override requires `vertex` and `fragment`, resolved safely within the configured graphics directory/project root. OpenGL ignores native override selection; project validation checks referenced files on either platform. Headless validation checks paths/configuration, not HLSL compilation or actual Direct3D pixels.

Native shader entry points are `main`; profiles are `vs_5_0` and `ps_5_0`. Use `TEXCOORDn` vertex semantics corresponding to attribute location n. Keep pipeline varyings consistent between your stages. Scene attributes are position 0, UV 1, normal 2, bone indices 3, weights 4, and color 5. See the GLSL particle files for legacy/instanced particle layouts. Exposed constant buffers may contain scalar/vector/mat4 uniforms and arrays, with the same names used by the scene/particle draw contract; arbitrary structs/resources are outside this API.

Native HLSL matrices use ordinary column-major storage and `mul(matrix, vector)`. Explicit row-major matrices are transposed when uploaded. Generated HLSL follows SPIRV-Cross's row-vector convention with matching row-major compiler packing. Game coordinates, GLM projections, UV conventions, camera targets, screenshots, and transparency ordering do not change with the backend.

The device retains GL-oriented offscreen buffers and flips them when presenting. A native vertex shader receiving a Forge GL projection must convert its final clip position, as the example does:

```hlsl
float4 forge_clip_position(float4 value) {
    value.y = -value.y;
    value.z = (value.z + value.w) * 0.5;
    return value;
}
```

Use `Texture2D u_texture` and `SamplerState u_texture_sampler`, or matching texture/sampler registers `tN`/`sN`. The same texture uniform names determine Forge texture units. Native shader text is a self-contained compilation unit; include-file preprocessing is not provided. You may modify the loader/compiler to add a project-safe include policy.

## Device Boundary and Freedom to Extend

`graphics_device.hpp/.cpp` separates device selection, presentation, program overrides, and editor integration. `direct3d11.hpp/.cpp` implements independent Windows resources and the existing GL-shaped draw commands. `shader_compiler.hpp/.cpp` owns portable compilation; `graphics_settings` owns validation. The renderer keeps backend-independent scene/resource orchestration. OpenGL's driver loader remains in `gl.hpp`.

These are adaptable implementations outside the exhaustive license Core list. Shaders and device code may be changed or replaced without modified-Core attribution by themselves. The Core-origin configuration/CLI/Python contracts changed only to validate configuration, expose capabilities, and support silent audio; their origin remains described in CORE.md. One active graphics/Python runtime per process remains supported; this release does not claim device isolation or multiple simultaneous runtimes.

Direct3D allocates an internal color/depth presentation pair in addition to the swapchain. `presentation_bytes` reports that pair (8 bytes/pixel); the swapchain/driver allocations and CPU mirrors are not part of the renderer's asset budget. Feature parity does not imply identical pixels, memory usage, or performance between drivers. Device removal reports diagnostics and terminates normal execution; automatic device recreation is not implemented.

## Verification

Eight base CTest suites remain. Enabling the translator adds `shader_compiler`: portable default/PBR/post/particle/custom/shadow translation, sampler limits, mismatched stages, and invalid-source diagnostics. On Windows this test also invokes the real D3D compiler on translated shaders and the native examples.

The four shared GPU suites can run against either backend. Windows CI uses WARP and silent audio:

```powershell
$env:FORGE_TEST_BACKEND = 'direct3d11'
$env:FORGE_TEST_SILENT_AUDIO = '1'
python tests/graphics.py build/bin/Release/forge.exe
python tests/features_graphics.py build/bin/Release/forge.exe
python tests/simulation_graphics.py build/bin/Release/forge.exe
python tests/rendering_graphics.py build/bin/Release/forge.exe
python tests/direct3d_graphics.py build/bin/Release/forge.exe
python tools/verify_package.py dist/Game --graphics-backend direct3d11
```

The Direct3D-specific suite covers native HLSL uniforms/UV/clipping/orientation, rejected shader reload/recovery, auto selection, and GLSL uniform names that require HLSL keyword renaming. Shared suites cover PBR, cameras/shadows/postprocessing, skinning/morphs, particles, text/UI, and editor pixels. The package verifier modifies a disposable copy, poisons host Python variables, launches three graphical scenes from another directory, and confirms the selected backend.

This update was developed on macOS: shader translation and OpenGL can be executed locally; Windows-specific code can be cross-compiled. Neither proves Windows device behavior. Windows CI/WARP runs must pass before treating Direct3D runtime rendering as verified. WARP does not validate physical GPU performance or hardware audio.

Local validation passed nine CTest suites with the editor/translator enabled, eight base suites with both disabled, all 30 shared OpenGL GPU tests, an alternative viewport shell without ImGui, a fresh project validate/run, and standalone headless/OpenGL verification. The Direct3D source compiled into a Windows object; an editor-free link probe resolved the Windows graphics libraries with stubbed engine services. This is a cross-compilation/link check, not a full Windows engine build or an executed Direct3D test. Windows CI has been configured but was not run during this macOS session.

Implementation references: [Microsoft Direct3D/WARP](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-create-warp), [Khronos glslang](https://github.com/KhronosGroup/glslang), and [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross).
