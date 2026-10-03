import forge
import ui

pages = [
    forge.message('example.welcome.page1'),
    forge.message('example.welcome.page2'),
    forge.message('example.welcome.page3'),
]
page = 0

def on_start():
    ui.text('brand', 'F O R G E   /   ENGINE 1.2', 64, 40, 18, (0.4, 0.86, 0.73, 1))
    ui.text('title', forge.message('example.welcome.title'), 60, 90, 52)
    ui.text('subtitle', forge.message('example.welcome.subtitle'), 64, 159, 21, (0.62, 0.68, 0.76, 1))
    ui.panel('line', 640, 217, 1152, 2, (0.18, 0.23, 0.3, 1))
    ui.text('section', forge.message('example.welcome.section'), 64, 244, 20, (0.4, 0.86, 0.73, 1))
    ui.text('story', pages[0], 64, 288, 29)
    ui.text('hint', forge.message('example.welcome.hint'), 64, 342, 19, (0.62, 0.68, 0.76, 1))
    ui.text('controls', forge.message('example.welcome.controls'), 64, 606, 20)
    ui.text('status', forge.message('example.welcome.status'), 64, 658, 17, (0.4, 0.86, 0.73, 1))
    forge.log('2D сцена готова.')

def on_update(dt):
    global page
    if forge.key_pressed('ENTER'):
        page = (page + 1) % len(pages)
        forge.find('story').text = pages[page]
    if forge.key_pressed('F5'):
        forge.save('progress', {'page': page, 'position': forge.find('player').position})
        forge.find('status').text = forge.message('example.welcome.saved')
        forge.play_sound('notify.wav', volume=.25)
    if forge.key_pressed('3'): forge.change_scene('interface.py')
    if forge.key_pressed('2'): forge.change_scene('world3d.py')
    if forge.key_pressed('ESCAPE'): forge.quit()
