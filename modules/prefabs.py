"""Independent prefab instances; local names remain stable across copies."""
import forge

class Prefab:
    def __init__(self, file):
        self.file = file
    def instantiate(self, *, prefix='', overrides=None, parent=None, position=(0, 0, 0)):
        return PrefabInstance(forge.instantiate_prefab(self.file, prefix, overrides or {}, parent, position))
    def describe(self):
        return forge.load_prefab(self.file)

class PrefabInstance:
    def __init__(self, entities):
        self.entities = entities
        self.root = next(e for e in entities.values() if e.parent not in entities.values())
    def __getitem__(self, name):
        return self.entities[name]
    def destroy(self):
        self.root.destroy()
