#pragma once

#include <stdexcept>
#include <vector>
#include <string>
#include <algorithm>

#include <iostream>

namespace tensormidi {

bool err(char const* msg)
{
    throw std::runtime_error(msg);
}
bool err(std::string msg)
{
    throw std::runtime_error(msg);
}

struct Stream
{
    uint8_t const* begin;
    uint8_t const* end;
    uint8_t const* cursor = begin;

    uint8_t const* take(size_t n)
    {
        uint8_t const* out = cursor;
        (cursor += n) <= end || err("EOF");;
        return out;
    }
    size_t remain() const { return end - cursor; }
};

template<class T>
T big_endian(uint8_t const* bytes)
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
        uint8_t b = *src.take(1);
        x = (x << 7) | (b & 0x7f);
        if(b < 0x80) return x;
    }
    return x;
}

struct ChunkHead
{
    std::string chunk_type;
    uint32_t length = 0;
    uint8_t const* data = nullptr;
    
    ChunkHead() {}
    ChunkHead(Stream & src)
    :   chunk_type((char const*)src.take(4), 4),
        length(big_endian<uint32_t>(src.take(4))),
        data(src.take(length))
    {
    }
};

struct Tempo
{
    uint32_t tick;
    uint32_t usec_per_beat;
};

struct Event
{
    uint32_t dt;
    uint32_t duration;
    uint8_t program;
    uint8_t track;
    uint8_t type;
    uint8_t channel;
    uint8_t key;
    uint8_t value;
    uint8_t _reserved[2];

    enum Type
    {
        // SET_TEMPO = 0x10,
        NOTE_OFF = 0x80,
        NOTE_ON = 0x90,
        POLY_AFTERTOUCH = 0xA0,
        CONTROL = 0xB0,
        PROGRAM = 0xC0,
        CHAN_AFTERTOUCH = 0xD0,
        PITCH_BEND = 0xE0,
        SYSEX_BEGIN = 0xF0,
        SYSEX_END = 0xF7
    };
};

struct Track
{
    std::vector<Event> events;

    Track() {}

    size_t size() const { return events.size(); }
    Event const* data() const { return events.data(); }
    Event * data() { return events.data(); }
    Event const& operator[](int i) const { return events[i]; }
    Event & operator[](int i) { return events[i]; }

    Track(Stream & src, std::vector<Tempo> & tempos, int track, bool notes_only=true)
    {
        ChunkHead head { src };
        head.chunk_type == "MTrk" || err("unsupported track: "+head.chunk_type);

        Stream midi { head.data, head.data + head.length };

        int program[16] = {0};
        size_t time = 0;
        size_t last_time = 0;
        int msg[3] = {0};
        int n_msg = 0;

        auto fill_msg = [&] (int n) {
            while(n_msg < n) { 
                int c = *midi.take(1);
                if(c < 128) msg[n_msg++] = c; 
            }
        };
        auto add_event = [&] (int type, int chan, int key, int val) {
            events.push_back({
                uint32_t(time-last_time),
                0, // duration
                uint8_t(chan < 16 ? program[chan] : 0),
                uint8_t(track),
                uint8_t(type),
                uint8_t(chan),
                uint8_t(key),
                uint8_t(val)
            });
            last_time = time;
        };

        while(midi.remain())
        {
            // track abs time to handle dropped events
            time += variable_int<uint32_t>(midi);

            int x = *midi.take(1);

            // if(x == 0xFF || x == 0xF0) { /* don't overwrite status */ }
            // else 
            if(x & 0x80) { msg[0] = x; n_msg = 1; }
            else if(n_msg == 1) { msg[1] = x; n_msg = 2; }
            else { err("missing status byte"); }
            
            int type = (msg[0] & 0xF0);
            int chan = (msg[0] & 0x0F);
            if( x == 0xF0 ) // SYSEX
            {
                while(*midi.take(1) != 0xF7) {}
            }
            else if( x == 0xFF ) // META
            {
                int mtype = *midi.take(1);
                uint32_t mlen = variable_int<uint32_t>(midi);
                uint8_t const* d = midi.take(mlen);
                if( mtype == 0x51 ) // SET_TEMPO
                {
                    // add_event(Event::SET_TEMPO, d[0], d[1], d[2]);
                    uint32_t usec_per_beat = (d[0]<<16)|(d[1]<<8)|d[2];
                    tempos.push_back({time, usec_per_beat});
                }
                else if( mtype == 0x2F ) // END_OF_TRACK
                {
                    break;
                }
            }
            else if( type == Event::NOTE_OFF ||
                type == Event::NOTE_ON )
            {
                fill_msg(3);
                if(type == Event::NOTE_ON && msg[2] == 0)
                {
                    type = Event::NOTE_OFF;
                }
                add_event(type, chan, msg[1], msg[2]);
                n_msg = 1; // keep status byte
            }
            else if( type == Event::POLY_AFTERTOUCH ||
                type == Event::CONTROL ||
                type == Event::PITCH_BEND )
            {
                fill_msg(3);
                if(!notes_only)
                    add_event(type, chan, msg[1], msg[2]);
                n_msg = 1; // keep status byte
            }
            else if( type == Event::CHAN_AFTERTOUCH )
            {
                fill_msg(2);
                if(!notes_only)
                    add_event(type, chan, 0, msg[1]);
                n_msg = 1; // keep status byte
            }
            else if( type == Event::PROGRAM )
            {
                fill_msg(2);
                program[chan] = msg[1];
                n_msg = 1; // keep status byte
            }
            else if( x == 0xF1 || x == 0xF3 )
            {
                // quarter note, song select
                fill_msg(2);
                n_msg = 1; // keep status byte
            }
            else if( x == 0xF2 )
            {
                // song position
                fill_msg(3);
                n_msg = 1; // keep status byte
            }
            else
            {
                // drop various system-common and real-time messages
                n_msg = 0;
            }
        }
    }

