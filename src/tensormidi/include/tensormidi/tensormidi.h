#pragma once

#include <stdexcept>
#include <vector>
#include <cstring>
#include <algorithm>

namespace tensormidi {

using u8 = uint8_t;

bool err(char const* msg) { throw std::runtime_error(msg); }

struct Stream
{
    u8 const* begin;
    u8 const* end;
    u8 const* cursor = begin;

    u8 const* take(size_t n)
    {
        u8 const* out = cursor;
        (cursor += n) <= end || err("EOF");
        return out;
    }
    u8 peek() const { return *cursor; }
    size_t remain() const { return end - cursor; }
};

template<class T>
T big_endian(u8 const* bytes)
{
    T x = 0;
    for(int i=0 ; i<sizeof(T) ; i++)
        x = (x << 8) | bytes[i];
    return x;
}

template<class T>
T variable_int(Stream & src)
{
    T x = 0;
    for(int i=0 ; i<sizeof(T) ; i++)
    {
        u8 b = *src.take(1);
        x = (x << 7) | (b & 0x7f);
        if(b < 0x80) return x;
    }
    return x;
}

struct ChunkHead
{
    struct HeadChecker
    {
        HeadChecker(u8 const* data, char const* type)
        {
            std::strncmp((char const*)data, type, 4)==0 || err("wrong chunk type");
        }
    };
    HeadChecker check_magic;
    uint32_t length = 0;
    u8 const* data = nullptr;
    
    ChunkHead(Stream & src, char const* type)
    :   check_magic(src.take(4), type),
        length(big_endian<uint32_t>(src.take(4))),
        data(src.take(length))
    {
    }
};

struct Tempo
{
    uint64_t tick;
    double sec_per_beat;
};

struct Event
{
    double time;
    u8 track;
    u8 program;
    u8 channel;
    u8 type;
    u8 key;
    u8 value;
    u8 _reserved[2];

    enum Type
    {
        NOTE_OFF = 0x80,
        NOTE_ON = 0x90,
        POLY_AFTERTOUCH = 0xA0,
        CONTROL = 0xB0,
        PROGRAM = 0xC0,
        CHAN_AFTERTOUCH = 0xD0,
        PITCH_BEND = 0xE0,
        SYSEX_BEGIN = 0xF0,
        QUARTER_FRAME = 0xF1,
        SONG_POINTER = 0xF2,
        SONG_SELECT = 0xF3,
        SYSEX_END = 0xF7,
    };
};

struct Track
{
    struct Meta
    {
        enum Type
        {
            MSG = 0xFF,
            END_OF_TRACK = 0x2F,
            SET_TEMPO = 0x51,
        };
    };

    std::vector<Event> events;

    Track() {}

