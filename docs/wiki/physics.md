# Physics and Character Controllers

[Forge wiki](../../GUIDE.md) · **Physics and Character Controllers**

Legacy AABB remains the default. Select Bullet for optional rotating 3D bodies and convex character queries.

- [Legacy physics](#legacy-physics)
- [Bullet configuration and shapes](#bullet-configuration-and-shapes)
- [Forces, impulses, and queries](#forces-impulses-and-queries)
- [Convex character movement](#convex-character-movement)
- [Character controller wrapper](#character-controller-wrapper)
- [Batch physics queries](#batch-physics-queries)

## Legacy physics

In the default legacy backend, collider specifies full dimensions of an axis-aligned rectangle/box centered on the entity's world position. 2D uses X/Y; 3D requires positive X/Y/Z. Collider dimensions do not inherit visual scale or rotation. Update collider yourself when scaling an entity. Bullet's shapes/rotation are described in [physics and character controllers](physics.md).

Dynamic entities receive gravity, integrate at a fixed 1/120-second step, and resolve penetration along the smallest overlap axis. Mass distributes correction and impulse. Static bodies do not move through integration. Triggers generate contact enter/exit without correction. Nonpositive width/height disables a collider; 3D also requires positive depth. Contacts, overlaps, and raycast share this rule and exclude destroyed entities. Only active colliders enter pair selection, so UI without colliders is excluded. Legacy intermediates use double; positions/velocities are range-checked before float writes. Arithmetic failure reports an entity ID and restores the entire legacy step rather than leaving partial updates or NaN/Infinity. Dev pauses on errors.

The default legacy backend is basic AABB physics without rotating bodies, friction, restitution, joints, mesh colliders, or continuous collision detection. Small fast objects may tunnel. Enable Bullet for rotating bodies, friction, and more accurate 3D collision ([physics and character controllers](physics.md)), or replace the physics module for other models. The starter 3D free camera is intentionally not a physical character controller.

## Bullet configuration and shapes

In a JSON scene or Python build() result:

```json
{
  "mode": "3d",
  "gravity": [0, -9.81, 0],
  "physics": {"backend": "bullet", "iterations": 20, "max_bodies": 10000},
  "entities": [
    {"id": "floor", "kind": "cube", "position": [0, -0.5, 0],
     "scale": [20, 1, 20], "collider": [20, 1, 20],
     "rigid_body": {"friction": 0.8}},
    {"id": "box", "kind": "cube", "position": [0, 3, 0],
     "collider": [1, 1, 1], "dynamic": true, "mass": 2,
     "angular_velocity": [0, 1, 0],
     "rigid_body": {"shape": "box", "friction": 0.8, "restitution": 0.2}}
  ]
}
```

physics is also accepted at engine.json's top level; scenes override individual fields. Default backend is legacy. Global Bullet applies to 3D; 2D without its own selection remains legacy. Explicit Bullet in 2D raises. At runtime, call physics.enable_3d(iterations=20,max_bodies=10000) or forge.configure_physics({...}); this replaces backend settings, so validate bodies before switching. physics_settings()/physics_stats() return configuration/diagnostics including backend, bodies/sleeping bodies, Bullet version, and precision.

Bullet 3.25 is statically compiled with double precision; the game's private Python needs no PyBullet. Dynamic bodies translate/rotate with contact impulses, friction, restitution, and sleeping. Static bodies have dynamic:false and rigid_body.kinematic:false. Kinematic bodies have dynamic:false, kinematic:true, with game-controlled pose affecting dynamics. dynamic:true and kinematic:true together are rejected. Triggers participate in queries and Behavior callbacks without pushing bodies.

collider is full world-unit dimensions independent of visual scale. The shape rotates with Entity.rotation:

| shape | Dimensions |
| --- | --- |
| box (default) | X/Y/Z lengths of the rotating box |
| sphere | Diameter is min(X,Y,Z) |
| capsule | Y axis; radius is min(X,Z)/2; Y is total height including hemispheres, at least the diameter |

Any nonpositive axis disables a 3D collider consistently for solver, raycast, and controller. Objects without active colliders retain free fall without collision/rotational simulation.

| rigid_body | Default / Range |
| --- | --- |
| friction | 0.5 / 0..10 |
| rolling_friction, spinning_friction | 0 / 0..10 |
| restitution | 0 / 0..1; both bodies' coefficients affect Bullet response |
| linear_damping, angular_damping | 0 / 0..1 |
| sleep | true; false disables automatic sleeping |
| ccd | true for dynamic bodies; enables swept-sphere protection for fast motion |
| kinematic | false |
| group, mask | 1 and 65535 / bit masks 1..65535 and 0..65535 |
| max_slope | 45 / 0..89 degrees for controller grounded detection |

Bodies collide when each body's group is allowed by the other's mask. mask=0 disables contacts. iterations is 1..100 (default 20); max_bodies is 1..100000 (default 10000), including statics/triggers. Raise budgets deliberately; limits do not guarantee a fixed frame time.

## Forces, impulses, and queries

```python
import forge
from physics import RigidBody, raycast

body = RigidBody(forge.find('box'), friction=.8, angular_damping=.1)
body.force((10, 0, 0))                    # Newtons: acceleration depends on mass.
body.torque((0, 1, 0))                    # Torque.
body.impulse((0, 2, 0), point=(.5, 3, 0))  # Impulse at a world point, causing rotation.
body.wake()
hit = raycast((0, 10, 0), (0, -1, 0), 100, ignore='box')
if hit:
    forge.log(f"{hit['entity'].id}: distance={hit['distance']}, normal={hit['normal']}")
```

Native equivalents: forge.apply_force(entity,vector,point=None), apply_torque(entity,vector), apply_impulse(entity,vector,point=None), and wake_body(entity). point is a world-space point; omission means center of mass. Impulse requires a dynamic body with an active Bullet collider. Force/torque accumulate; automatic physics applies on_update force to every fixed step of that frame, then consumes it. Collision-callback force waits until the next frame. Add continuous force each on_update; with physics disabled it stays queued. Legacy also handles central force, but not torque.

Entity.angular_velocity is radians/second, rotation is degrees, linear velocity is world units/second, and mass is nominal kilograms for metric coordinates. Entity.impulse(x,y,z) retains the central linear impulse divided by mass; RigidBody.impulse additionally handles the world application point and Bullet rotation.

forge.set_rigid_body(entity,settings) replaces body settings; RigidBody(entity,**settings) merges with current settings. rigid_body_settings() reports configured fields; rigid_body_info() reports shape/dynamic/kinematic/sleeping. Changes synchronize before the next step/query. forge.overlaps() and forge.raycast() use the scene backend automatically. raycast_hit(origin,direction,distance=1000,mask=65535,ignore='',include_triggers=True) requires Bullet and returns None or a dict with entity ID, position, normal, distance, and fraction; physics.raycast substitutes the Entity for its ID.

forge.physics_step(dt) advances manually regardless of pause/physics_enabled. dt is 0..1 seconds; larger steps subdivide to 1/120. Positive dt consumes queued forces/torques. It invokes no Behavior contact callbacks and does not advance game time. Disable automatic physics first to avoid double stepping. dt=0 updates contacts only.

Active Bullet limits: mass 1e-6..1e6, collider dimensions 1e-4..1e4, absolute position/velocity/force/torque ≤1e6, rotation ≤1e7 degrees, and angular_velocity ≤1e4 rad/s. Gravity is also ≤1e6 per component. Setters/spawn/configuration reject NaN/Infinity and invalid vectors. The complete physics result is checked before writing Entity poses; failure preserves Entity state, logs context, and pauses dev. Legacy retains its own numerical ranges.

## Convex character movement

CharacterController works with both backends. For Bullet use rigid_body.shape:'capsule' and collider:[1,2,1]. It makes the body kinematic, uses convex sweep/normal sliding, and max_slope for grounded state. Slopes are no longer treated as axis-aligned boxes. Bounded penetration recovery precedes sweep; spawn in free space because recovery cannot guarantee escape from every deep/enclosed overlap.

The bridge supports box/sphere/capsule only: no mesh/convex-hull colliders, joints, ragdolls, soft bodies, navmesh, or automatic step climbing. The controller does not fully handle moving platforms or force reactions against dynamics. CCD uses an inscribed swept sphere, reducing translational tunneling without guaranteeing protection for every thin rotating body. Backend recreation preserves Entity fields but resets internal contacts/sleep. scene_data() stores configuration/angular_velocity, not a complete Bullet solver snapshot. Cross-platform determinism and network rollback are not promised.

## Character controller wrapper

```python
from character import CharacterController

# player is an Entity with an active collider, spawned outside obstacles.
controller = CharacterController(player, speed=5, jump_speed=6, automatic=False)
controller.walk(horizontal, forward)  # Normalizes diagonal movement.
controller.jump()                     # Only when grounded.
controller.update(dt)                 # Call from on_update when automatic=False.
# Close when retiring the scene.
controller.close()
```

forge.move_character(entity,delta,skin=.001) performs swept AABB movement along X/Z/Y, limits movement at walls, slides along free axes, and returns position/grounded/hits with IDs/normals. CharacterController makes the body kinematic and adds gravity/speed/jumping. For 2D, give the collider positive Z; collisions ignore that axis. Legacy is an AABB controller without capsule, slope/step climbing, rotating bodies, or automatic recovery from deep initial penetration. Bullet uses capsule/box/sphere convex sweep, slope normals, and bounded penetration recovery; see [physics and character controllers](physics.md). Spawn outside obstacles. forge.set_physics_enabled(False) disables physics independently of UI/scripts; physics_enabled()/gravity() report state.

## Batch physics queries

Bullet caches shape/body settings, world poses, and velocities. Repeated queries on unchanged worlds do not build JSON signatures, reconstruct bodies, reapply identical transforms/velocities, or update AABBs. Collider/mass/shape/pose/force/torque changes, destruction, and ID reuse are checked; changes commit after all active bodies pass validation.

For many rays, use a batch:

```python
hits = forge.raycast_many([
    ((0, 1, 5), (0, 0, -1), 10),
    ((3, 1, 5), (0, 0, -1), 10),
])
for hit in hits:
    if hit is not None:
        forge.log(hit.name)
```

Each item is (origin,direction,distance). Results preserve input order and contain Entity/None like forge.raycast. Both legacy/Bullet and triggers are supported. All input numbers validate before querying. Use raycast_hit/physics for hit position/normal/fraction and mask/ignore.

A batch synchronizes once and queries that state without Python callbacks between rays. physics_stats() adds cumulative sync_audits/body_synchronizations; calling stats itself audits and increments the first. The second counts body creation/pose/velocity updates, not every solver integration.

Public C++ field audits remain linear; legacy rays inspect active colliders while Bullet uses broadphase. Caching does not make arbitrary scalar queries O(1). Batching reduces repeated audits. Collider dimensions/backend limits from [physics and character controllers](physics.md) remain unchanged.
