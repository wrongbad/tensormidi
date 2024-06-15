# tensormidi

Extremely fast midi parser returning dense numpy [structured arrays](https://numpy.org/doc/stable/user/basics.rec.html).

Ideal for machine learning pipelines.

Natively supported by [numba](https://numba.pydata.org/) so you can write optimized post-processors and tokenizers in python.

Can parse ~10k midi files per second on single CPU core (Ryzen 7950X)


```python
%pip install git+https://github.com/wrongbad/tensormidi.git
```


```python
import tensormidi
import numpy as np

midi = tensormidi.load('bach/catech7.mid')

print(midi.shape)
print(midi.dtype)
for k in midi.dtype.names:
    print(k, midi[4][k])
```

    (1440,)
    (numpy.record, {'names': ['dt', 'duration', 'program', 'track', 'type', 'channel', 'key', 'value'], 'formats': ['<u4', '<u4', 'u1', 'u1', 'u1', 'u1', 'u1', 'u1'], 'offsets': [0, 4, 8, 9, 10, 11, 12, 13], 'itemsize': 16, 'aligned': True})
    dt 150000
    duration 0
    program 81
    track 2
    type 144
    channel 1
    key 62
    value 80



```python
# numpy.recarray allows pythonic field access via attributes
seconds = np.cumsum(midi.dt / 1e6)
print(len(seconds))
print(seconds[-1])
```

    1440
    79.60461900000054


### Output

The track output is a contiguous array of numpy `record` (the memory layout is the same as an array of structs in C/C++)

field | dtype | description
--- | --- | ---
`dt` | uint32 | microseconds or ticks since previous event
`duration` | uint32 | microseconds or ticks until matching NOTE_OFF
`track` | uint8 | track index the event originates from
`program` | uint8 | most recent program for the channel (default 0)
`type` | uint8 | event type (see below)
`channel` | uint8 | midi channel
`key` | uint8 | multi-purpose (see below)
`value` | uint8 | multi-purpose (see below)

Fields `key` and `value` are multi-purpose for various channel events

type | key | value
--- | --- | ---
NOTE_ON | note | velocity
NOTE_OFF | note | velocity
POLY_AFTERTOUCH | note | pressure
CONTROL | index | value
CHAN_AFTERTOUCH | 0 | pressure
PITCH_BEND | value&127 | value>>7

Notably `PROGRAM_CHANGE` is handled internally, populating the `program` field instead. This could become optional behavior - file an issue if you want it.

If `merge_tracks=True` a single track is returned containing all events chronologically. Otherwise a list of arrays is returned, one for each track.

If `microseconds=True` then `dt` field is microseconds since previous event. Otherwise it is ticks. In microseconds mode, only the tracks are returned.

In ticks mode, `load()` returns `tracks, tempos, ticks_per_beat` where tempos is an array of record `(tick, usec_per_beat)`. Note `tick` is since beginning of score, not a delta.

If `notes_only=True` then only `NOTE_ON` and `NOTE_OFF` events are included.

If `durations=True` then the `duration` field is computed, otherwise it contains `0`.

If `remove_note_off=True` then `NOTE_OFF` events are also dropped.


### C++ Linkage

The C++ library is header only with clean C++ APIs, unbiased by the python bindings.

Header include path can be dumped with `python -m tensormidi.includes` for easy makefile use.

Of course you could just clone this repo and point to `src/tensormidi/include` as well.

### Numba Example

Note: you can get durations by passing durations=True to tensormidi.load

This just shows how easily you can write optimized post processing logic.


```python
import numba

# @numba.jit
def durations(midi):
    n = len(midi)
    out = np.zeros(n, dtype=np.uint32)
    off_time = np.zeros((16, 128), dtype=np.uint32)
    time = 0
    for i in range(n-1, -1, -1):
        e = midi[i]
        if e.type == tensormidi.NOTE_ON:
            out[i] = time - off_time[e.channel, e.key]
        elif e.type == tensormidi.NOTE_OFF:
            off_time[e.channel, e.key] = time
        time += e.dt
    return out

midi = tensormidi.load('bach/catech7.mid')

print("pure python")
%timeit durs = durations(midi)

print("with numba")
jitdurations = numba.jit(durations)
%timeit durs = jitdurations(midi)

durs = jitdurations(midi)
durs = durs[midi.type == tensormidi.NOTE_ON] / 1e6
notes = midi[midi.type == tensormidi.NOTE_ON]

print("")
print(notes[:20].key)
print(durs[:20])
```

    pure python
    9.07 ms ± 30 µs per loop (mean ± std. dev. of 7 runs, 100 loops each)
    with numba
    2.5 µs ± 6.34 ns per loop (mean ± std. dev. of 7 runs, 100,000 loops each)
    
    [43 43 43 43 62 64 66 67 82 72 45 45 45 45 69 67 66 67 81 64]
    [1.05 1.05 1.05 1.05 0.13 0.13 0.13 0.86 0.26 0.78 1.05 1.05 1.05 1.05
     0.13 0.13 0.13 0.13 0.26 0.13]


### FluidSynth Example


```python
%pip install pyfluidsynth
```


```python
from IPython.display import Audio
import fluidsynth
import numpy as np

samplerate = 44100
synth = fluidsynth.Synth(samplerate=samplerate)
synth.sfload('/usr/share/sounds/sf2/FluidR3_GM.sf2')

audio = np.zeros((0,2), np.int16)

for m in midi:
    dt = m.dt / 1e6 # microseconds to seconds
    if dt:
        nsamp = int(samplerate * dt)
        chunk = synth.get_samples(nsamp).reshape(-1, 2)
        audio = np.concatenate((audio, chunk))
    
    # every note event carries program id
    synth.program_change(m.channel, m.program)
    
    if m.type == tensormidi.NOTE_ON:
        synth.noteon(m.channel, m.key, m.value)
    elif m.type == tensormidi.NOTE_OFF:
        synth.noteoff(m.channel, m.key)
    elif m.type == tensormidi.CONTROL:
        synth.cc(m.channel, m.key, m.value)

Audio(data=audio[:, 0], rate=samplerate)
```


```python
!jupyter nbconvert --to markdown readme.ipynb --output ../README.md
```


```python

```
