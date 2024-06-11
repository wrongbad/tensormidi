from . import tensormidi_bind as _ext

NOTE_OFF = 0x80
NOTE_ON = 0x90
POLY_AFTERTOUCH = 0xA0
CONTROL = 0xB0
PROGRAM = 0xC0
CHAN_AFTERTOUCH = 0xD0
PITCH_BEND = 0xE0
SYSEX_BEGIN = 0xF0
SYSEX_END = 0xF7

NUMPY_DTYPE = [
    ('dt', 'u4'), 
    ('duration', 'u4'), 
    ('program', 'u1'), 
    ('track', 'u1'), 
    ('type', 'u1'), 
    ('channel', 'u1'), 
    ('key', 'u1'), 
    ('value', 'u1'),
    ('_pad0', 'u1'),
    ('_pad1', 'u1'),
]

def load(
    filename,
    merge_tracks = True,
    microseconds = True,
    notes_only = True,
    durations = False,
    remove_note_off = False,
):
    tracks = [
        x.view(NUMPY_DTYPE)[:, 0]
        for x in 
        _ext.load_midi(
            filename, 
            merge_tracks=merge_tracks,
            microseconds=microseconds,
            notes_only=notes_only, 
            durations=durations,
            remove_note_off=remove_note_off,
        )
    ]
    return tracks[0] if merge_tracks else tracks
