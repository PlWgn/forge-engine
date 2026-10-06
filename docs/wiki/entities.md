# Entities, Hierarchies, and Prefabs

[Forge wiki](../../GUIDE.md) · **Entities, Hierarchies, and Prefabs**

Entities are scene-owned. Prefabs reuse declarations; parent relationships share transforms and lifetime.

- [Entity fields and templates](#entity-fields-and-templates)
- [Parents and coordinate spaces](#parents-and-coordinate-spaces)
- [Bulk transform updates](#bulk-transform-updates)
- [Prefab instances and inheritance](#prefab-instances-and-inheritance)

## Entity fields and templates

Supported `kind` values are `sprite`, `cube`, `mesh`, `text`, and `empty`. `empty` is useful for triggers and logical objects.

| Field | Meaning |
| --- | --- |
| id, name | Identifier and display name |
| prefab | JSON template from paths.objects |
| position, rotation, scale | Three-component numeric vectors |
| color | Four-component RGBA, usually 0..1 |
| texture | File from paths.textures |
| model | Model from paths.models; required for mesh |
| material | JSON from paths.materials |
| text, font_size | UTF-8 text and font height in pixels |
| text_key, text_params | Localization key and JSON translation parameters; see [localization](localization.md) |
| visible | Rendering visibility; false does not disable scripts or physics |
| screen | UI above the world without the camera or depth test |
| collider | Full dimensions independent of scale; AABB in legacy, selected shape in Bullet |
| dynamic, mass, velocity | Physics integration, positive mass, and velocity |
| angular_velocity, rigid_body | Angular velocity and optional Bullet settings; [physics and character controllers](physics.md) |
| trigger | Overlap events without physical resolution |
| scripts | List of filenames or file/properties objects |
| data | User-defined JSON data |
| parent | Same-scene parent ID; transforms are local to it |
| uv, layer, clip | Texture region, camera mask, screen clipping |
| casts_shadow | Include in supported shadow pass |
| material_properties | Inline rendering properties |
| animator, morph_weights | Declarative skeletal layers and morph overrides |
| optimization | Optional LOD, bounds, distance, and occluder settings |

JSON objects, Python build() results, and forge.spawn use the same validator after merging a prefab. Position, rotation/scale/velocity/collider, RGBA, and clip require numeric components that are finite and representable as C++ float. Boolean/null components, NaN/Infinity, and values such as 1e100 are rejected. Mass and font size must remain positive after float conversion. Reciprocal mass must also fit float: mass=1e-40 is rejected in JSON, forge.spawn, and Entity.mass assignment. clip=null disables clipping. Numeric Entity setters, move, and impulse validate before mutation and preserve the previous value on failure.

Template `objects/crate.json`:

```json
{"kind": "cube", "model": "", "scale": [1, 1, 1], "material": "wood.json", "collider": [1, 1, 1]}
```

Material `materials/wood.json`:

```json
{"color": [1, 1, 1, 1], "texture": "wood.png"}
```

A material sets color and texture; explicit entity fields take precedence. Template and local fields use JSON Merge Patch: local values replace base values, arrays are replaced entirely, and null removes a field. [Prefab instances](#prefab-instances-and-inheritance) support inheritance and reusable hierarchies.

Textures support PNG/JPEG/BMP/TGA and other stb_image formats; V points downward. Model import uses Assimp and supports the formats in [lights and imported models](materials.md). OBJ may contain positions, UVs, normals, and negative indices; polygon faces are triangulated. Triangulate complex concave polygons when exporting. Imported material maps and explicit JSON materials are described in [lights and imported models](materials.md) and [pbr material reference](materials.md).

Materials are read when an entity is created. Change color or texture at runtime through Entity properties. Editing a material JSON in dev recreates the scene.

## Parents and coordinate spaces

```json
{
  "entities": [
    {"id": "root", "kind": "empty", "position": [5,0,0], "rotation": [0,45,0]},
    {"id": "child", "kind": "cube", "parent": "root", "position": [2,0,0]}
  ]
}
```

parent is an ID in the same scene; empty/absent means a root. JSON/build() may declare parents after children; runtime spawn requires an existing parent. Cycles, missing parents, mixed screen/world spaces, and matrix overflow are rejected before committing the graph.

position/rotation/scale and aliases local_position/local_rotation/local_scale define local TRS; a root's position is also world position. Local matrix order is T × Rx × Ry × Rz × S; world matrix is parent.world_matrix × local. Screen entities may have parents if the complete link remains in screen space.

```python
root = forge.find('root')
child = forge.find('child')
child.local_position = (2, 1, 0)
print(child.world_position)       # XYZ list.
print(child.world_matrix)         # Four rows of four components.
print(child.world_rotation)       # Accumulated XYZ degrees, without scale.
child.world_position = (10, 2, 0)  # Inverse parent transform -> local.
child.set_parent(root)            # Keeps local, changes world.
child.set_parent(None, keep_world=True)
for item in root.children(recursive=True):
    forge.log(item.id)
# Choose one destruction policy:
root.destroy()                   # Destroys subtree; on_destroy once.
# root.destroy(children=False)   # Children become roots preserving world TRS.
```

parent is read-only Entity/None; world_matrix/world_rotation are read-only. set_parent accepts Entity, ID, or None. children() returns immediate children by default. keep_world retains the full matrix only if representable as local TRS; shear/singular matrices fail and roll back. Ordinary inheritance can display shear. Failed destroy(children=False) keeps the entire subtree alive/attached. world_position rejects inverse conversion through a singular parent.

**Physics contract:** dynamic bodies must be roots, with visual children allowed. Static/kinematic colliders may be children. Collider dimensions are explicit world units, not multiplied by visual scale. Bullet uses world pose/orientation; legacy remains axis-aligned. Raycast, overlaps, and character control use world coordinates. Parent scale/rotation may move a child without resizing its collider. Particle follow tracks owner world position; offsets still do not inherit rotation/scale.

Hierarchy does not inherit visibility, scripts, or data; it manages transforms/lifetime. Scene JSON records parent and local TRS. Editor Parent reparents with keep_world, World displays coordinates, and PBR/Metallic/Roughness edit materials. Other maps use JSON/API. Undo/redo stores scene JSON, not arbitrary Python callbacks.

## Bulk transform updates

position/local_position, rotation/local_rotation, scale/local_scale, world_position setters and move() update only the affected entity/subtree. An independent root no longer rebuilds the whole world. Cached parent poses use double precision; overflow/physics limits are checked before commit. Invalid descendant matrices preserve previous local/world transforms.

For a coordinated update of several objects:

```python
forge.set_positions([
    (player, (10, 0, 0)),
    (child, (2, 0, 0)),        # Local position relative to parent.
    (enemy, (20, 0, 0)),
])
```

Accepts a sequence of (Entity,(x,y,z)) pairs. Entities must be distinct live members of the current world. Updates are atomic: invalid objects, duplicates, NaN/Infinity, or final-tree overflow preserve old positions. Parent and child can change together; the final graph is validated. No JSON serialization is involved. Bulk updates can outperform repeated overlapping subtree updates; ordinary setters remain effective for a few independent objects.

world_stats() includes cumulative transform_audits (public-field scans) and transform_computations (computed world matrices). An unchanged world is audited without recomputing matrices. Python world_position/world_matrix/world_rotation reads retain audits for native-extension compatibility.

C++ modules use World::setLocalTransform, setPositions, and setWorldPosition. After direct public Entity writes or structural changes, call world.syncTransforms() before fast setters/cached pose reads. Explicit sync, physics, and rendering audit public fields. The cache cannot intercept arbitrary C++ writes; extensions retain this responsibility.

## Prefab instances and inheritance

Existing single-entity prefab remains valid. A new JSON prefab can hold an entire hierarchy:

```json
{
  "entities": [
    {"id": "root", "kind": "empty"},
    {"id": "mesh", "parent": "root", "kind": "mesh", "model": "authoring.gltf"}
  ]
}
```

Prefab IDs are local. Exactly one root is required, all parents belong to the prefab, and cycles are forbidden. Instance IDs receive a prefix; empty prefix makes the engine select a unique one. The limit is 8192 entities. A root may attach to an existing entity; position offsets its local root position while preserving child positions.

```python
from prefabs import Prefab
actor = Prefab('animated-actor.json')
a = actor.instantiate(prefix='player_', position=(2, 0, 0))
b = actor.instantiate(overrides={'mesh': {'color': [.2, .8, 1, 1]}})
a['mesh'].visible = False      # b is unchanged.
print(a.root, actor.describe())
a.destroy()                   # Removes the instance tree.
```

forge.load_prefab(file) returns a normalized document copy. instantiate_prefab(file,prefix='',overrides={},parent=None,position=(0,0,0)) returns local-name → Entity dict. parent is a current Entity, its ID, or None. Overrides use local names and cannot change id/parent; use hierarchy methods afterward. Mutable JSON/data/playback are independent; imported models and immutable CPU/GPU resources are shared.

{"extends":"base.json",...} inherits through JSON Merge Patch: objects merge, arrays replace, null removes fields. Inheritance depth is 64; cycles fail. Hierarchies use instantiate; single entities also support existing spawn/prefab. Creation validates before publishing the tree; resource/hierarchy/overflow failure removes partial entities. Behavior attaches in the next lifecycle pass. Arbitrary external Behavior side effects are not part of the creation transaction.