    Track(Stream & src, std::vector<Tempo> & tempos, 
        int track, bool notes_only=true, int default_program=0)
    {
        ChunkHead head { src, "MTrk" };
        Stream midi { head.data, head.data + head.length };

        u8 status = 0;
        u8 program[16];
        for(int i=0 ; i<16 ; i++) { program[i] = default_program; }
        uint64_t time = 0;

        auto add_event = [&] (u8 type, u8 chan, u8 key, u8 val) {
            events.push_back({ time, u8(track), program[chan],
                chan, type, key, val });
        };
        auto clip = [&] (u8 x) { return std::min<u8>(x, 127); };
        auto check = [&] (u8 x) { return x<128 ? x : err("data byte > 127"); };

        while(midi.remain())
        {
            time += variable_int<uint64_t>(midi);

            u8 peek = midi.peek();

            if( peek >= 0xF8 )
            {
                midi.take(1); // consume peek
                if( peek == Meta::MSG )
                {
                    u8 mtype = *midi.take(1);
                    if( mtype == Meta::END_OF_TRACK )
                        break;
                    uint32_t mlen = variable_int<uint32_t>(midi);
                    u8 const* d = midi.take(mlen);
                    if( mtype == Meta::SET_TEMPO )
                    {
                        if(tempos.size()==0 && time>0)
                            tempos.push_back({0, 0.5});
                        double usec_per_beat = (d[0]<<16)|(d[1]<<8)|d[2];
                        tempos.push_back({time, usec_per_beat / 1e6});
                    }
                }
                continue;
            }
            if( peek >= 0xF0 )
            {
                midi.take(1); // consume peek
                if( peek == Event::SONG_SELECT ) { midi.take(1); }
                else if( peek == Event::SONG_POINTER ) { midi.take(2); }
                else if( peek == Event::QUARTER_FRAME ) { midi.take(1); }
                else if( peek == Event::SYSEX_BEGIN )
                {
                    while( *midi.take(1) != Event::SYSEX_END ) {}
                }
                continue;
            }
            if( peek >= 0x80 ) { status = *midi.take(1); }

            status >= 0x80 || err("missing status byte");
            u8 type = (status & 0xF0);
            u8 chan = (status & 0x0F);

            if( type == Event::NOTE_OFF ||
                type == Event::NOTE_ON )
            {
                u8 const* m = midi.take(2);
                if(type == Event::NOTE_ON && m[1] == 0)
                    type = Event::NOTE_OFF;
                add_event(type, chan, check(m[0]), clip(m[1]));
            }
            else if( type == Event::CONTROL ||
                type == Event::POLY_AFTERTOUCH ||
                type == Event::PITCH_BEND )
            {
                u8 const* m = midi.take(2);
                if(!notes_only)
                    add_event(type, chan, check(m[0]), clip(m[1]));
            }
            else if( type == Event::CHAN_AFTERTOUCH )
            {
                u8 const* m = midi.take(1);
                if(!notes_only)
                    add_event(type, chan, 0, clip(m[1]));
            }
            else if( type == Event::PROGRAM )
            {
                u8 const* m = midi.take(1);
                if(m[0] < 128) // drop invalid programs
                    program[chan] = m[0];
            }
        }
    }

    Track & to_seconds(double ticks_per_beat, std::vector<Tempo> const& tempos)
    {
        auto tempo = tempos.begin();
        double sec_per_tick = 0.5 / ticks_per_beat;
        double seconds = 0;
        uint64_t tick = 0;
        auto step_to = [&] (uint64_t t) {
            seconds += (t-tick) * sec_per_tick;
            tick = t;
        };
        for(Event & e : events)
        {
            while(tempo != tempos.end() && tempo->tick <= e.time)
            {
                step_to(tempo->tick);
                sec_per_tick = tempo->sec_per_beat / ticks_per_beat;
                tempo ++;
            }
            step_to(e.time);
            e.time = seconds;
        }
        return *this;
    }
};

struct File
{
    int type = 0;
    int ticks_per_beat = 0;
    std::vector<Tempo> tempos;
    std::vector<Track> tracks;

    File(Stream & src, bool notes_only=true, int default_program=0)
    {
        ChunkHead head { src, "MThd" };
        type = big_endian<uint16_t>(head.data+0);
        int n_tracks = big_endian<uint16_t>(head.data+2);
        ticks_per_beat = big_endian<uint16_t>(head.data+4);

        for(int i=0 ; i<n_tracks ; i++)
            tracks.emplace_back(src, tempos, i, notes_only);

        for(size_t i=1 ; i<tempos.size() ; i++)
            if(tempos[i-1].tick > tempos[i].tick)
            {
                std::sort(tempos.begin(), tempos.end(), 
                    [] (auto& a, auto& b) { return a.tick < b.tick; });
                break;
            }
    }

    File & to_seconds()
    {
        for(Track & t : tracks)
            t.to_seconds(ticks_per_beat, tempos);
        tempos.clear();
        ticks_per_beat = 0;
        return *this;
    }

    File & merge_tracks()
    {
        Track out;
        size_t n_events = 0;
        for(Track & t : tracks) { n_events += t.events.size(); }
        out.events.reserve(n_events);

        for(Track & t : tracks)
            out.events.insert(out.events.end(), t.events.begin(), t.events.end());
        
        // dumb sort is faster in practice for typical midi files
        std::sort(out.events.begin(), out.events.end(), 
            [] (auto& a, auto& b) { return a.time < b.time; });
        tracks = {out};
        return *this;
    }
};

} // namespace tensormidi