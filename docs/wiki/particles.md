# Particles

[Forge wiki](../../GUIDE.md) · **Particles**

Native bounded particle pools support emission, lifetime curves, billboards, following, and texture batching. The module and shaders are replaceable.

- [Emitters and simulation](#emitters-and-simulation)
- [Instancing, textures, and custom shaders](#instancing-textures-and-custom-shaders)

## Emitters and simulation

Define emitters in scene JSON/build() or create an effect from Python:

```python
from particles import Emitter

sparks = Emitter(texture='particle.png', rate=0, max_particles=300,
                 velocity=[0, 3, 0], velocity_random=[3, 2, 3],
                 gravity=[0, -5, 0], lifetime=[.5, 1.5], size=[.15, 0],
                 color_start=[1, .6, .1, 1], color_end=[1, .1, 0, 0],
                 blend='additive', seed=18)
sparks.move((0, 2, 0))
born = sparks.burst(100)
# sparks.stop() stops emission; existing particles finish their lives.
# sparks.start() resumes emission.
# sparks.close() immediately removes the emitter and its particles.
```

Particles occupy native pools rather than Entity/Behavior instances. Emitter needs no per-frame Python update. rate controls continuous emission, burst the initial burst; .burst(count) works even when continuous emission is disabled. It returns the actual spawn count; excess particles are dropped and counted in dropped.

| Field | Meaning / Default |
| --- | --- |
| position | Emitter position [0,0,0] |
| velocity, velocity_random | Initial velocity [0,1,0], symmetric spread [0,0,0] |
| gravity, drag | Acceleration [0,0,0], exponential velocity damping 0 |
| shape | point; also box (extent is half-size) and sphere (radius) |
| lifetime | Random seconds range [1,1], minimum .001, maximum 3600 |
| size | Size **at birth and death** [.1,.1], linearly interpolated |
| size_random | Random size multiplier [1,1] |
| color_start, color_end | RGBA [1,1,1,1] → [1,1,1,0], linearly interpolated |
| rotation, angular_speed | Initial angle range [0,0] and speed 0; degrees |
| rate, burst | 30 particles/second, 0 initial particles |
| duration, loop | 0 means unlimited emission; finite duration/loop=false stops emission; loop=true repeats the continuous-emission clock |
| max_particles, seed | 1000; seed=1 gives repeatable random initial parameters |
| texture, uv | '' means white square; file relative to textures; UV [x,y,w,h]=[0,0,1,1] |
| blend | alpha or additive |
| screen, layer | false; layer=1, participates in camera masks |
| follow, space | '' means no owner; otherwise Entity ID; world or local |
| enabled, name | true; optional name '' |

space:'world' leaves emitted particles in world space; local moves them with the emitter/owner. Following inherits position only, not rotation/scale. Owner destruction stops automatic emission; existing particles retain the last owner position. Local gravity/velocity still use world axes. 3D particles are camera-facing billboards; 2D particles lie in XY. screen:true uses screen pixels and pixel size, drawn above UI. The world particle pass follows geometry and precedes UI; alpha particles sort within that pass and do not write depth. They are not jointly sorted with transparent entities. Render-target cameras draw world particles by layer mask and screen particles when include_ui is enabled.

A scene permits 256 emitters and **100000 total reserved** particles. max_particles is 1..100000 per emitter; rate/burst are at most 100000. Limits apply even to empty pools; lower defaults for many effects. Color/UV: 0..1; size: 0..10000; size_random: 0..10; velocity_random/extent: ≤10000; absolute position/velocity/gravity: ≤1e6; drag: 0..100; duration: ≤86400; radius: ≤10000. Seed/layer are uint32. Invalid parameters fail without adding an emitter.

Emitter.info includes settings/alive/enabled/dropped/elapsed. snapshot(limit=32) returns live-particle diagnostics, not save configuration. close() is idempotent; stop(clear=True) also removes the emitter. Emitter.existing(id) wraps a JSON emitter; particle_emitters() lists IDs. Native APIs: particle_emitter(dict), particle_info(id), particle_burst(id,count), particle_remove(id), particle_enabled(id,bool), particle_position(id,vector), particle_snapshot(id,limit), particle_stats(), and particle_step(dt).

Gameplay pause stops automatic updates; disabling physics does not. particle_step(0..1) allows manual control; do not combine it with automatic stepping unless double advancement is intended. Emitters belong to scenes: transitions release them, old IDs expire, and failed reload restores previous pools/bodies. scene_data() stores emitter definitions rather than age, RNG state, or live particles; a restored scene restarts the effect.

graphics/particle.vert and particle.frag are editable. renderer.particle_vertex_shader/particle_fragment_shader resolve relative to graphics. Older projects without default files use embedded shaders; explicitly configured missing files are errors. Candidate GLSL failure preserves old programs/pools. Profiles contain particles_ms/pool stats; renderer diagnostics include particle_quads and particle_draw_calls across cameras. GPU buffers count toward the GPU budget. Particles lack geometry collisions, lighting/shadows, and GPU compute; atlas UV is static, not per-frame sprite animation.

## Instancing, textures, and custom shaders

Default particles use instancing on the selected graphics backend: position, UV rectangle, color, size, and angle require 52 bytes per particle rather than six 36-byte vertices (216 bytes). GLSL expands/rotates quads. The static quad adds 48 bytes; instance buffers grow to batch capacity, count toward the GPU budget, and release with renderer resources. Budgets do not limit transient driver memory when orphaning buffers.

Renderer takes one particle-pool snapshot per frame for all cameras/UI passes. Simulation, property interpolation, filtering, and alpha sorting remain CPU-side. Already ordered passes skip sorting; additive particles group by texture without depth sorting. Existing transparency/UI order remains; no GPU compute, collisions, or particle lighting are added.

Resolved texture paths cache across frames. GPU handles cache within a frame and mark textures for LRU; eviction cannot leave active references to removed resources. Successful reload/resource replacement clears both caches. Directory renames, symlink retargets, and replacements apply through reload rather than per-particle canonicalization. Paths are checked for project containment before caching.

Disable the optimization in engine.json:

```json
"renderer": {"particle_instancing": false}
```

particle_instancing defaults to true. If particle_vertex_shader or graphics/particle.vert differs from the embedded source, the engine selects the legacy six-vertex layout automatically. particle_fragment_shader works in both modes. Change the instanced layout through renderer.particle_instance_shader (default particle-instance.vert). Retain the default legacy vertex shader when you want instancing.

Instanced attributes: location 0 quad corner vec2; 1 position vec3; 2 UV rectangle vec4; 3 color vec4; 4 size/angle vec2. Uniforms: u_view mat4, u_right/u_up vec3; fragment texture u_texture. Outputs v_uv/v_color are retained. A new shader failure rejects reload and keeps the previous game.

forge.renderer_stats() adds:

| Field | Meaning |
| --- | --- |
| particle_instancing | Whether the instanced vertex layout is selected |
| particle_upload_bytes | Total live stream bytes across particle passes in the last frame |
| particle_sort_ms | CPU filtering/depth/order time across particle passes, excluding simulation |
| texture_path_resolutions | Newly resolved logical texture names this frame; a warm cache returns 0 |

Extra cameras increase upload bytes/quads/draw calls because each needs its own pass/order. render_ms is CPU rendering wall time, including submission/driver waits, not GPU timer queries.
