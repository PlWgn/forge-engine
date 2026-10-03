import forge

def on_start():
    forge.log('Проект запущен. C++ ядро + Python сценарии.')

def on_destroy():
    forge.log('Сессия завершена.')
