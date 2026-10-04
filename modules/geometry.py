"""Scene-owned native triangle meshes: create/update without temporary model files."""
import forge

class Mesh:
    def __init__(self, name, positions, *, indices=None, normals=None, uvs=None, colors=None):
        self.name = name
        self.model = self.update(positions, indices=indices, normals=normals, uvs=uvs, colors=colors)

    def update(self, positions, *, indices=None, normals=None, uvs=None, colors=None):
        values = {'positions': positions}
        for key, value in [('indices', indices), ('normals', normals), ('uvs', uvs), ('colors', colors)]:
            if value is not None:
                values[key] = value
        self.model = forge.set_mesh(self.name, values)
        return self.model

    @property
    def info(self):
        return forge.mesh_info(self.name)

    def close(self):
        """Destroy referencing entities first; an in-use mesh cannot be removed."""
        forge.remove_mesh(self.name)
