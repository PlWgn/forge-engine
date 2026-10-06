"""Pinned native asset handles and scene preloading. GPU uploads stay on the render thread."""
import json
from copy import deepcopy
import time
from pathlib import Path
import os
import forge

class AssetError(RuntimeError): pass

class AssetHandle:
    def __init__(self, group, file):
        self.group, self.file = group, file
        self.id = forge.asset_request(group, file)
    @property
    def info(self):
        if self.id is None: raise AssetError('Asset handle is closed')
        return forge.asset_info(self.id)
    @property
    def ready(self): return self.info['status'] == 'ready'
    def wait(self, timeout=30):
        deadline = time.monotonic()+timeout
        while True:
            info = self.info
            if info['status'] == 'ready': return self
            if info['status'] == 'failed': raise AssetError(info['error'])
            if time.monotonic() >= deadline: raise TimeoutError(self.file)
            time.sleep(.001)
    def bytes(self): return forge.asset_bytes(self.id)
    def json(self): return json.loads(self.bytes())
    def close(self):
        if self.id is not None:
            forge.asset_release(self.id)
            self.id = None
    def __enter__(self): return self
    def __exit__(self, *args): self.close()
    def __del__(self):
        try: self.close()
        except Exception: pass  # Interpreter/runtime may already be shutting down.

class ScenePreloader:
    def __init__(self, resources=(), *, budget_bytes=None):
        if budget_bytes is not None: forge.set_asset_budget(budget_bytes)
        self.handles = {}
        self._dependencies = set()
        for group, file in resources: self.add(group, file)
    def add(self, group, file):
        key = (group, file)
        if key not in self.handles: self.handles[key] = AssetHandle(group, file)
        return self.handles[key]
    @classmethod
    def scene(cls, file):
        data = json.loads(Path(forge.asset_path('scenes', file)).read_text(encoding='utf-8'))
        result = cls((entry['group'], entry['file']) for entry in data.get('preload', []))
        def add(group, name):
            if name and not name.startswith(('@target:', '@mesh:')): result.add(group, name)
        for emitter in data.get('emitters', []): add('textures', emitter.get('texture'))
        for original in data.get('entities', []):
            entity = dict(original)
            if 'prefab' in entity:
                prefab = forge.load_prefab(entity['prefab'])['entities']
                if len(prefab) != 1: raise AssetError('Entity prefab must contain one object')
                def overlay(base, patch):
                    if not isinstance(patch, dict): return deepcopy(patch)
                    value = deepcopy(base) if isinstance(base, dict) else {}
                    for key, item in patch.items():
                        if item is None: value.pop(key, None)
                        else: value[key] = overlay(value.get(key), item)
                    return value
                entity = overlay(prefab[0], entity)
            for field, group in [('texture','textures'),('model','models'),('material','materials')]:
                add(group, entity.get(field))
            for level in entity.get('optimization', {}).get('levels', []):
                add('models', level.get('model'))
                add('textures', level.get('texture'))
            materials = [entity.get('material_properties', {})]
            if entity.get('material'):
                materials.append(json.loads(Path(forge.asset_path('materials',entity['material'])).read_text(encoding='utf-8')))
            for material in materials:
                for field in ('texture','albedo_texture','normal_texture','metallic_texture','roughness_texture','metallic_roughness_texture','occlusion_texture','emissive_texture'):
                    add('textures', material.get(field))
        return result
    def _expand(self):
        for key, handle in list(self.handles.items()):
            info = handle.info
            if info['status'] == 'failed': raise AssetError(info['error'])
            if info['status'] == 'ready' and 'model' in info and key not in self._dependencies:
                self._dependencies.add(key)
                for path in info['model'].get('textures',[]):
                    relative = os.path.relpath(path, forge.asset_path('textures','.'))
                    self.add('textures', relative)
    @property
    def progress(self):
        self._expand()
        return sum(h.ready for h in self.handles.values())/max(1,len(self.handles)) if self.handles else 1
    @property
    def ready(self): return self.progress == 1
    def wait(self, timeout=30):
        deadline = time.monotonic()+timeout
        while not self.ready:
            if time.monotonic()>=deadline: raise TimeoutError('Scene preload timed out')
            time.sleep(.001)
        return self
    def close(self):
        for handle in self.handles.values(): handle.close()
        self.handles.clear(); self._dependencies.clear()
    def __enter__(self): return self
    def __exit__(self,*args): self.close()
