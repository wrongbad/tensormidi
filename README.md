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

midi = tensormidi.load('bach/catech7.mid')

print(f'{midi.shape=}')
print(f'{midi.dtype=}')
for k in midi.dtype.names:
    print(k, midi[0][k])
```

    midi.shape=(1440,)
    midi.dtype=dtype((numpy.record, [('time', '<f8'), ('track', 'u1'), ('program', 'u1'), ('channel', 'u1'), ('type', 'u1'), ('key', 'u1'), ('value', 'u1')]), align=True)
    time 1.2
    track 4
    program 19
    channel 3
    type 144
    key 43
    value 80


All your favorite array-level ops just work


```python
import numpy as np

notes = np.sum(midi.type == tensormidi.NOTE_ON)
length = np.max(midi.time)
print(f'{notes=}')
print(f'{length=}')
```

    notes=720
    length=79.60473141666768


Field accessors are normal numpy array views, understood by other libraries


```python
import torch

torch.tensor(midi.time)
```




    tensor([ 1.2000,  1.2000,  1.2000,  ..., 79.6047, 79.6047, 79.6047],
           dtype=torch.float64)



### Output

The track output is a contiguous array of numpy `record` (the memory layout is the same as an array of structs in C/C++)

field | dtype | description
--- | --- | ---
`time` | float64 | seconds or ticks since beginning of song 
`track` | uint8 | track index the event originates from
`program` | uint8 | most recent program for the channel (or `default_program`)
`channel` | uint8 | midi channel
`type` | uint8 | event type (see below)
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

Notably `PROGRAM_CHANGE` is handled internally, populating the `program` field on every event.

If `merge_tracks=True` a single track is returned containing all events chronologically. Otherwise a list of arrays is returned, one for each track.

If `seconds=True` then `time` field is seconds. Otherwise it is midi-ticks. In seconds mode, only the tracks are returned.

In ticks mode, `load()` returns `tracks, tempos, ticks_per_beat` where tempos is an array of record `(tick, sec_per_beat)`. Note `tick` is since beginning of score, not a delta.

If `notes_only=True` then only `NOTE_ON` and `NOTE_OFF` events are included.


### C++ Linkage

The C++ library is header only with clean C++ APIs, unbiased by the python bindings.

Header include path can be dumped with `python -m tensormidi.includes` for easy makefile use.

Of course you could just clone this repo and point to `src/tensormidi/include` as well.

### Numba Example

Numpy record arrays work perfectly with numba.

Here is an example of how you can compute note durations with simple code that is also very fast.


```python
%pip install numba
```


```python
import numba

# @numba.jit
def durations(midi):
    n = len(midi)
    out = np.zeros(n, dtype=np.float32)
    off_time = np.zeros((16, 128), dtype=np.float64)
    for i in range(n-1, -1, -1):
        e = midi[i]
        if e.type == tensormidi.NOTE_ON:
            out[i] = off_time[e.channel, e.key] - e.time
        elif e.type == tensormidi.NOTE_OFF:
            off_time[e.channel, e.key] = e.time
    return out

midi = tensormidi.load('bach/catech7.mid')

print("pure python")
%timeit durs = durations(midi)

print("with numba")
jitdurations = numba.jit(durations)
%timeit durs = jitdurations(midi)

durs = jitdurations(midi)
durs = durs[midi.type == tensormidi.NOTE_ON]
notes = midi[midi.type == tensormidi.NOTE_ON]

print("")
print(notes[:20].key)
print(durs[:20])
```

    pure python
    8.87 ms ± 79.1 µs per loop (mean ± std. dev. of 7 runs, 100 loops each)
    with numba
    2.43 µs ± 1.98 ns per loop (mean ± std. dev. of 7 runs, 100,000 loops each)
    
    [43 43 43 43 62 64 66 67 82 45 45 45 45 72 69 67 66 67 81 64]
    [1.05 1.05 1.05 1.05 0.13 0.13 0.13 0.86 0.26 1.05 1.05 1.05 1.05 0.78
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

midi = tensormidi.load('bach/catech7.mid')
audio = np.zeros((0,2), np.int16)

for m in midi:
    nsamp = int(samplerate * m.time)
    if nsamp > audio.shape[0]:
        # make the audio engine catch up to current time
        nsamp -= audio.shape[0]
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
