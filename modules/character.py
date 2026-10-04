"""Kinematic character: swept AABB (legacy) or convex shape (Bullet), slide and jump.
The entity is kinematic (dynamic=False); collider dimensions are world units.
"""
import math
import forge

class CharacterController:
    def __init__(self, entity, *, speed=5, jump_speed=6, skin=.001, automatic=True):
        if any(not math.isfinite(v) or v < 0 for v in (speed, jump_speed, skin)) or skin > 1:
            raise ValueError('Invalid character parameters')
        if any(v <= 0 for v in entity.collider): raise ValueError('Character needs a 3D collider')
        self.entity, self.speed, self.jump_speed, self.skin = entity, speed, jump_speed, skin
        self.velocity = [0., 0., 0.]
        self.direction, self.grounded, self.hits = (0., 0., 0.), False, []
        entity.dynamic = False
        if forge.physics_settings().get('backend') == 'bullet':
            settings=forge.rigid_body_settings(entity)
            forge.set_rigid_body(entity,dict(settings,kinematic=True))
        self._listener = forge.on_frame(self.update) if automatic else None
    def walk(self, x, z=0):
        if not math.isfinite(x) or not math.isfinite(z): raise ValueError('Direction must be finite')
        length = max(1., math.hypot(x, z))
        self.direction = (x/length, 0., z/length)
    def jump(self):
        if self.grounded:
            self.velocity[1] = self.jump_speed * (-1 if forge.gravity()[1] > 0 else 1)
            self.grounded = False
            return True
        return False
    def move(self, delta):
        result = forge.move_character(self.entity, delta, self.skin)
        self.grounded, self.hits = result['grounded'], result['hits']
        for hit in self.hits:
            normal=hit['normal']
            into=sum(v*n for v,n in zip(self.velocity,normal))
            if into<0:self.velocity=[v-into*n for v,n in zip(self.velocity,normal)]
        return result
    def update(self, dt):
        if not math.isfinite(dt) or not 0 <= dt <= 1: raise ValueError('dt must be 0..1')
        if not self.entity.alive or forge.is_paused() or not forge.physics_enabled(): return
        gravity = forge.gravity()
        for axis in range(3): self.velocity[axis] += gravity[axis]*dt
        return self.move(tuple((self.velocity[i]+self.direction[i]*self.speed)*dt for i in range(3)))
    def close(self):
        if self._listener is not None: forge.remove_listener(self._listener)
        self._listener = None
