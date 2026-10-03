"""SRT/JSON subtitles synchronized to the native audio playback cursor, including pause."""
import json
import math
import re
from pathlib import Path
import forge

def load(file):
    path=Path(forge.asset_path('audio',file));text=path.read_text(encoding='utf-8-sig')
    if path.suffix.lower()=='.json': return json.loads(text)
    def seconds(value):
        hours,minutes,seconds=re.split('[:]',value.replace(',','.'))
        return int(hours)*3600+int(minutes)*60+float(seconds)
    cues=[]
    for block in re.split(r'\n\s*\n',text.strip().replace('\r\n','\n')):
        lines=block.splitlines();index=next((i for i,line in enumerate(lines) if '-->' in line),None)
        if index is None: continue
        start,end=lines[index].split('-->');cues.append(dict(start=seconds(start.strip()),end=seconds(end.strip().split()[0]),text='\n'.join(lines[index+1:])))
    return cues

class Subtitles:
    def __init__(self, sound, cues, label, *, automatic=True):
        if isinstance(cues,str): cues=load(cues)
        self.sound=sound.id if hasattr(sound,'id') else sound;self.cues=list(cues);self.label=label
        for cue in self.cues:
            if not all(math.isfinite(cue[k]) for k in ('start','end')) or cue['start']<0 or cue['end']<=cue['start']: raise ValueError('Invalid subtitle interval')
            if not isinstance(cue.get('text',''),str) and 'key' not in cue: raise ValueError('Subtitle needs text or localization key')
        self._listener=forge.on_frame(self.update) if automatic else None
    def update(self, dt=0):
        info=forge.sound_info(self.sound);cursor=info.get('cursor',0)
        current=[forge.tr(c['key'],**c.get('params',{})) if 'key' in c else c.get('text','') for c in self.cues if c['start']<=cursor<c['end']]
        self.label.value='\n'.join(current) if info.get('playing') or info.get('paused') else ''
    def close(self):
        if self._listener is not None: forge.remove_listener(self._listener);self._listener=None
        self.label.value=''
