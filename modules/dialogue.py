"""Optional dialogue widget: reveal, autoplay and a reusable history journal."""
import math
import forge
from ui import Column, Row, Label, Button, ScrollView, MUTED, PANEL


class _JournalText:
    def __init__(self, entry): self.entry = entry
    def __str__(self):
        speaker = Journal.resolve(self.entry, 'speaker')
        return (speaker+'\n' if speaker else '')+Journal.resolve(self.entry, 'text')


class Journal:
    def __init__(self): self.entries = []
    def append(self, speaker, text):
        entry = dict(speaker=str(speaker), text=str(text))
        for name, value in [('speaker', speaker), ('text', text)]:
            if isinstance(value, forge.LocalizedText):
                entry[name+'_key'], entry[name+'_params'] = value.key, value.params
        self.entries.append(entry)

    @staticmethod
    def resolve(entry, name):
        return str(forge.LocalizedText(entry[name+'_key'], entry.get(name+'_params', {}))) if name+'_key' in entry else entry[name]
    def view(self, *, height=350):
        return ScrollView(*(Label(_JournalText(entry), size=22)
                            for entry in self.entries), height=height, padding=16, background=PANEL)


class Dialogue(Column):
    def __init__(self, pages, *, characters_per_second=35, auto_delay=1.5, on_done=lambda: None,
                 journal=None, **options):
        if not math.isfinite(characters_per_second) or characters_per_second <= 0 or not math.isfinite(auto_delay) or auto_delay < 0:
            raise ValueError('Dialogue reveal speed must be positive; delay must be nonnegative')
        self.pages = [dict(speaker='', text=p) if isinstance(p, (str, forge.LocalizedText)) else dict(p) for p in pages]
        if any(not isinstance(p.get('speaker', ''), (str, forge.LocalizedText)) or not isinstance(p.get('text'), (str, forge.LocalizedText)) for p in self.pages):
            raise ValueError('Dialogue pages need text and optional speaker strings or LocalizedText')
        self.speed, self.auto_delay, self.on_done = characters_per_second, auto_delay, on_done
        self.journal = journal if journal is not None else Journal()
        self.index, self.revealed, self.auto, self.wait, self.finished = 0, 0., False, 0., False
        self.speaker = Label('', size=22, color=(.4, .86, .73, 1))
        self.body = Label('', size=28, flex=1)
        self.auto_button = Button(forge.message('engine.dialogue.auto_off'), self.toggle_auto, size=20)
        self.next_button = Button(forge.message('engine.dialogue.next'), self.advance, size=20)
        options.setdefault('padding', 24)
        options.setdefault('height', 280)
        options.setdefault('background', PANEL)
        super().__init__(self.speaker, self.body, Row(self.auto_button, self.next_button), **options)
        self._revision = forge.localization_revision()
        self._length = 0
        self._listener = forge.on_frame(self.update)
        self._show(record=True)

    def _show(self, record):
        if not self.pages:
            self.finished = True
            self.speaker.value, self.body.value = '', ''
            self.next_button.enabled = False
            return
        page = self.pages[self.index]
        self.speaker.value = page.get('speaker', '')
        text = str(page['text'])
        self._length = len(text)
        self.body.value = text[:int(self.revealed)]
        if record: self.journal.append(page.get('speaker', ''), page['text'])

    def toggle_auto(self):
        self.auto = not self.auto
        self.auto_button.label = forge.message('engine.dialogue.auto_on' if self.auto else 'engine.dialogue.auto_off')
        self.wait = 0

    def advance(self):
        if self.finished: return
        length = len(str(self.pages[self.index]['text']))
        if self.revealed < length:
            self.revealed = length
            self._show(record=False)
            self.wait = 0
        elif self.index+1 < len(self.pages):
            self.index += 1
            self.revealed, self.wait = 0., 0.
            self._show(record=True)
        else:
            self.finished = True
            self.next_button.enabled = False
            self.on_done()

    def update(self, dt):
        revision = forge.localization_revision()
        if revision != self._revision:
            self._revision = revision
            if self.pages:
                length = len(str(self.pages[self.index]['text']))
                self.revealed = length if self.revealed >= self._length else min(self.revealed, length)
                self._show(record=False)
        if self.finished or forge.is_paused() or not self.visible: return
        # Hidden parents and an open modal suspend automatic reading too.
        if self.canvas is not None and (not self._shown or self.canvas.modal_widget is not None): return
        length = len(str(self.pages[self.index]['text']))
        if self.revealed < length:
            self.revealed = min(length, self.revealed+dt*self.speed)
            self._show(record=False)
        elif self.auto:
            self.wait += dt
            if self.wait >= self.auto_delay+length/80:
                self.advance()

    def capture(self):
        return dict(index=self.index, revealed=self.revealed, auto=self.auto,
                    page_complete=self.revealed >= self._length,
                    finished=self.finished, history=list(self.journal.entries))

    def restore(self, state):
        index, revealed = state.get('index'), state.get('revealed', 0)
        if type(index) is not int or not 0 <= index < len(self.pages) or not isinstance(revealed, (float, int)) or not math.isfinite(revealed) or revealed < 0:
            raise ValueError('Invalid dialogue checkpoint')
        history = state.get('history', [])
        if not isinstance(history, list) or any(not isinstance(p, dict) or not isinstance(p.get('speaker'), str) or not isinstance(p.get('text'), str) for p in history):
            raise ValueError('Invalid dialogue journal')
        for entry in history:
            for name in ('speaker', 'text'):
                if name+'_key' in entry:
                    if not isinstance(entry[name+'_key'], str) or not isinstance(entry.get(name+'_params', {}), dict):
                        raise ValueError('Invalid localized journal entry')
                    Journal.resolve(entry, name)
        complete = state.get('page_complete', False)
        if type(complete) is not bool: raise ValueError('Invalid dialogue completion flag')
        length = len(str(self.pages[index]['text']))
        self.index, self.revealed = index, length if complete else min(revealed, length)
        self.auto, self.finished, self.wait = bool(state.get('auto')), bool(state.get('finished')), 0
        self.journal.entries = list(history)
        self.auto_button.label = forge.message('engine.dialogue.auto_on' if self.auto else 'engine.dialogue.auto_off')
        self.next_button.enabled = not self.finished
        self._show(record=False)

    def close(self):
        forge.remove_listener(self._listener)
        super().close()
