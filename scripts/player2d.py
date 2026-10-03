import forge

class Behavior:
    def __init__(self, entity, properties):
        self.entity = entity
        self.speed = properties.get('speed', 270)
    def on_update(self, dt):
        x, y, z = self.entity.position
        x += (forge.key_down('D') - forge.key_down('A')) * self.speed * dt
        self.entity.position = (max(100, min(1170, x)), y, z)
        if forge.key_pressed('SPACE') and abs(self.entity.velocity[1]) < 1:
            self.entity.impulse(0, -470, 0)
    def on_collision(self, other):
        if other and other.trigger: forge.log(f'Триггер: {other.name}')
