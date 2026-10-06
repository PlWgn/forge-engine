# Assets and Procedural Geometry

[Forge wiki](../../GUIDE.md) · **Assets and Procedural Geometry**

Load CPU resources in workers, pin them with handles, and generate geometry through the scene registry. Uploads use the graphics thread.

- [Loading and cache budgets](#loading-and-cache-budgets)
- [Procedural meshes](#procedural-meshes)

## Loading and cache budgets

```python
from assets import AssetHandle, ScenePreloader
loader = ScenePreloader.scene('level.json')  # Supported declarative asset references.
# Do not block the loading screen: poll loader.progress/ready from on_frame.
if loader.ready: forge.change_scene('level.json')
# loader.close() releases pins; the cache may continue using the resource.

with AssetHandle('audio', 'intro.srt') as handle:
    data = handle.wait(timeout=10).bytes()
```

ScenePreloader also accepts an explicit list of (group,file); use this for Python scenes. Single-entity JSON prefabs resolve inheritance/overrides and include PBR maps from material/material_properties. Virtual @mesh:/@target: references do not enter the file loader; the game creates them. Hierarchy prefabs use instantiate and require an explicit preload list. The scene helper also discovers model/texture LOD assets in `optimization.levels` and emitter textures. Resources used only by Python code still need explicit entries. A closed loader can be populated again. JSON supports preload: [{"group":"audio","file":"intro.wav"}]. AssetHandle.info contains queued/loading/ready/failed, bytes, error, path, and group; images add width/height and models add a model metadata object under model (also returned by forge.model_info()). wait() is for setup/testing, not every frame. Four workers read/decode without graphics calls; GPU upload remains on the renderer's main thread. No additional Python packages are needed.

The CPU cache identifies file generations by canonical path, asset group, modification time, and size. New requests see size changes even if a tool preserves modification time; existing pinned handles keep their previous generation. This is metadata caching, not content hashing: for edits that preserve both time and size, change the timestamp or restart the runtime. A reload alone does not guarantee a new CPU cache generation. Imported models also track files read by Assimp, such as glTF binary buffers and OBJ material files; changed/missing dependencies invalidate subsequent requests while pinned handles keep their earlier model. A new request retries a failed model load after sidecar repair without requiring an edit to the main file.

asset_budget_bytes (default 256 MiB) limits resident CPU cache; renderer.gpu_budget_bytes (128 MiB) limits accounted GPU resources. forge.set_asset_budget(bytes) changes the CPU budget; forge.asset_stats() reports handles, entries, workers, loading, and resident_bytes. A live handle pins an asset against LRU eviction; close/context managers release it predictably. Insufficient budget produces an explicit error. These budgets exclude some CPython/Assimp/driver allocations and transient decoding peaks. validate/build fully decode and check content. run/dev/edit check structure/references and prepare current-scene resources; future scenes may preload in workers. Reload prepares models/textures/glyphs/framebuffers before commit, preserving the old scene on failure. Preload does not replace file validation.

## Procedural meshes

```python
import forge
from geometry import Mesh
mesh = Mesh('triangle', [[-1,0,0],[1,0,0],[0,1,0]],
            colors=[[1,0,0,1],[0,1,0,1],[0,0,1,1]])
entity = forge.spawn({'kind':'mesh', 'model':mesh.model})
mesh.update([[-2,0,0],[2,0,0],[0,2,0]])
entity.destroy()
mesh.close()
```

forge.set_mesh(name,data) returns @mesh:name. Data contains positions XYZ, optional triangle indices, normals XYZ, uvs XY, and colors RGBA 0..1. Optional arrays must match the position count. Without indices, positions form triples; without normals the engine calculates flat triangle normals. Values must be finite, indices in range, with at most 2M positions/2M indices. Names contain 1..128 ASCII letters/digits/spaces or -_., optionally prefixed @mesh:.

Updates replace data entirely and increment revision; invalid input/CPU budget preserves the old mesh. GPU buffers update on the graphics-context thread during preparation/rendering. Budgets are checked before removing the old GPU mesh, but driver allocation errors have no general rollback guarantee. remove_mesh(name) rejects live Entity references, including procedural models in their LOD levels, and tolerates an absent name. A later render releases unused removed procedural GPU meshes.

mesh_info(name) reports model/vertices/bytes/revision; geometry_stats() reports meshes/resident_bytes/budget_bytes/revision. geometry_budget_bytes defaults to 64 MiB, range 1..1 GiB. set_geometry_budget(bytes) changes the scene limit only if it covers resident meshes. It is independent of CPU asset/GPU budgets. Input JSON/Python, temporary arrays, old shared copies, and driver memory are excluded; this is not a process-wide memory limit.

The registry belongs to the scene: transitions/reload create a new one and rejection restores the old one. Reusing a name/revision in another scene does not reuse its old GPU mesh. Create declarative @mesh data in scene on_start before readiness checks. scene_data/save_scene store references, not vertices; your generator recreates them on startup. Editor undo retains the current registry, not generation history; failed authoring candidates use an isolated registry.

Native providers use forge/geometry.hpp and geometry(world).set/remove with the common data format; each generator needs no validator modification. Custom-format loading belongs to a provider registering @mesh results. There is no general arbitrary URI/custom-attribute registry. Chunks, terrain, and water remain game modules.
