"""Named actions, contexts, persistent remapping, gamepads and full-frame replay."""
import copy
import json
import math
from pathlib import Path
import forge
from saves import SaveManager, _atomic

PAD_BUTTONS = dict(zip(('a','b','x','y','left_bumper','right_bumper','back','start','guide','left_stick','right_stick','up','right','down','left'),range(15)))
PAD_AXES = dict(zip(('left_x','left_y','right_x','right_y','left_trigger','right_trigger'),range(6)))

class ActionMap:
    def __init__(self, bindings=None, *, deadzone=.15, automatic=True, directory=None):
        if not math.isfinite(deadzone) or not 0<=deadzone<1: raise ValueError('Invalid deadzone')
        self.bindings, self.deadzone = {}, deadzone
        self.values, self.previous, self.contexts = {}, {}, ['default']
        self.store = SaveManager(directory=directory or forge.settings().get('save_directory','saves')+'/preferences')
        for name, value in (bindings or {}).items(): self.bind(name, value)
        self._listener = forge.on_frame(self.update) if automatic else None
    @staticmethod
    def _binding(value):
        value = {'input':value} if isinstance(value,str) else dict(value)
        parts = value['input'].split(':')
        if parts[0]=='key' and len(parts)==2: parts[1]=parts[1].upper()
        elif parts[0]=='mouse' and len(parts)==2 and parts[1].isdigit() and 0<=int(parts[1])<=7: pass
        elif parts[0] in ('pad','axis') and len(parts)==3 and parts[1].isdigit() and 0<=int(parts[1])<=15:
            if parts[2] not in (PAD_BUTTONS if parts[0]=='pad' else PAD_AXES): raise ValueError('Unknown gamepad control')
        else: raise ValueError('Binding needs key:SPACE, mouse:0, pad:0:a or axis:0:left_x')
        scale = value.get('scale',1)
        if not math.isfinite(scale): raise ValueError('Binding scale must be finite')
        value['input']=':'.join(parts)
        return value
    def bind(self, action, bindings, *, context='default'):
        if not action: raise ValueError('Action name required')
        if isinstance(bindings,(str,dict)): bindings=[bindings]
        self.bindings.setdefault(context,{})[action]=[self._binding(b) for b in bindings]
    def push_context(self, name): self.contexts.append(name); self.values.clear(); self.previous.clear()
    def pop_context(self):
        if len(self.contexts)==1: raise ValueError('Cannot pop default context')
        self.contexts.pop(); self.values.clear(); self.previous.clear()
    def update(self, dt=0):
        snapshot=forge.input_snapshot(); pads={p['id']:p for p in snapshot.get('gamepads',[])}
        self.previous=self.values.copy();self.values={}
        for action, bindings in self.bindings.get(self.contexts[-1],{}).items():
            value=0
            for binding in bindings:
                parts=binding['input'].split(':');raw=0
                if parts[0]=='key': raw=float(parts[1] in snapshot.get('keys',[]))
                elif parts[0]=='mouse': raw=float(int(parts[1]) in snapshot.get('buttons',[]))
                elif int(parts[1]) in pads:
                    pad=pads[int(parts[1])]
                    if parts[0]=='pad': raw=float(PAD_BUTTONS[parts[2]] in pad.get('buttons',[]))
                    else:
                        raw=pad.get('axes',[0]*6)[PAD_AXES[parts[2]]]
                        if parts[2].endswith('_trigger'): raw=(raw+1)/2  # GLFW idle=-1; actions idle=0.
                        if abs(raw)<=self.deadzone: raw=0
                        else: raw=math.copysign((abs(raw)-self.deadzone)/(1-self.deadzone),raw)
                value+=raw*binding.get('scale',1)
            self.values[action]=max(-1,min(1,value))
    def value(self, action): return self.values.get(action,0)
    def down(self, action): return abs(self.value(action))>.001
    def pressed(self, action): return self.down(action) and abs(self.previous.get(action,0))<=.001
    def released(self, action): return not self.down(action) and abs(self.previous.get(action,0))>.001
    def save(self): self.store.write('input_actions',self.bindings)
    def load(self):
        data=self.store.read('input_actions',default=self.bindings);validated={}
        for context, actions in data.items():
            validated[context]={name:[self._binding(b) for b in bindings] for name,bindings in actions.items()}
        self.bindings=validated
    def close(self):
        if self._listener is not None: forge.remove_listener(self._listener);self._listener=None

class InputRecorder:
    def __init__(self, *, max_frames=360000, automatic=True):
        if type(max_frames) is not int or max_frames < 1: raise ValueError('max_frames must be a positive integer')
        self.frames=[];self.max_frames=max_frames
        self._listener=forge.on_frame(self.update) if automatic else None
    def update(self, dt=0):
        if len(self.frames)>=self.max_frames: raise RuntimeError('Input recording limit reached')
        self.frames.append(copy.deepcopy(forge.input_snapshot()))
    def save(self, file):
        payload=dict(format='forge.input/1',frames=copy.deepcopy(self.frames))
        def write():
            path=Path(forge.storage_path(file));path.parent.mkdir(parents=True,exist_ok=True)
            _atomic(path,json.dumps(payload,allow_nan=False).encode('utf-8'))
        forge.defer_persistence(write)
    def close(self):
        if self._listener is not None: forge.remove_listener(self._listener);self._listener=None

class InputReplay:
    def __init__(self, recording, *, loop=False, automatic=True):
        if isinstance(recording,(str,Path)): recording=json.loads(Path(forge.storage_path(str(recording))).read_text(encoding='utf-8'))
        if recording.get('format')!='forge.input/1' or not isinstance(recording.get('frames'),list): raise ValueError('Invalid input recording')
        self.frames=copy.deepcopy(recording['frames']);self.index=0;self.loop=loop
        self._listener=forge.on_frame(self.update) if automatic else None
    @property
    def finished(self): return self.index>=len(self.frames) and not self.loop
    def update(self, dt=0):
        if not self.frames or self.finished: return
        forge.inject_input(self.frames[self.index%len(self.frames)]);self.index+=1
    def close(self):
        if self._listener is not None: forge.remove_listener(self._listener);self._listener=None
