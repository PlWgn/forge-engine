class Behavior:
    def __init__(self, entity, properties):
        self.entity, self.speed = entity, properties.get('speed', 28)
    def on_update(self, dt):
        x, y, z = self.entity.rotation
        self.entity.rotation = (x, y + self.speed * dt, z)
