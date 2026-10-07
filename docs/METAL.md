# Metal on macOS (Forge 2.7)

Forge adds optional native Metal rendering for Apple Silicon while retaining OpenGL 3.3 and the Windows Direct3D 11 backend. Scenes, Python scripts, materials, animation, text/UI, particles, cameras, postprocessing, and editor commands use the same APIs. Shader sources and backend implementations remain editable, adaptable modules outside the exhaustive license Core list.

## Select the Backend

Add the backend to the existing renderer object in `engine.json`:

```json
{
  "renderer": {
    "backend": "metal"
  }
}
```

This is a fragment; retain your other project settings. Omitting `backend` or selecting `opengl` preserves the previous default. `metal` requires a macOS build with Metal enabled and a Metal-capable device. An explicit unavailable backend reports an error. `auto` now chooses Metal when compiled on macOS, Direct3D 11 on Windows, and OpenGL otherwise; it selects build capabilities, without benchmarking or falling back after a native-device initialization failure. Existing projects that explicitly chose `auto` on macOS may therefore render through Metal after rebuilding in 2.7. Choose `opengl` to retain their previous device.

Metal targets macOS 11+ and Metal Shading Language 2.0. The implementation is tested on Apple M2; Intel Metal GPUs are not locally verified. Runtime shader compilation uses macOS's system Metal compiler and does not require the `xcrun metal` command or a full Xcode installation. Building the engine needs Command Line Tools and the standard pinned dependencies.

```python
import forge
print(forge.graphics_backends())  # Compiled support, not a device probe.
print(forge.capabilities()['graphics_backends'])
# After a rendered frame:
print(forge.renderer_stats())
```

Metal diagnostics include `backend`, `adapter`, `unified_memory`, `shader_language`, `translated_programs`, `native_programs`, `presentation_bytes`, `uniform_arena_bytes`, and `uniform_budget_bytes`. Since 2.9.2 they also expose `draw_pipeline_builds`, `draw_pipeline_cache_entries`, `command_submissions`, `gpu_waits`, `synchronous_flushes`, `inflight_command_buffers`, `peak_inflight_command_buffers` and `max_inflight_command_buffers`. Build/submission/wait counters are cumulative for the device; renderer stats snapshot the preceding rendered frame before presentation. Pipeline builds count draw variants, excluding shader-link reflection probes, clear and presentation pipelines. `gpu_waits` counts actual blocking completion calls; `synchronous_flushes` counts explicit drains with outstanding work, excluding in-flight/budget backpressure. Editor-neutral project capabilities expose compiled backends without creating a window.

Changing devices or the Metal uniform budget requires restart. Development reload rejects backend changes and retains the working scene/resources/device. Shader, material, and supported content edits still reload transactionally. OpenGL/Direct3D settings may coexist with Metal settings in the same project; shells preserve unknown fields.

## Build and Run

On macOS, Metal is compiled alongside OpenGL by default:

```sh
python tools/dependencies.py
python tools/forge.py compile
python tools/forge.py dev
```

After selecting `metal` in your project, the same run/dev/edit/build commands use it. No editor is needed for run/dev/build. `compile --without-editor` keeps both rendering backends and external viewport shells while omitting ImGui.

An OpenGL-only macOS build is available:

```sh
python tools/dependencies.py --without-metal
python tools/forge.py compile --without-metal
```

CMake uses `FORGE_WITH_METAL`, independent of `FORGE_WITH_EDITOR` and `FORGE_WITH_DIRECT3D11`. Metal requires `FORGE_WITH_SHADER_TRANSLATOR`; enabling Metal outside macOS or disabling its translator is a configuration error. With Metal disabled, Objective-C++ and Metal frameworks are not required by Forge's backend. `--shader-tools` may still build translator tests on any platform.

The existing pinned glslang/SPIRV-Cross sources supply GLSL parsing and MSL generation. Standard macOS bootstrap now fetches these compiler dependencies for Metal; OpenGL-only builds can omit them. Their pinned versions and checksums are unchanged. Shader sources and compiler notices/licenses are retained in standalone packages; Metal/Cocoa/QuartzCore are macOS system frameworks. No Apple SDK component is redistributed. Standalone launch uses private Python and does not depend on a developer's working directory or Python environment.

## Editable Portable Shaders

