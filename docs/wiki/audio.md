# Audio and Subtitles

[Forge wiki](../../GUIDE.md) · **Audio and Subtitles**

Named channels, fades, voice limits, spatial sound, DSP, and cursor-based subtitles are available through public APIs.

- [Channels, gain, and fades](#channels-gain-and-fades)
- [Spatial audio, DSP, and subtitles](#spatial-audio-dsp-and-subtitles)

## Channels, gain, and fades

```python
import audio

audio.master_volume(.8)
audio.music.volume = .5
track = audio.music.crossfade('theme.wav', seconds=1.5)
effect = audio.sfx.play('notify.wav', volume=.3)
effect.volume = .2
effect.fade(0, seconds=.5, stop_after=True)
track.pause()
track.resume()
audio.music.stop(fade=1)
```

Predefined channels are audio.music, sfx, voice, and ui. Create any named channel with audio.Channel('ambience'); master is reserved for overall gain. Effective gain is voice × channel × master, with ducking when configured. Values are 0..1; fade durations are finite and nonnegative. Sound.id is stable, playing is the playback state, and volume is voice gain before channel/master. fade changes it linearly; pause/resume retains playback position. A paused sound's fade is paused too. Pausing gameplay alone does not stop audio.

crossfade loads the replacement first, then fades out the previous voices on that channel and fades in the new one. A replacement-file error preserves the old voices. loop=True and volume=1 configure the new music. Finished one-shot sounds and completed stop-fades release their resources.

The native API is available without the Python wrapper: play_sound(file, loop=False, volume=1, channel='sfx', fade=0) → id, stop_sound(id, fade=0), stop_channel(name, fade=0), channel_sounds(name), set_channel_volume(name, value), channel_volume(name), set_sound_volume(id, value), sound_volume(id), fade_sound(id, volume, seconds, stop_after=False), sound_playing(id), pause_sound(id, paused=True), and stop_sounds(). Control commands safely ignore absent/finished IDs. Headless decodes and mixes without an audio device: useful for API tests, but not proof of audible playback.

## Spatial audio, DSP, and subtitles

```python
from audio import voice, music
from subtitles import Subtitles
sound = voice.play('intro.wav', pan=-.5, position=(-2,1,0),
                   lowpass=2000, highpass=100, echo_seconds=.12,
                   echo_decay=.3, echo_wet=.25, stream=True)
sound.pan=.5; sound.position=(2,1,0); sound.configure(pitch=1.1)
caption = Subtitles(sound, 'intro.srt', label)
forge.set_audio_listener((0,1,0),(0,0,-1))
```

Audio supports pan -1..1, pitch .01..8, spatial/position, min_distance/max_distance, lowpass/highpass (0 off, 20..23999 Hz), echo_seconds 0..5, echo_decay 0...95, and echo_wet 0..1. DSP uses actual miniaudio nodes: second-order filters and delay. Non-spatial sources retain normal stereo. sound.info/forge.sound_info report options, cursor/duration, volume, paused/playing, channel, and streaming. audio_settings.follow_camera enables camera-following listener updates.

audio_settings in engine.json configures max_voices (integer 1..1024), channel_limits, overflow (steal_oldest/reject), streaming (auto/stream/decode), and stream_threshold_bytes. Individual stream overrides policy. At capacity, the oldest lowest-priority voice is selected; lower-priority requests cannot displace higher-priority voices. Refusal returns ID 0. Stop/release unwanted looping sounds. configure_audio({...}) updates policy; audio_stats() reports voices/streams/DSP/stolen/dropped/duck_gains.

Ducking rules such as {'source':'voice','target':'music','gain':.3,'attack':.05,'release':.5} lower music during speech and restore it smoothly. Subtitles accept SRT or JSON [{'start':0,'end':2,'text':'...'}]; JSON supports localization key/params. They follow the actual playback cursor, remain during pause, and clear on stop/end. Streaming stalls also stall subtitle timing. InputReplay does not reproduce the audio device byte-for-byte.
