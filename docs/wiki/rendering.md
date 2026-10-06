# Rendering, Cameras, and Shaders

[Forge wiki](../../GUIDE.md) · **Rendering, Cameras, and Shaders**

OpenGL is the default. Optional Metal and Direct3D 11 use the same game API and portable GLSL; native shader overrides remain editable.

- [Choose a graphics backend](#choose-a-graphics-backend)
- [Portable shaders and text](#portable-shaders-and-text)
- [Cameras, UVs, postprocessing, and uniforms](#cameras-uvs-postprocessing-and-uniforms)
- [Texture filtering and captures](#texture-filtering-and-captures)
- [LOD and visibility optimization](#lod-and-visibility-optimization)

## Choose a graphics backend

Set `renderer.backend` in engine.json:

```json
{"renderer": {"backend": "auto"}}
```

| Selection | Availability / behavior |
| --- | --- |
| `opengl` | Default; OpenGL 3.3 Core |
| `metal` | Optional macOS backend, including Apple Silicon |
| `direct3d11` | Optional Windows backend, feature level 11_0 / shader model 5 |
| `auto` | Prefers compiled Metal on macOS, compiled Direct3D on Windows, otherwise OpenGL |

Explicit unavailable backends fail with diagnostics. Auto selection follows build capabilities; it does not retry a different backend after device initialization fails. `forge.graphics_backends()` reports compiled support; `forge.renderer_stats()` reports the actual backend after rendering. Backend/driver/debug/Metal-arena-budget changes require restart and are rejected transactionally during reload.

GLSL remains the shared editable source. Native HLSL/MSL overrides are optional **complete vertex/fragment pairs** per pipeline; they do not remove the OpenGL shaders. See [Direct3D shader contracts and examples](../DIRECT3D11.md) and [Metal shader contracts and examples](../METAL.md) for stage semantics, entry points, logical uniform arrays, coordinates, budgets, and compile switches. Windows supports hardware or WARP; WARP is software rendering. Headless compilation/validation cannot prove device rendering.

## Portable shaders and text

Defaults are graphics/default.vert and default.frag. Removing vertex_shader/fragment_shader selects embedded equivalents. Custom shaders use GLSL 330 Core and this interface:

| Attribute location | Type / meaning |
| --- | --- |
| 0 | `vec3` position |
| 1 | `vec2` UV |
| 2 | `vec3` normal |
| 3 | `ivec4` bone indices |
| 4 | `vec4` bone weights |
| 5 | `vec4` vertex color |

The base uniforms include `mat4 u_mvp, u_model`, `vec4 u_color`, `sampler2D u_texture`, `int u_textured`, and `float u_lit` (0 for 2D/UI, 1 for 3D). Skinning uses `u_bones[128]` and `u_skinned`; `u_uv_rect` applies the entity UV region and `u_flip_y` handles render-target orientation. The editable [default vertex shader](../../graphics/default.vert) and [fragment shader](../../graphics/default.frag) show the complete scene/PBR/lighting contract. [Particles](particles.md#instancing-textures-and-custom-shaders) use a separate vertex layout.

Unused uniforms removed by the compiler are allowed. The default shader handles sprites/meshes/text; specialized particle/post passes have their own contracts. Shader errors include the filename and backend/compiler diagnostics. Avoid zero scale on rendered objects: the standard shader uses inverse(u_model).

Text decodes from UTF-8; stb_truetype rasterizes requested glyphs and caches them. Noto Sans covers Latin and Cyrillic; other coverage depends on fonts. Ligatures, bidi/RTL shaping, and color emoji are unsupported. Text position is its top-left; \n inserts a line break. ui.Label handles wrapping/alignment. Rasterization accounts for size, scale, and HiDPI. entity.clip=(left, top, width, height) restricts a screen entity to a logical rectangle; None removes clipping.

## Cameras, UVs, postprocessing, and uniforms

```python
monitor = forge.set_render_target('door', {
    'width': 640, 'height': 360, 'mode': '3d',
    'position': [5, 3, 5], 'target': [0, 1, 0], 'fov': 60,
    'include_ui': False, 'layers': 1,
})
screen = forge.spawn({'texture': monitor, 'screen': True,
             'position': [900, 180, 0], 'scale': [480, 270, 1]})
# When finished, remove references before deleting the target:
screen.destroy()
forge.remove_render_target('door')
```

Each camera has color/depth framebuffers and renders the world every frame. Dimensions are integers 1..4096, fov is 1..178 degrees, and mode is 2d or 3d. 2D uses its texture's coordinates with position as an offset. Entity.layer is a 32-bit mask; visibility requires intersection with the camera mask. Cameras render in name order; a camera's own texture is excluded from its pass, while others can show the previous frame. Cyclic camera graphs are not evaluated recursively. @target:name references are textures, never filesystem paths.

entity.uv=(u,v,width,height) selects a normalized 0..1 region from the top-left without exceeding the texture bounds. It works for sprites, meshes, and screen entities. Batching merges adjacent compatible sprites/glyphs while preserving order; texture, color, clipping, depth, camera, and uniforms define batch boundaries. Glyphs occupy GPU atlases and rasterize for their actual size/HiDPI up to 1024 pixels.

```python
import forge

forge.set_postprocess({'grain': .12, 'bloom': .2, 'aberration': 2,
                      'scanlines': .3, 'vignette': .25, 'fade': 0, 'gamma': 1})
forge.set_shader_uniform('custom_tint', [.2, .8, .5, 1.])
actor.uniforms = {'custom_amount': .4}
```

Postprocess is a separate fullscreen pass using UV/u_texture, u_time, and u_resolution; replace its shader through renderer.post_shader. Grain, scanlines, vignette, and fade: 0..1; bloom: 0..4; aberration: 0..32 pixels; gamma: .1..8. enabled=False disables it. Bloom is a threshold filter of neighboring bright pixels, not an HDR pyramid. Shader uniforms support bool, signed int32, float, and 1..4 float vectors; types must match GLSL (1.0 for float, 1 for int). Global uniforms are in `rendering.uniforms`, entity uniforms in `Entity.uniforms`, and post uniforms in `rendering.postprocess.uniforms`. u_* names are engine-owned; choose different names for custom parameters.

Uniform overrides are scoped to their draw. Removing an entity override restores the global value when present, otherwise zero; removed postprocess overrides also reset to zero. Set explicit global defaults for custom shader parameters that need nonzero values. Previous objects/frames must not supply implicit defaults. The same behavior applies to OpenGL, Metal, and Direct3D.

renderer.fallback_fonts in engine.json lists additional TTF files from graphics. Glyph lookup tries the primary font then fallbacks; absent coverage logs Missing glyph U+... once to terminal/file. Wrapping and measurement use the same font selection. Complex shaping, bidi/RTL, and color emoji require another text backend. Include each added font's license in the distribution.

## Texture filtering and captures

```json
{"renderer": {"texture_filter": "nearest", "mipmaps": true}}
```

texture_filter is nearest or linear (default); mipmaps is bool (default false). Options cover file/imported/embedded textures; font atlases/render targets filter separately. GPU-generated mipmap chains count fully toward GPU budgets. nearest selects nearest mip levels; linear is trilinear. Anisotropic filtering and per-texture samplers are absent.

forge.user_screenshot('review.ppm') writes P6 PPM under forge.user_path('captures',...), outside the game bundle even in project storage. forge.screenshot writes inside the project for tests/development. Capture occurs on the next render, is unavailable headless, and remains inside the selected root.

## LOD and visibility optimization

Enable optional native rendering optimization in `rendering.optimization` or call `forge.set_render_optimization({'enabled': True, 'occlusion': True})`. Defaults preserve existing projects. Each entity's JSON/Python `optimization` supports static model/texture `levels` at increasing distances, hysteresis, a maximum drawing distance, conservative local bounds, and explicit solid-box occluders. Culling affects draws only, leaving scripts, physics, audio, and animation clocks active. Screen UI/text remain outside these tests.

```python
forge.set_render_optimization({'enabled': True})
prop.optimization = {'levels': [{'distance': 40, 'model': 'low-detail.obj'}],
                     'hysteresis': .1, 'max_distance': 200}
# Opt an entity out, or disable the whole system:
prop.optimization = {'culling': False, 'lod': False}
```

Static mesh bounds enclose all model levels and inherit parent transforms. Skeletal/morph bounds require an explicit envelope for every pose; model LOD does not replace a skeleton. Occlusion needs authored boxes wholly inside opaque solid geometry for every LOD. Custom shader displacement needs enclosing bounds; cutouts must never be covered by a solid proxy. The conservative CPU grid runs independently per color camera, with no GPU readback. Distance/occlusion do not remove shadow casters. These systems provide no automatic mesh decimation/GPU Hi-Z/gameplay streaming.

Run `python tools/forge.py dev --scene optimization.py` for a visible example. Inspect `forge.renderer_stats()['optimization']` for per-pass culling/LOD/cache/CPU counters. `RenderOptimizationPolicy` and `Renderer::setOptimizationPolicy` are public C++ contracts; `modules/render_optimization_example.cpp` demonstrates replacement/composition. All shader/backend and editor-shell customization remains available. [Complete settings, contracts, schema, examples, and tests](../RENDER_OPTIMIZATION.md).
