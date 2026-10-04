"""Rigid bodies, a capsule controller and native particle effects in one scene."""
import forge
from character import CharacterController
from particles import Emitter
from physics import RigidBody
from ui import text


def on_start():
    global controller, sparks
    controller = CharacterController(forge.find('player'), speed=5)
    sparks = Emitter(name='Sparks', texture='particle.png', rate=0, max_particles=300,
                     velocity=[0,3,0], velocity_random=[3,2,3], gravity=[0,-5,0],
                     lifetime=[.5,1.5], size=[.15,0], color_start=[1,.6,.1,1],
                     color_end=[1,.1,0,0], blend='additive', seed=18)
    text('simulation-title',forge.message('simulation.title'),30,30,30)
    text('simulation-help',forge.message('simulation.help'),30,76,18)
    forge.log('Forge 2.1 particles + Bullet 3D ready')


def on_update(dt):
    controller.walk(int(forge.key_down('D'))-int(forge.key_down('A')),
                    int(forge.key_down('S'))-int(forge.key_down('W')))
    if forge.key_pressed('SPACE'): controller.jump()
    if forge.key_pressed('E'):
        sparks.move(controller.entity.position)
        sparks.burst(100)
    if forge.key_pressed('R'):
        ball=forge.find('ball')
        ball.position=(-4,4,0);ball.velocity=(0,0,0);ball.angular_velocity=(0,0,0)
        RigidBody(ball).impulse((2,1,0))
    if forge.key_pressed('ESCAPE'):forge.quit()


def on_destroy():
    controller.close()
    sparks.close()
