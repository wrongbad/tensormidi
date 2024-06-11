#pragma once

#include <stdexcept>
#include <vector>
#include <string>
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

struct Mem
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
T variable_int(Mem & mem)
{
    T x = 0;
    for(int i=0 ; i<sizeof(T) ; i++)
    {
        uint8_t b = *mem.take(1);
        x = (x << 7) | (b & 0x7f);
        if(b < 0x80) return x;
    }
    return x;
}

struct Chunk
{
    std::string const chunk_type;
    uint32_t const length = 0;
    uint8_t const* data = nullptr;
    
    Chunk() {}
    Chunk(Mem & mem)
    :   chunk_type((char const*)mem.take(4), 4),
        length(big_endian<uint32_t>(mem.take(4))),
        data(mem.take(length))
    {
    }
};

struct File : Chunk
{
    uint16_t const type = big_endian<uint16_t>(data+0);
    uint16_t const n_tracks = big_endian<uint16_t>(data+2);
    uint16_t const ticks_per_beat = big_endian<uint16_t>(data+4);

    File(Mem & mem) : Chunk(mem)
    {
        chunk_type == "MThd" || err("unsupported file: "+chunk_type);
    }
};

struct Event
{
    uint32_t dtick;
    uint8_t type;
    uint8_t channel;
    uint8_t key;
    uint8_t value;

    enum Type
    {
        SET_TEMPO = 0x10,
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

struct Track : Chunk
{
    std::vector<Event> events;
    // size_t usec_per_beat = 500000;

    Track() {}

    Track(Mem & mem) : Chunk(mem)
    {
        chunk_type == "MTrk" || err("unsupported track: "+chunk_type);

        Mem midi {data, data+length};

        size_t time = 0;
        size_t last_time = 0;
        uint8_t msg[3] = {0};
        int n_msg = 0;
        while(midi.remain())
        {
            size_t dt = variable_int<uint32_t>(midi);
            // dt *= usec_per_beat;
            // dt /= ticks_per_beat;
            time += dt;

            uint8_t x = *midi.take(1);

            if(x == 0xFF || x == 0xF0) { /* don't overwrite status */ }
            else if(x & 0x80) { msg[0] = x; n_msg = 1; }
            else if(n_msg == 1) { msg[1] = x; n_msg = 2; }
            else { 
                // std::cout << "msg0 " << int(msg[0]) << std::endl;
                err("missing status byte");
            }
            
            uint8_t type = (msg[0] & 0xF0);
            uint8_t chan = (msg[0] & 0x0F);
            if( x == 0xF0 ) // SYSEX
            {
                while(*midi.take(1) != 0xF7) {}
                // n_msg = 0;
            }
            else if( x == 0xFF ) // META
            {
                uint8_t mtype = *midi.take(1);
                uint32_t mlen = variable_int<uint32_t>(midi);
                uint8_t const* d = midi.take(mlen);
                if( mtype == 0x51 ) // SET_TEMPO
                {
                    events.push_back({time-last_time, 
                        Event::SET_TEMPO, d[0], d[1], d[2]});
                    last_time = time;

                    // usec_per_beat = (d[0]<<16)|(d[1]<<8)|d[2];
                }
            }
            else if( type == Event::NOTE_OFF ||
                type == Event::NOTE_ON ||
                type == Event::POLY_AFTERTOUCH ||
                type == Event::CONTROL ||
                type == Event::PITCH_BEND )
            {
                // TODO check here for interrupt bytes
                while(n_msg < 3) { msg[n_msg++] = *midi.take(1); }
                if(type == Event::NOTE_ON && msg[2] == 0)
                {
                    type = Event::NOTE_OFF;
                }
                events.push_back({time-last_time, type, chan, msg[1], msg[2]});
                last_time = time;
                n_msg = 1; // keep status byte
            }
            else if( type == Event::PROGRAM ||
                     type == Event::CHAN_AFTERTOUCH )
            {
                if(n_msg < 2) { msg[n_msg] = *midi.take(1); }
                events.push_back({time-last_time, type, chan, 0, msg[1]});
                last_time = time;
                n_msg = 1; // keep status byte
            }
            else if( x == 0xF1 || x == 0xF3 )
            {
                if(n_msg < 2) { msg[n_msg] = *midi.take(1); }
                n_msg = 1; // keep status byte
            }
            else if( x == 0xF2 )
            {
                while(n_msg < 3) { msg[n_msg++] = *midi.take(1); }
                n_msg = 1; // keep status byte
            }
            else
            {
                // drop various system-common and real-time messages
                n_msg = 0;
            }
        }
    }
    size_t size() const { return events.size(); }
    Event const& operator[](int i) const { return events[i]; }
    Event & operator[](int i) { return events[i]; }
};

Track merge(std::vector<Track> const& tracks, size_t ticks_per_beat)
{
    size_t usec_per_beat = 500000;

    Track out;
    std::vector<size_t> t_time(tracks.size());
    std::vector<size_t> t_pop(tracks.size());

    size_t n_events = 0;
    for(int i=0 ; i<tracks.size() ; i++)
    {
        if(tracks[i].size())
            t_time[i] = tracks[i][0].dtick;
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
        // if(t_time[min_i] - time > 10000000)
        // {
        //     std::cout << e.dtick << " " << t_time[min_i] << " " << time << std::endl;

        //     err("dtick");
        // }
        if(e.type == Event::SET_TEMPO)
        {
            usec_per_beat = (e.channel<<16)|(e.key<<8)|(e.value);
        }
        else
        {
            size_t dt = t_time[min_i] - time;
            dt *= usec_per_beat;
            dt /= ticks_per_beat;
            e.dtick = dt;
            time = t_time[min_i];
            out.events.push_back(e);
        }    
        t_pop[min_i] ++;
        if(t_pop[min_i] < tracks[min_i].size())
            t_time[min_i] += tracks[min_i][t_pop[min_i]].dtick;
    }
    return out;
}

} // namespace tensormidi