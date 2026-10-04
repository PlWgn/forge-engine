"""Optional native rigid-body backend. Old scenes retain their legacy AABB solver."""
import forge


def enable_3d(*, iterations=20, max_bodies=10000):
    """Call in a 3D scene, or set scene.physics.backend='bullet' in JSON/build()."""
    forge.configure_physics({'backend': 'bullet', 'iterations': iterations, 'max_bodies': max_bodies})


class RigidBody:
    """Convenience wrapper; the native world owns simulation and object lifetime.

    collider contains full world-unit dimensions, independent from visual scale.
    angular_velocity/torque use radians and seconds; entity.rotation uses degrees.
    """
    def __init__(self, entity, **settings):
        self.entity = entity
        if settings:
            values = forge.rigid_body_settings(entity)
            values.update(settings)
            forge.set_rigid_body(entity, values)

    @property
    def settings(self):
        return forge.rigid_body_settings(self.entity)

    @property
    def sleeping(self):
        return forge.rigid_body_info(self.entity)['sleeping']

    def force(self, value, point=None):
        forge.apply_force(self.entity, value, point)

    def torque(self, value):
        forge.apply_torque(self.entity, value)

    def impulse(self, value, point=None):
        forge.apply_impulse(self.entity, value, point)

    def wake(self):
        forge.wake_body(self.entity)


def raycast(origin, direction, distance=1000, *, mask=65535, ignore='', include_triggers=True):
    """Return hit details and a live Entity, or None. Requires the Bullet backend."""
    hit = forge.raycast_hit(origin, direction, distance, mask, ignore, include_triggers)
    if hit is not None:
        hit['entity'] = forge.find(hit['entity'])
    return hit
