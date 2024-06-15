from . import tensormidi_bind as _ext
import numpy

NOTE_OFF = 0x80
NOTE_ON = 0x90
POLY_AFTERTOUCH = 0xA0
CONTROL = 0xB0
CHAN_AFTERTOUCH = 0xD0
PITCH_BEND = 0xE0

TRACK_DTYPE = numpy.dtype([
    ('dt', 'u4'),
    ('duration', 'u4'),
    ('track', 'u1'),
    ('program', 'u1'),
    ('type', 'u1'),
    ('channel', 'u1'),
    ('key', 'u1'),
    ('value', 'u1'),
], align=True)

TEMPO_DTYPE = numpy.dtype([
    ('tick', 'u4'),
    ('usec_per_beat', 'u4')
])


def load(
    filename,
    merge_tracks = True,
    microseconds = True,
    notes_only = True,
    durations = False,
    remove_note_off = False,
):
    tracks, tempos, tick_per_beat = _ext.load_midi(
        filename, 
        merge_tracks=merge_tracks,
        microseconds=microseconds,
        notes_only=notes_only, 
        durations=durations,
        remove_note_off=remove_note_off,
    )
    tracks = [
        x.view(TRACK_DTYPE)[:, 0].view(numpy.recarray)
        for x in tracks
    ]
    tracks = tracks[0] if merge_tracks else tracks
    if microseconds:
        return tracks
    else:
        tempos = tempos.view(TEMPO_DTYPE)[:,0].view(numpy.recarray)
        return tracks, tempos, tick_per_beat
