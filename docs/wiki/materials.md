# Materials and Lighting

[Forge wiki](../../GUIDE.md) · **Materials and Lighting**

Use legacy shading for compatibility, unlit for independent color, or PBR for metallic/roughness materials. Material definitions and graphics implementations are adaptable.

- [PBR material reference](#pbr-material-reference)
- [Transparency and depth](#transparency-and-depth)
- [Lights and imported models](#lights-and-imported-models)

## PBR material reference

A material is JSON in paths.materials or entity inline material_properties. material names the file; inline properties replace its rendering property set. Legacy color/texture remain supported; color retains finite values outside 0..1, while PBR base_color has separate range checks. Entity.material_properties returns an independent dict; reassign it to apply edits.

```json
{
  "shading": "pbr",
  "base_color": [1, 1, 1, 1],
  "metallic": 0.7,
  "roughness": 0.35,
  "normal_scale": 1,
  "occlusion_strength": 1,
  "emissive": [0, 0, 0],
  "alpha_mode": "opaque",
  "alpha_cutoff": 0.5,
  "albedo_texture": "surface/base.png",
  "normal_texture": "surface/normal.png",
  "metallic_roughness_texture": "surface/orm.png",
  "occlusion_texture": "surface/orm.png",
  "emissive_texture": "surface/emissive.png"
}
```

All maps resolve relative to paths.textures, checking root containment and existence. An empty name means no map. validate/build, spawn, and property assignment use the same material checks; failed assignment preserves the old material. Decode/GPU upload occurs during scene preparation/rendering; failed reload does not replace the working renderer.

| Field | Meaning / Default |
| --- | --- |
| shading | legacy by default; also pbr, unlit |
| base_color | RGBA factor 0..1, [1,1,1,1]; multiplied by entity/vertex/model-part color |
| `metallic` | 0..1, default 0 |
| roughness | 0..1, default 1; shader clamps the result to at least 0.045 |
| normal_scale | 0..10, default 1; scales normal-map XY |
| occlusion_strength | 0..1, default 1; affects ambient |
| emissive | Linear RGB 0..1000, default [0,0,0]; map multiplies the factor |
| alpha_mode | opaque, mask, blend; default blend for legacy, opaque for pbr/unlit |
| alpha_cutoff | 0..1, default 0.5; used by mask |
| texture, albedo_texture | sRGB color map; explicit Entity.texture takes precedence |
| normal_texture | Linear RGB tangent-space normal; TBN computed from UV derivatives |
| metallic_roughness_texture | Linear map: G roughness, B metallic |
| metallic_texture, roughness_texture | Additional linear R channels; multiply factors/packed MR when combined |
| occlusion_texture | Linear R; an ORM map can serve both MR and AO |
| `emissive_texture` | sRGB RGB |

PBR factors/vertex colors are linear; albedo/emissive textures decode from sRGB and output is encoded for display. The shader uses GGX, Smith approximation, and Schlick Fresnel. Directional/point/spot lights, shadows, and camera position affect PBR, including render targets. Empty lights retain built-in lighting. shading:unlit displays color independently of lighting, useful for procedural data.

Assimp imports major metallic/roughness factors, external/embedded base/normal/MR/AO/emissive maps, and the first vertex-color set. Shared metallic/roughness maps are recognized as glTF packed MR. validate decodes all external model maps. Explicit entity material maps take precedence; unset channels may use model-part maps.

This is direct-light PBR with approximate ambient: no IBL/environment reflections, HDR pipeline, transmission, parallax, glTF material extensions, multiple sampler/UV sets, or texture transforms. It uses the first UV set and shared filtering. double_sided accepts bool, but culling is currently off for all materials. Public channels are fixed; arbitrary vertex layouts require changing the available graphics module. Custom GLSL must support its required uniforms/attributes; old shaders remain valid but only compatible shaders display PBR maps.

## Transparency and depth

In 3D, opaque writes depth, mask discards fragments below cutoff and supports cutout shadows, and blend draws after opaque geometry in far-to-near object-center order with depth testing but no depth writes. Screen/UI retains a separate order; 2D keeps its existing Z order.

Center sorting is not OIT; intersecting surfaces and large models with mixed alpha categories may need splitting. If any model part blends, the entire model enters the transparent pass. Blended entities do not cast shadows. Particles have a separately sorted pass, not joint ordering with transparent meshes. opaque forces alpha=1; use legacy/blend or explicit mask for older transparent textures.

## Lights and imported models

```python
forge.set_lights([
    {'type':'directional', 'direction':[-.5,-1,-.3], 'color':[1,.9,.8],
     'intensity':1, 'shadows':True, 'shadow_extent':20},
    {'type':'point', 'position':[3,2,0], 'color':[1,.2,.1], 'range':10, 'intensity':2},
    {'type':'spot', 'position':[0,4,0], 'direction':[0,-1,0], 'range':20, 'cone':30}
])
info = forge.model_info('actor.glb')
actor = forge.spawn({'kind':'mesh', 'model':'actor.glb'})
forge.play_animation(actor, info['animations'][0]['name'], speed=1, loop=True)
forge.pause_animation(actor)
actor.animation_time = .5
```

Assimp imports OBJ, glTF 2/GLB, FBX, and COLLADA/DAE: node hierarchies, base color, diffuse/base-color maps, embedded textures, skin weights, and TRS clips. Unknown clips raise. model_info returns parts, vertex/bone counts, clip durations, external textures, and size. animation_pose(file,clip,seconds,loop) provides global node matrices for tools; Entity.animation/animation_playing are readable. Limits are 128 bones per mesh, four weights per vertex, and 2M source vertices. GPU vertex shaders perform skinning. Imported TRS keys interpolate without complete glTF cubic-spline tangent reproduction. Layered blending, retargeting, authored tracks, and morph targets are described in [animation and authoring](animation.md); full blend-tree graphs are absent.

All external model dependencies must stay within the project. FBX/material support depends on the importer; errors include the path and Assimp message. Imported PBR/normal/metallic-roughness/AO/emissive maps follow the material contract above. Blending and rest-pose retargeting follow the boundaries in [animation](animation.md); adapt the model module for other requirements.

Up to 16 point/directional/spot lights are supported; rendering.ambient sets ambient RGB. One PCF shadow map is available for the first directional light with shadows=True. rendering.shadow_size is 64..4096; shadow_extent controls orthographic coverage around the camera target. Entity.casts_shadow=False excludes an object from the depth pass. Opaque/masked geometry casts shadows; text/screen UI and blended objects are excluded. Masked cutout shadows are supported; multiple maps and cascades are absent. Empty lights retain the basic lighting default.
