"""Public API example: physical character, two cameras, skinning, lighting and profiling."""
import forge
from character import CharacterController
from input_actions import ActionMap
from ui import Canvas,Column,Label
from menus import MenuController
from ai import Scheduler
from audio import sfx

canvas=menus=actions=controller=scheduler=None

def on_start():
    global canvas,menus,actions,controller,scheduler,status
    actions=ActionMap({'left':['key:A','pad:0:left'],'right':['key:D','pad:0:right'],
                       'forward':['key:W','pad:0:up'],'back':['key:S','pad:0:down'],'jump':['key:SPACE','pad:0:a']})
    controller=CharacterController(forge.find('player'),automatic=False)
    status=Label('',size=18)
    canvas=Canvas(Column(Label('FORGE 2.0 / 3D WORKSHOP',size=36),
        Label('WASD / controller: move · Space: jump · Escape: menu',size=20),
        Label('Live security camera · shadows · glTF skinning · postprocess',size=18),
        status,padding=32))
    menus=MenuController(canvas)
    scheduler=Scheduler()
    scheduler.every(.5,update_stats)
    update_stats()

def update_stats():
    stats=forge.renderer_stats()
    status.value=f"Draw calls: {stats.get('draw_calls',0)} · GPU: {stats.get('gpu_bytes',0)/1048576:.1f} MiB"

def on_update(dt):
    controller.walk(actions.value('right')-actions.value('left'),actions.value('back')-actions.value('forward'))
    if actions.pressed('jump') and controller.jump():
        sfx.play('notify.wav',volume=.1,position=controller.entity.position,lowpass=2500)
    controller.update(dt)

def on_destroy():
    for value in (menus,canvas,actions,controller,scheduler):
        if value is not None:value.close()
