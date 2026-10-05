"""Minimal alternative viewport controller, independent of ImGui.
python tools/forge.py edit --scene editor-empty.json --shell examples/editor/viewport_shell.py
Arrow keys move the selected object, Tab selects, Z/Y undo/redo, S exports.
The shell displays diagnostics in the terminal; a GUI can use its own toolkit.
"""
import forge
API_VERSION = 1

def on_start():
    forge.log('Custom shell: arrows move; Tab selects; Z/Y undo/redo; S saves custom-scene.json')

def on_update(dt):
    state = forge.editor_command({'op':'snapshot'})
    entities = state['scene']['entities']
    if not entities:
        forge.editor_command({'op':'patch','patch':[{'op':'add','path':'/entities/-','value':{'id':'editable','kind':'cube','color':[.2,.8,.7,1]}}]})
        return
    if forge.key_pressed('TAB') or not state['selected']:
        ids = [e['id'] for e in entities]
        index = ids.index(state['selected']) if state['selected'] in ids else -1
        forge.editor_command({'op':'select','id':ids[(index+1)%len(ids)]})
    selected = forge.editor_command({'op':'snapshot'})['selected']
    index = next(i for i,e in enumerate(entities) if e['id']==selected)
    position = list(entities[index]['position'])
    for key,axis,delta in [('LEFT',0,-.25),('RIGHT',0,.25),('UP',1,.25),('DOWN',1,-.25)]:
        if forge.key_pressed(key):
            position[axis] += delta
            forge.editor_command({'op':'patch','patch':[{'op':'replace','path':f'/entities/{index}/position','value':position}]})
    for key,op in [('Z','undo'),('Y','redo')]:
        if forge.key_pressed(key): forge.editor_command({'op':op})
    if forge.key_pressed('S'):
        try: forge.editor_command({'op':'save','file':'custom-scene.json'})
        except RuntimeError as error: forge.log(str(error),'ERROR')
    if forge.key_pressed('ESCAPE'): forge.quit()
