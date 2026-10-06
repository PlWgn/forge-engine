# Native rendering optimization — Forge 2.8

Forge provides optional distance LOD, frustum culling, distance culling, and conservative software occlusion. They share one backend-independent policy and work with OpenGL, Metal, and Direct3D 11. Existing projects keep their rendering behavior: optimization is disabled by default. Physics, scripts, animation clocks, events, audio, and entity visibility properties continue running when a draw is culled.

## Enable it

Put this in project `engine.json`, a scene's `rendering` object, or the result of `build()`:

```json
"rendering": {
  "optimization": { "enabled": true, "occlusion": true }
}
```

Or use Python:

```python
forge.set_render_optimization({'enabled': True, 'occlusion': True})
```

This replaces the optimization settings, with defaults for omitted fields. It does not change lights, cameras, shaders, materials, batching, or postprocessing. JSON/Python setters reject invalid values before mutation. Unknown settings and entity fields survive scene snapshots and editor document operations. Project, shell, and Python API versions remain 1.

Run the built-in example:

```sh
python tools/forge.py dev --scene optimization.py
```

Use arrows to move, O to toggle occlusion, and L to toggle the system. Draw and culling counters appear on screen. The low-detail mesh is intentionally a different shape/color so the selected representation is visible.

## LOD and maximum distance

The original entity model/texture is level 0. Supply ordinary lower-detail assets at increasing camera distances:

```python
actor = forge.spawn({
    'kind': 'mesh', 'model': 'tree-high.obj',
    'optimization': {
        'levels': [
            {'distance': 25, 'model': 'tree-medium.obj'},
            {'distance': 70, 'model': 'tree-low.obj', 'texture': 'tree-small.png'},
        ],
        'hysteresis': .1,
        'max_distance': 180,
    },
})
```

Models can also be scene-owned procedural `@mesh:` names. A level can override a model, a texture, or both. A missing override uses the original entity value; levels are complete choices, not cumulative patches. Model overrides require `kind: mesh`; texture LOD also works on cubes and sprites. LODs use the entity's existing transform, color, UVs, uniforms, material override, layer, and shadow setting. Imported alternative materials remain editable; their transparency is classified before draw ordering. No vertex layouts or shader stages are forced. Keep transparency/shadow semantics consistent across representations: an originally blended object remains excluded from shadow passes even when an opaque LOD is selected.

Distances use world units in 3D and camera-relative XY pixels in 2D, measured from the entity's world origin. Screen UI and text do not participate. Each camera has independent hysteresis: the first frame selects at the exact threshold; subsequent outward transitions occur at `distance * (1 + hysteresis)`, inward transitions below `distance * (1 - hysteresis)`. Default hysteresis is 10%; 0 removes it. Shadow LOD uses the main camera's distance but its own state. Distance culling is disabled in shadow passes so an off-camera object can still cast a shadow into the view.

LOD changes draw representation only. It does not replace the entity, change `entity.model`/`texture`, alter collision shapes, restart scripts, or freeze animation. Mesh LOD is currently for static models. A skeletal, animated, or morph model keeps its original model; texture-only levels remain usable. Supply externally authored low-detail assets: Forge does not automatically decimate meshes, retarget a skeleton to a LOD, cross-fade geometry, stream whole scenes, or infer gameplay regions.

## Bounds and opt-outs

Cube/sprite bounds are automatic. Static imported/procedural mesh bounds are cached from vertices and model-node transforms, then combined across all model LODs. Local bounds are transformed with the full parent hierarchy, including rotation and negative scale. Candidate testing uses a conservative world AABB; it can retain extra geometry but must not remove geometry inside the declared bounds. Updated procedural meshes and committed resource reloads invalidate derived bounds.

Animated/morph meshes stay visible to frustum/occlusion tests unless you supply a conservative local-space bound enclosing every pose:

```python
actor.optimization = {
    'bounds': {'min': [-2,-1,-2], 'max': [2,5,2]},
    'max_distance': 200,
}
```

An explicit bound must enclose all geometry, every LOD, and any vertex-shader displacement. Keep optimization off or opt the entity out when that cannot be guaranteed. Shader source remains fully editable, including native MSL/HLSL overrides. Custom shader displacement/discard is not inferred from shader code.

```python
actor.optimization = {'culling': False, 'lod': False}
forge.set_render_optimization({'enabled': False})
```

`culling: false` disables native frustum, distance, and occlusion rejection for that entity; `lod: false` preserves its original representation. Screen-space UI, text, and particles use their existing rendering paths. Culling changes no physics or lifecycle contracts.

## Occlusion proxies

Occlusion is opt-in and needs explicit solid boxes. Add an `occluder` to an opaque object:

```python
wall = forge.spawn({
    'kind': 'cube', 'scale': [8,4,.5],
    'material_properties': {'alpha_mode': 'opaque'},
    'optimization': {
        'occluder': {'min': [-.5,-.5,-.5], 'max': [.5,.5,.5]},
    },
})
```

The box is local to the entity and inherits its full world transform. It must lie entirely inside the object's actual solid geometry, for every LOD. Use multiple actual wall objects around a doorway rather than one box covering its opening. A mesh's bounding box is **not** automatically a solid occluder. Transparent/cutout/deforming objects cannot provide native proxies; a shader that discards pixels needs a proxy restricted to guaranteed solid pixels, or none. A displaced shader also needs matching bounds/proxy declarations.

The native system projects the convex box silhouette into a bounded grid. A tile is covered only when all four tile corners are strictly inside the silhouette. Stored depth is the proxy's farthest depth. An object is rejected only when every tile touched by its projected bound is covered strictly in front of its nearest depth. Near-plane intersections, uncertain projections, partial coverage, and viewport-edge candidates remain visible. Depth tolerance protects touching surfaces. The grid is rebuilt for each color camera, so moving/disappearing proxies have no previous-frame visibility lag. Occlusion does not run for 2D or shadow passes.