    Track & microseconds(uint32_t ticks_per_beat, std::vector<Tempo> const& tempos)
    {
        auto tempo_it = tempos.begin();
        auto tempo_end = tempos.end();
        uint32_t usec_per_beat = 500000;
        size_t usec = 0;
        size_t tick = 0;

        for(Event & e : events)
        {
            size_t pre_usec = usec;
            size_t ntick = tick + e.dt;

            while(tempo_it != tempo_end && tempo_it->tick <= ntick)
            {
                size_t dt = tempo_it->tick - tick;
                dt *= usec_per_beat;
                dt /= ticks_per_beat;
                usec += dt;
                tick = tempo_it->tick;
                usec_per_beat = tempo_it->usec_per_beat;
                tempo_it ++;
            }

            size_t dt = ntick - tick;
            dt *= usec_per_beat;
            dt /= ticks_per_beat;
            usec += dt;
            tick = ntick;
            e.dt = usec - pre_usec;
        }

        return *this;
    }

    Track & durations()
    {
        size_t time = 0;
        size_t off_time[16][128] = {{0}};
        for(int t=events.size()-1 ; t>=0 ; t--)
        {
            Event & e = events[t];
            
            if(e.type == Event::NOTE_ON)
                e.duration = time - off_time[e.channel][e.key];
            else if(e.type == Event::NOTE_OFF)
                off_time[e.channel][e.key] = time;
            
            time += e.dt;
        }
        return *this;
    }

    Track & remove_note_off()
    {
        events.erase(std::remove_if(
            events.begin(), events.end(),
            [] (Event const& e) { 
                return e.type == Event::NOTE_OFF; 
            }), 
            events.end()
        );
        return *this;
    }
};

struct File
{
    uint16_t type;
    uint16_t n_tracks;
    uint16_t ticks_per_beat;
    
    std::vector<Tempo> tempos;
    std::vector<Track> tracks;

    File(Stream & src, bool notes_only=true)
    {
        ChunkHead head { src };
        head.chunk_type == "MThd" || err("unsupported file: "+head.chunk_type);

        type = big_endian<uint16_t>(head.data+0);
        n_tracks = big_endian<uint16_t>(head.data+2);
        ticks_per_beat = big_endian<uint16_t>(head.data+4);

        for(int i=0 ; i<n_tracks ; i++)
            tracks.emplace_back(src, tempos, i, notes_only);

        check_tempos();
    }

    void check_tempos()
    {
        bool is_sorted = true;
        size_t t = 0;
        for(Tempo & temp : tempos)
        {
            if(temp.tick < t) 
                is_sorted = false;
            t = temp.tick;
        }
        if(!is_sorted)
        {
            std::sort(tempos.begin(), tempos.end(), 
                [] (Tempo const& a, Tempo const& b) {
                    return a.tick < b.tick;
                });
        }
    }

    File & microseconds()
    {
        for(Track & t : tracks)
            t.microseconds(ticks_per_beat, tempos);
        return *this;
    }

    File & durations()
    {
        for(Track & t : tracks)
            t.durations();
        return *this;
    }

    File & remove_note_off()
    {
        for(Track & t : tracks)
            t.remove_note_off();
        return *this;
    }

    File & merge_tracks()
    {
        Track out;
        std::vector<size_t> t_time(tracks.size());
        std::vector<size_t> t_pop(tracks.size());

        size_t n_events = 0;
        for(int i=0 ; i<tracks.size() ; i++)
        {
            if(tracks[i].size())
                t_time[i] = tracks[i][0].dt;
            n_events += tracks[i].size();
        }
        out.events.reserve(n_events);
        
        size_t time = 0;
        while(true)
        {
            int min_i = -1;
            for(int i=0 ; i<tracks.size() ; i++)
            {
                if(t_pop[i] >= tracks[i].size()) continue;
                if(min_i == -1 || t_time[i] < t_time[min_i]) min_i = i;
            }
            if(min_i < 0) break;

            Event e = tracks[min_i][t_pop[min_i]];
            e.dt = t_time[min_i] - time;
            out.events.push_back(e);
            
            time = t_time[min_i];
            t_pop[min_i] ++;

            if(t_pop[min_i] < tracks[min_i].size())
                t_time[min_i] += tracks[min_i][t_pop[min_i]].dt;
        }
        tracks = {out};
        return *this;
    }
};

} // namespace tensormidi