Continue editing GLSL files under the configured `paths.graphics`. Existing scene, postprocess, particle, instanced-particle, and shadow shader fields remain available. OpenGL compiles GLSL directly. Direct3D translates to HLSL; Metal translates to MSL through glslang/SPIR-V/SPIRV-Cross, then compiles it with the Metal device. Translation links stages and matches varying locations across declaration orders; it does not substitute strings in user source.

`forge.set_shader_uniform`, per-entity uniforms, and postprocess uniforms keep their names and types. Translation retains public names when MSL keywords require renaming and logical scalar-array shapes when the compiler pads them to vectors. Missing or optimized-out uniforms are ignored. Errors include shader/pipeline diagnostics in terminal/log; rejected reload preserves old pixels/resources.

The portable draw contract covers vertex/fragment stages, 16 float/int/uint32 vertex attributes, scalar integer/bool and float-vector uniforms, mat4 uniforms/arrays, and 16 individual sampler2D resources per stage. Compute, geometry/tessellation, arbitrary UBO/SSBO interfaces, storage images, texture cubes/arrays, nested uniform structures, and multidimensional uniform arrays require backend/API extensions. Unsupported contracts report errors. Metal is an implementation of Forge's command vocabulary, not a complete OpenGL emulator.

## Native Metal Shader Overrides

Native MSL is optional. Keep portable GLSL settings and override selected Metal pipeline pairs:

```json
{
  "renderer": {
    "backend": "auto",
    "vertex_shader": "default.vert",
    "fragment_shader": "default.frag",
    "metal": {
      "shaders": {
        "scene": {
          "vertex": "metal/unlit.vert.metal",
          "fragment": "metal/unlit.frag.metal",
          "vertex_entry": "vertex_main",
          "fragment_entry": "fragment_main"
        }
      }
    }
  }
}
```

The editable examples provide unlit scene shading and retain textures, UVs, vertex colors, skeletal transforms, and alpha modes. They replace the portable PBR scene shader only when selected. Pipeline keys: `scene`, `post`, `shadow`, `particles`, `particles_instanced`. Unspecified pipelines use GLSL. Both paths are required, resolved safely within the project through the graphics path group. Optional entry names default to `vertex_main` / `fragment_main`; both files may point to one compilation unit containing both functions.

Use `[[attribute(n)]]` matching the draw layout. Scene fields are position 0, UV 1, normal 2, integer bone indices 3, weights 4, and vertex color 5. Particle layouts are documented by the corresponding GLSL files. Vertex/fragment varyings must match, as in the example's `Pixel` structure.

Uniform buffers use slots 0..14; vertex fetch reserves 15..30. Flat buffer members expose their names through Forge's uniform API. Members may be float/int/uint/bool vectors, float4x4, or one-dimensional arrays of these; nested structs/pointer resources are outside the API. Reflection supports these layouts; the current shared setters upload float vectors/matrices and scalar integer/bool values or indexed scalar arrays. Integer/bool-vector setters need an API extension. Buffer size is limited to 64 KiB. Texture/sampler slots use 0..15. Matching names such as `u_texture` / `u_texture_sampler`, or matching texture/sampler indices, associate sampling with Forge's texture-unit uniforms. Matrices use normal Metal column-major storage and `matrix * vector`.

Forge retains GL-oriented offscreen rows/UVs and converts the final presentation image. Native vertex stages receiving a Forge GL projection must convert the final clip position:

```metal
float4 forge_clip_position(float4 value) {
    value.y = -value.y;
    value.z = (value.z + value.w) * 0.5;
    return value;
}
```

The native example includes this helper. Game coordinates, camera targets, screenshot orientation, transparency ordering, and HiDPI rules keep their existing meaning. Metal standard-library includes work; project include-file preprocessing is not provided. Extend the loader/compiler with a project-safe include policy if needed.

## Resources, Extension Boundary, and Limits

`metal.hpp` / `metal.mm` implement the independent device, resources, command encoding, reflection, readback, and editor renderer. `graphics_device` owns selection/presentation; `graphics_settings` validates configuration; `shader_compiler` owns GLSL translation. The shader/compiler/device modules can be changed or replaced without changing game rules or the Core list. No new closed engine component, mandatory editor, network service, or attribution enforcement is introduced.