This is conservative CPU occlusion, not GPU Hi-Z, hardware queries, or arbitrary-mesh rasterization. It avoids GPU readback stalls and works identically across backend APIs. Large/rotated bounds or tiny/thin proxies can reduce effectiveness. Higher grid resolution improves coverage at greater CPU cost. Measure counters and frame time in your scene; there is no universal FPS guarantee.

## Settings and diagnostics

| System setting | Default | Contract |
| --- | --- | --- |
| `enabled` | false | Master enable; required for the built-in or replacement policy |
| `frustum` | true | Test conservative bounds against this pass's clip volume |
| `distance` | true | Apply a positive per-entity `max_distance`; 0 means unlimited |
| `lod` | true | Select authored model/texture levels |
| `occlusion` | false | Explicit proxy-based software occlusion for 3D color passes |
| `grid_width`, `grid_height` | 64, 36 | Integers 1..256; grid memory is bounded |
| `max_occluders` | 128 | Test at most this many eligible proxies per pass, 1..1024 |

Entity options are `culling`/`lod` (both true), `max_distance` (0..1e30), `hysteresis` (0..0.49), optional `bounds`/`occluder` min/max triples, and `levels` (at most 16, positive strictly increasing distances up to 1e30). Bounds must be finite float-range triples with min <= max; proxy volume must be positive. Asset paths use normal project validation and configured resource groups. Invalid setters preserve the old settings.

```python
stats = forge.renderer_stats()
optimization = stats.get('optimization', {})
main = optimization.get('passes', {}).get('main', {})
forge.log(str(main))
```

Each pass (`main`, `shadow`, `camera:<name>`) reports `tested`, `visible`, `frustum_culled`, `distance_culled`, `occlusion_culled`, `lod_selected`, `occluders`, `proxy_tests`, and `grid_cells`. UI/text/particles are excluded from these entity counts. The `optimization` object also reports `enabled`, `cpu_ms` (candidate preparation, policy, and visibility selection), `bounds_builds`, and `bounds_cache_entries`. Compare overall `draw_calls`, `triangles`, and `render_ms` with optimization disabled. Cache counters should settle after the first frames. Transparency classification is computed once per candidate, rather than repeatedly resolving meshes during sorting.

Headless mode validates configuration/scene contracts but does not render or produce GPU visibility counters. Build/validate check project assets; graphical candidate validation checks LOD resources before committing. Runtime drawing uploads selected model LODs on demand, under existing resource budgets.

## Replace the native system

`engine/include/forge/render_optimization.hpp`, `engine/src/render_optimization.cpp`, and `render_optimization_data.cpp` are independent adaptable modules outside the exhaustive Core list. Their public `RenderOptimizationPolicy` accepts a camera, ordered entity candidates, and validated settings; it returns an ordered decision vector plus diagnostic JSON. Each decision controls draw visibility and the authored LOD index. Implement another algorithm, compose `NativeRenderOptimizer`, or change these modules directly.

Install through the public renderer contract on the main thread:

```cpp
runtime.renderer->setOptimizationPolicy(std::make_shared<MyPolicy>());
// Restore Forge's policy:
runtime.renderer->setOptimizationPolicy(nullptr);
```

`beginFrame()` and `endFrame()` bracket all passes. `evaluate()` runs once per pass with 3D/2D/shadow identity, clip matrix, camera origin, conservative bounds, explicit proxy corners, opacity/deformation flags, and entity references. It must return exactly one decision per candidate and a diagnostic object. LOD indices must be 0..N. UI/text and normal layer/visibility/feedback filters remain outside policy input; an invalid policy plan raises a contextual runtime error before this pass submits entity draws. A deforming entity never accepts a model override. Policy code must not mutate the world or invoke rendering during evaluation.

`modules/render_optimization_example.cpp` demonstrates composition and a custom `Entity.data['example_hide']` flag. Add it to `native_modules`, compile, then call `forge.use_example_render_policy()`; pass `False` to restore the native policy. It uses public interfaces without editing the Core or renderer internals. Native changes require compilation; ordinary settings, LODs, bounds, shaders, and scenes remain file/Python-driven. Alternatively disable the built-in system and implement Python visibility/LOD logic with existing public entity setters; that path also serves custom editor shells.

Renderer policy replacement is application-scoped; failed scene preparation restores its previous pointer. Arbitrary internal state in a user policy cannot be rolled back by Forge. Keep owned resources/caches bounded and release scene references. Forge's policy holds weak entity references for hysteresis and retires unused camera states each frame.

## Verification

`ctest --test-dir build -C Release --output-on-failure` includes the native `render_optimization` suite. It checks frustum/near-plane/reflected bounds, distance/shadow rules, multi-level hysteresis, independent cameras, deformation fallback, conservative tile coverage, self/edge visibility, transparent proxies, removed proxies, and disable behavior. `tests/rendering.py` checks Python/config validation, independent JSON copies, and serialization. `tests/rendering_graphics.py` includes actual pixel/counter checks for occlusion, camera LODs, and parented 2D texture LODs and runs in existing OpenGL/Metal/Direct3D CI jobs.

`python tests/render_policy_extension.py <binary-with-example-module>` verifies public policy installation, custom rejection, and restoration with actual frames. It requires the optional example compiled into that binary; it is not part of the default suite.

Physical OpenGL/Metal validation on one Mac does not prove Windows Direct3D or every driver. Hardware results belong in release notes; source coverage alone does not establish a test run.
