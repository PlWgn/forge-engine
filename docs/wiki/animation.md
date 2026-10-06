# Animation and Authoring

[Forge wiki](../../GUIDE.md) · **Animation and Authoring**

Use sprite animation or property timelines for simple movement, and native skeletal layers for models. Playback is independent of editor use.

- [Sprite sheets and tweens](#sprite-sheets-and-tweens)
- [Property clips and timelines](#property-clips-and-timelines)
- [Skeletal layers and playback](#skeletal-layers-and-playback)
- [Retargeting and morph targets](#retargeting-and-morph-targets)
- [Authored skeletal clips](#authored-skeletal-clips)
- [State machines and visual authoring](#state-machines-and-visual-authoring)

## Sprite sheets and tweens

```python
import forge
from animation import SpriteSheet, Tween

# Supply a texture divided into an 8-by-4 grid and a live sprite entity.
sheet = SpriteSheet('actor.png', columns=8, rows=4, frames=range(8))
playback = sheet.animate(actor, fps=12, loop=True)
tween = Tween(actor, 'position', (700, 450, 1), 2)

def on_update(dt):
    playback.update(dt)
    tween.update(dt)
```

Sprite playback changes the entity UV rectangle. SpriteAnimation and Tween require manual update; do not confuse them with an automatically updated Animator or Canvas. Tween requires matching finite vector endpoints and a finite positive duration. Updates require finite nonnegative dt; invalid clock/frame arithmetic or a rejected property setter leaves playback time unchanged.


## Property clips and timelines

animation.AnimationClip stores reusable property tracks of (seconds,value) pairs with strictly increasing times. Numbers/vectors support linear/smooth; use step for strings/bools/discrete UV frames. Endpoint values hold before/after the key range. Property animation is independent of 2D/3D mode, font, or renderer.

```python
from animation import AnimationClip
clip = AnimationClip({
    'position': [(0, (0, 0, 0)), (1, (100, 30, 0))],
    'visible': {'interpolation': 'step', 'keys': [(0, True), (1, False)]}
}, duration=1, events=[{'name': 'half', 'time': .5}], name='enter')
player = clip.play(entity, loop=False)
player.update(dt)             # The game calls this once per frame.
player.pause().seek(.3)
player.play()
for event in player.events(): print(event)
```

Timeline exposes time, speed, playing, finished, seek, pause, play, update, and events. Seek clears the queue without generating events. Marker crossings support loops/reverse speed; finished fires once at a non-looping clip's end. Limits: 128 tracks, 8192 keys/track, 1024 markers, 4096 queued events, dt 0..10 seconds, clocks/seek ≤1e6 seconds. Setters validate; failed track application restores already changed properties. Custom setters with external effects need a game-defined strategy. Property clips are Python modules, not GPU compute or physical root motion.

## Skeletal layers and playback

Assimp imports glTF/GLB/FBX/DAE. model_info additionally reports skeleton name/parent and morph_targets part/name/weight. Existing animation_pose(model,clip,time,loop=True) remains. entity_animation_pose(entity) reports the actual global blended/retargeted/faded pose, with each node matrix flattened into 16 row-major values.

```python
from animation import Animator
animator = Animator(entity, 'Move')
animator.crossfade('Idle', .25)
animator.pause().seek(.5).play()
animator.configure(layers=[
    {'clip': 'Idle', 'weight': .4},
    {'clip': 'Move', 'weight': .6, 'mask': ['Joint'],
     'events': [{'name': 'step', 'time': .5, 'foot': 'left'}]}
])
for event in animator.events(): handle(event)
print(animator.info)
```

An animator has 1..16 layers; a layer includes clip, optional model, weight 0..1, time seconds (default 0), speed (default 1, ±1e6), loop (default true), mask of target-node names, events, mapping, and translation_scale. When the sum of layer weights exceeds one it is normalized; an unfilled contribution uses rest pose. Local TRS blend with quaternion hemisphere alignment; crossfades use slerp. Transitions last 0..60 seconds. Interruptions begin from the current blended pose. Masks affect named nodes without implicit descendant expansion. Global matrices follow local blending.

Native APIs: validate_animator(entity,settings), configure_animator, transition_animator(entity,settings,seconds=.25), crossfade_animation(entity,clip,seconds=.25,speed=1,loop=True), animator_info, seek_animation, update_animation, and animation_events. Configuration is {"layers":[...],"playing":true,"auto_update":true}. Defaults update automatically; set auto_update false for manual stepping. Crossfade preserves the previous auto_update setting. Gameplay pause stops automatic clocks; legacy pause_animation also controls the new animator. Use seek_animation/Animator.seek for the new system; animation_time belongs to legacy playback.

Marker events contain type:marker, clip, and original name/time/parameters; completion events have type:finished and clip. Both sides of a transition can emit markers. Sampling does not invoke user code; games consume the queue. A time-zero marker does not fire initially but is crossed on the next loop. Huge intervals cannot generate millions of events: exceeding budgets rejects the step and preserves clocks/pose. Use seek for jumps. Failed configuration/stepping preserves the entity's previous animation state.

Entity JSON supports animator and morph_weights, validated at spawn/load, saved in scene_data/save_scene, and checked with declarative assets during validate/build. Live Python animators use the same checks. Scene JSON stores configuration/initial time, not a serialized Python state machine or intermediate fade.

## Retargeting and morph targets

To transfer a clip, specify the source model and source → target name pairs:

```python
Animator(target, layers=[{
    'model': 'authoring.gltf', 'clip': 'Move',
    'mapping': {'Joint': 'TargetJoint'}, 'translation_scale': 2
}])
```

Empty mapping matches identical names. Target nodes must be unique; missing names, an empty resolved mapping, and singular source rest scales fail. Retargeting transfers **local rest-pose differences**: scaled translation, scale relative to source rest scale, and quaternion relative to source rest rotation applied to target rest TRS. Unmapped nodes keep rest pose. This suits compatible skeletons; anatomy detection, IK, foot locking, and automatic axis/proportion correction are absent. Prepare mappings/skeletons in DCC or replace the animation module.

Models import position/normal morph deltas, default weights, and animated weight channels, up to 32 targets per mesh part. Weights range −10..10; inputs/results must be finite. Weights animate/blend/crossfade and deform vertices before GPU skinning. set_morph_weights(entity,{'Smile':.7}) overrides corresponding animated weights; an empty dict returns control to clips. morph_weights(entity) copies overrides; morph_vertices(model,part=0,weights={}) returns deformed positions for tools/testing.

Morph deformation runs on CPU and uploads a GPU mesh stream. Instances do not share weights. Skinning remains GPU-based with 128 bones/part and four influences/vertex. This is not GPU morph compute, vertex-cache animation, or arbitrary topology animation. Tangent morph channels are not imported; exact glTF cubic-spline interpolation is not promised because sampling uses imported keys and linear/slerp.

## Authored skeletal clips

Create bone animation without an external clip: a layer with duration (1e-6..1e6 seconds) uses model rest pose and tracks. clip becomes the authored name; duration distinguishes it from imported clips. Without duration, tracks can override imported nodes. Node names refer to the source model before retargeting.

```python
Animator(entity, layers=[{
    'clip': 'Wave', 'duration': 2, 'loop': False,
    'tracks': [{'node': 'Joint',
        'position': [{'time': 0, 'value': [0,0,0]},
                     {'time': 2, 'value': [0,1,0]}],
        'rotation': [{'time': 0, 'value': [0,0,0]},
                     {'time': 2, 'value': [0,0,90]}]}]
}])
```

Position/scale are local vec3; rotation uses Euler degrees converted to quaternion/slerp. Times increase within duration; endpoint values hold. Limits are 128 node tracks, 8192 keys/property, and 65536 keys/layer. Existing blend/mask/event/retarget/fade contracts apply. Export preserves authored tracks/timeline. Create/edit tracks in the animation window without DCC/glTF export. This is not a full DCC: curve tangents, IK, graph nodes, and export of new glTF animations are absent.

Negative looping animator/timeline speed crosses loops backward, including starting at zero; info may report negative internal time. Sampling normalizes to positive clip time. Initial time/explicit seek remain nonnegative. Native/property timeline tests cover reverse looping and markers.

## State machines and visual authoring

AnimationStateMachine(animator,states,transitions,initial=None) validates states/edges/clips. A state has clip/loop/speed/fade or a layer with retarget/mask/events. Transitions have from (name or *), to, optional exit_time (state seconds), and when parameters. Conditions are equality, {"gt":number}, {"lt":number}, or {"trigger":true}. First matching transition wins, at most one per update. set/trigger/enter/update/events/describe control the graph and return description copies. Triggers are consumed after a successful transition; game callbacks are not embedded in sampling.

```python
from animation import AnimationStateMachine

machine = AnimationStateMachine(animator,
    {'idle': {'clip':'Idle'}, 'walk': {'clip':'Move', 'fade':.2}},
    [{'from':'idle', 'to':'walk', 'when':{'moving':True}},
     {'from':'walk', 'to':'idle', 'when':{'moving':False}}])
machine.set('moving', True)
machine.update(dt)
```

Run python tools/forge.py edit --scene editor-empty.json and select/import a mesh. Inspector's **Open animation editor** opens a movable **Forge Animation Editor** window:

- Layer clip selection, weights/speed/loop, adding/removing layers;
- Play/Pause, independent preview while the scene is paused, time scrubbing;
- Transition duration and transactionally validated Apply;
- Visual marker timeline, event names/positions, adding/removing markers;
- New authored bone clip: node/time/local position/Euler rotation/scale and insert/replace/load/delete TRS keys;
- Morph sliders and return to animated weights;
- JSON draft for source models, retarget mappings, and masks with validation;
- Export animation prefab into objects as new JSON without overwriting existing files.

Draft and JSON synchronize explicitly through Draft → JSON / Validate JSON → draft. Apply applies the draft. Morph overrides/applied settings persist in scene JSON with authoring undo/redo. Preview/seek is temporary playback; arbitrary Python state-machine graphs are not serialized. This is a visual playback/layer/bone-key/marker/morph/retarget editor without a state-machine node graph. forge.editor_select(entity) lets extensions select the current entity.

Run the built-in demo with python tools/forge.py dev --scene authoring.py. It shows two prefab instances, separate retargeting, and a property clip; Space transitions the first object, Escape returns to welcome. authoring/retarget models are original Forge resources generated by tests/model_fixture.py and may be changed/replaced.
