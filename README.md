# tensormidi

Extremely fast no-nonsense midi parser, returning dense numpy [structured arrays](https://numpy.org/doc/stable/user/basics.rec.html).

Ideal for machine learning pipelines. Natively supported by [numba](https://numba.pydata.org/) for writing extremely fast manipulations and tokenizers in python.

Can parse ~10k midi files per second on single CPU core (Ryzen 7950X)

```bash
pip install git+https://github.com/wrongbad/tensormidi.git
```

```py
import tensormidi

midi = tensormidi.load(
    'bach/catech7.mid',
    merge_tracks=True,
    microseconds=True,
    notes_only=True,
    durations=False,
    remove_note_off=False,
)

midi.shape
# (1440,)

midi.dtype
# dtype([('dt', '<u4'), ('duration', '<u4'), ('program', 'u1'), ('track', 'u1'), ('type', 'u1'), ('channel', 'u1'), ('key', 'u1'), ('value', 'u1'), ('_pad0', 'u1'), ('_pad1', 'u1')])

midi[0]['dt'] / 1e6
# 1.2

# deriving information couldn't be easier
seconds = numpy.cumsum(midi['dt'] / 1e6)
seconds[-1]
# 79.60461900000054
```

### Quirks

- Current `program` is attached to every event (no program_change events in the output)
- Fields `key` and `value` are multi-purpose for various channel events
- `key`, `value` = `note`, `velocity` also `key`, `value` = `control`, `value`

### C++ Linkage

Header include path can be dumped with `python -m tensormidi.includes` for easy makefile use. The C++ library is header only with clean C++ APIs, unbiased by the python wrapper.

Of course you could just clone this repo and point to `src/tensormidi/include` as well.

### FluidSynth example

```py
from IPython.display import Audio
import fluidsynth
import numpy as np

midi = tensormidi.load('bach/catech7.mid')

samplerate = 44100
synth = fluidsynth.Synth(samplerate=samplerate)
synth.sfload('/usr/share/sounds/sf2/FluidR3_GM.sf2')

audio = np.zeros((0,2), np.int16)

for m in midi:
    dt = m['dt'] / 1e6 # microseconds to seconds
    if dt:
        nsamp = int(samplerate * dt)
        chunk = synth.get_samples(nsamp).reshape(-1, 2)
        audio = np.concatenate((audio, chunk))

    chan = m['channel']
    k, v = m['key'], m['value']
    
    synth.program_change(chan, m['program'])
    
    if m['type'] == tensormidi.NOTE_ON:
        synth.noteon(chan, k, v)
    elif m['type'] == tensormidi.NOTE_OFF:
        synth.noteoff(chan, k)
    elif m['type'] == tensormidi.CONTROL:
        synth.cc(chan, k, v)

Audio(data=audio[:, 0], rate=samplerate)
```

C++ headers are installed with python packge, and can be found with `python -m tensormidi.includes`
You can use that command in makefiles to compile against the C++ backend directly.