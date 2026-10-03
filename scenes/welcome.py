import forge
import ui

pages = [
    'Любая история начинается с первой сцены.',
    'Сцены, объекты и поведение живут отдельно от ядра.',
    'Это текст новеллы. Измените его во время работы движка.',
]
page = 0

def on_start():
    ui.text('brand', 'F O R G E   /   ENGINE 1.0', 64, 40, 18, (0.4, 0.86, 0.73, 1))
    ui.text('title', 'Создавайте свои миры.', 60, 90, 52)
    ui.text('subtitle', 'Один движок. Сцены на Python. Графика на C++.', 64, 159, 21, (0.62, 0.68, 0.76, 1))
    ui.panel('line', 640, 217, 1152, 2, (0.18, 0.23, 0.3, 1))
    ui.text('section', '01  /  2D + НОВЕЛЛА', 64, 244, 20, (0.4, 0.86, 0.73, 1))
    ui.text('story', pages[0], 64, 288, 29)
    ui.text('hint', 'Enter — продолжить историю     F5 — сохранить     2 — перейти в 3D', 64, 342, 19, (0.62, 0.68, 0.76, 1))
    ui.text('controls', 'A / D — движение     Space — прыжок', 64, 606, 20)
    ui.text('status', 'LIVE   /   Скрипты можно менять без пересборки', 64, 658, 17, (0.4, 0.86, 0.73, 1))
    forge.log('2D сцена готова.')

def on_update(dt):
    global page
    if forge.key_pressed('ENTER'):
        page = (page + 1) % len(pages)
        forge.find('story').text = pages[page]
    if forge.key_pressed('F5'):
        forge.save('progress', {'page': page, 'position': forge.find('player').position})
        forge.find('status').text = 'SAVED   /   Прогресс сохранён в saves/progress.json'
        forge.play_sound('notify.wav', volume=.25)
    if forge.key_pressed('2'): forge.change_scene('world3d.py')
    if forge.key_pressed('ESCAPE'): forge.quit()
