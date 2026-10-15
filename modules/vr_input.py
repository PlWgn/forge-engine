"""Optional, editable VR input helpers over Forge's native OpenXR snapshot.

Locomotion, collision, comfort settings, interaction rules and menus belong to games.
"""
import copy
import math
import forge


def pose_rotation(pose):
    """Convert a valid OpenXR [x,y,z,w] quaternion to Forge XYZ Euler degrees."""
    if not pose.get('valid'):
        raise ValueError('Pose is not valid')
    x, y, z, w = pose['orientation']
    # Match composeTransform's Rx * Ry * Rz and extractEulerAngleXYZ,
    # including a stable equivalent rotation at the +/-90 degree yaw poles.
    m00, m01, m02 = 1-2*(y*y+z*z), 2*(x*y-w*z), 2*(x*z+w*y)
    m10, m11, m12 = 2*(x*y+w*z), 1-2*(x*x+z*z), 2*(y*z-w*x)
    m20, m21, m22 = 2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)
    pitch = math.atan2(-m12, m22)
    yaw = math.atan2(m02, math.hypot(m00, m01))
    s, c = math.sin(pitch), math.cos(pitch)
    roll = math.atan2(s*m20+c*m10, c*m11+s*m21)
    return tuple(math.degrees(a) for a in (pitch, yaw, roll))


def aim_ray(pose):
    """Return a world-space (origin, direction) for raycast; -Z is forward."""
    if not pose.get('valid'):
        raise ValueError('Pose is not valid')
    x, y, z, w = pose['orientation']
    return tuple(pose['position']), (-2*(x*z+w*y), -2*(y*z-w*x), -(1-2*(x*x+y*y)))


class ControllerInput:
    """Scene-owned edge detection; call update once in on_update, even while paused.

    Focus loss clears controls without fabricating release/press events. A held
    control must return below its threshold before it can press after refocus.
    """
    def __init__(self, threshold=.5):
        if isinstance(threshold, bool) or not math.isfinite(threshold) or not 0 < threshold <= 1:
            raise ValueError('threshold must be finite in (0,1]')
        self.threshold=threshold
        self.state={'enabled':False}
        self._down=set();self._pressed=set();self._released=set();self._suppressed=set()
        self._focused=False

    def update(self, snapshot=None):
        state=forge.xr_state() if snapshot is None else copy.deepcopy(snapshot)
        focused=bool(state.get('focused'))
        down=set()
        if focused:
            for hand, values in state.get('hands',{}).items():
                for control in ('select','primary','secondary','menu','trigger','squeeze'):
                    value=values.get(control,False)
                    if value >= self.threshold:down.add((hand,control))
        if not focused:
            self._suppressed.clear();self._pressed.clear();self._released.clear();self._down.clear()
        else:
            if not self._focused:self._suppressed=down.copy()
            self._suppressed.intersection_update(down)
            active=down-self._suppressed
            self._pressed=active-self._down;self._released=self._down-active;self._down=active
        self._focused=focused;self.state=state
        return self.state

    def down(self, hand, control='trigger'):return (hand,control) in self._down
    def pressed(self, hand, control='trigger'):return (hand,control) in self._pressed
    def released(self, hand, control='trigger'):return (hand,control) in self._released
