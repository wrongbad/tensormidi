from .tensormidi_bind import load_merge as _load_merge


class Track:
    NOTE_OFF = 0x80
    NOTE_ON = 0x90
    POLY_AFTERTOUCH = 0xA0
    CONTROL = 0xB0
    PROGRAM = 0xC0
    CHAN_AFTERTOUCH = 0xD0
    PITCH_BEND = 0xE0
    SYSEX_BEGIN = 0xF0
    SYSEX_END = 0xF7
    # note 'dt' units are microseconds
    NUMPY_DTYPE = [('dt', 'u4'), ('type', 'u1'), ('channel', 'u1'), ('key', 'u1'), ('value', 'u1')]

class MergedTrack(Track):
    def __init__(self, filename):
        x = _load_merge(filename)
        self.events = x.view(Track.NUMPY_DTYPE)[:, 0]