Metal snapshots uniforms and mutable vertex uploads so later CPU changes cannot alter previously encoded draws. Frame, shader-link, and texture-allocation autorelease scopes bound transient system objects, including reload and exceptions. Its uniform arena defaults to 64 MiB; `renderer.metal.uniform_budget_bytes` accepts integers from 65536 to 536870912. Exhaustion is reported; this is an additional transient-upload budget, separate from the renderer's asset budget. The arena retains reusable chunks. Its configured budget covers all current, pending and idle chunks together; snapshots are reused only after their owning command completes. Pending work can cause budget backpressure without increasing the cap. Internal presentation color/depth/composite targets cost 12 bytes per pixel, reported separately; drawable/driver allocations and CPU mirrors are not asset-budget measurements or a process-wide memory cap. Texture staging and retained retired vertex/texture versions are not counted as live asset bytes; limiting pending commands does not impose a total process-memory cap.

Since 2.9.2, presentation submits work asynchronously, bounded to three command buffers. Completion waits occur when capacity/budget pressure requires reuse, and explicit drains remain for readback, resize and shutdown. Texture allocation creates a new version, while subupdates use retained staging buffers and ordered GPU copies rather than mutating a texture from the CPU during active draws. Vertex uploads remain versioned and uniforms remain isolated per encoded draw. GPU completion errors surface when completed submissions are collected or drained; they may appear on a later frame. Shutdown explicitly drains before returning the runtime result, so the final submission is checked. Drawable/vsync and driver scheduling can still block; these changes do not guarantee stutter-free play or a performance advantage over OpenGL.

Draw pipelines use shader-specific effective vertex descriptors (formats, offsets, strides and instance/constant stepping), blending and depth-attachment presence. Buffer/VAO identities and mesh revision numbers are excluded, so replacing equivalent procedural geometry reuses its pipeline. Deleting a VAO leaves reusable variants cached; deleting/relinking its shader program releases them. Each program retains up to 256 variants with least-recently-used eviction. Encoded commands retain their pipeline/resources even if the corresponding cache entry is removed. Custom GLSL/MSL and graphics extensions remain supported; a real layout/render-state change can still require pipeline compilation. A drawable unavailable while a window is hidden/occluded does not destroy the world. GPU errors reach existing runtime diagnostics; automatic device recreation is not implemented. One active graphics/Python runtime per process remains supported.

## Verification

Eleven base CTest suites plus the optional shader-compiler suite include HLSL/MSL translation, varying-order regression, logical-array shapes, and configuration/path validation. Real Metal suites verify pixels and runtime compilation:

```sh
export FORGE_TEST_BACKEND=metal
export FORGE_TEST_SILENT_AUDIO=1
python tests/graphics.py build/bin/forge
python tests/features_graphics.py build/bin/forge
python tests/simulation_graphics.py build/bin/forge
python tests/rendering_graphics.py build/bin/forge
MTL_DEBUG_LAYER=1 build/metal_backend_tests
MTL_DEBUG_LAYER=1 python tests/metal_graphics.py build/bin/forge
python tools/verify_package.py dist/MyGame --graphics-backend metal
```

The four shared suites cover UI/text/localization, cameras/postprocess/shadows, PBR maps/hierarchy, skinning/morph/retargeting, particles, alternate shells/editor pixels, and rejected reload. Metal-specific tests cover native MSL edits/uniforms/clipping/orientation, rollback/recovery, auto selection, scalar arrays, reserved identifiers, explicit uniform-budget exhaustion, equivalent procedural replacements and asynchronous glyph updates. The native `metal_backend_tests` probe checks effective layout variants, encoded buffer/texture versions, pending uniform snapshots under 64 KiB and explicit final completion. `--silent-audio` retains graphics but does not verify speaker playback. `MTL_DEBUG_LAYER=1` enables Apple's validation layer when available.

Headless validate checks file paths/configuration/Python, without proving GPU compilation or pixels. Physical Apple Silicon checks do not certify Intel GPUs, Windows Direct3D, every macOS release, or relative performance. CI runs Metal GPU checks when metal_device_probe finds a device; unavailable GPU jobs emit an explicit warning/skip and do not count as pixel validation. Use a physical/GPU-enabled runner for those checks. CI describes configured checks; it is not proof of an unexecuted result.

References: [Apple Metal](https://developer.apple.com/metal/), [pipeline reflection](https://developer.apple.com/documentation/metal/mtlrenderpipelinereflection), [CAMetalLayer](https://developer.apple.com/documentation/quartzcore/cametallayer), [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross).
