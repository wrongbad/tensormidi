#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/ndarray.h>

#include <fstream>
#include <sstream>

#include "tensormidi/tensormidi.h"

namespace nb = nanobind;
namespace midi = tensormidi;

std::tuple<
    std::vector<nb::ndarray<nb::numpy, uint8_t>>, // tracks
    nb::ndarray<nb::numpy, uint32_t>, // tempos
    uint32_t // ticks_per_beat
>
load_midi(
    std::string filename, 
    bool merge_tracks, 
    bool microseconds, 
    bool notes_only, 
    bool durations,
    bool remove_note_off )
{
    std::stringstream iss;
    iss << std::ifstream(filename, std::ios::binary).rdbuf();
    std::string data_str = iss.str();

    uint8_t const* raw = (uint8_t const*)data_str.data();
    midi::Stream src {raw, raw+data_str.size()};

    midi::File f { src, notes_only };

    if(merge_tracks) f.merge_tracks();
    if(microseconds) f.microseconds();
    if(durations) f.durations();
    if(remove_note_off) f.remove_note_off();

    using TrackList = std::vector<midi::Track>;
    TrackList * buf = new TrackList(std::move(f.tracks));
    nb::capsule deleter(buf, [] (void *p) noexcept {
        delete (TrackList *) p;
    });

    std::vector<nb::ndarray<nb::numpy, uint8_t>> out;
    for(int i=0 ; i<buf->size() ; i++)
    {
        midi::Track & t = (*buf)[i];
        out.push_back(
            nb::ndarray<nb::numpy, uint8_t>(
                reinterpret_cast<uint8_t*>(t.data()),
                { t.size(), sizeof(midi::Event) },
                deleter
            )
        );
    }

    nb::ndarray<nb::numpy, uint32_t> tempos {nullptr, {0, 2}, nb::handle()};
    if(!microseconds)
    {
        using TempoList = std::vector<midi::Tempo>;
        TempoList * buf = new TempoList(std::move(f.tempos));
        nb::capsule deleter(buf, [] (void *p) noexcept {
            delete (TempoList *) p;
        });
        tempos = nb::ndarray<nb::numpy, uint32_t>(
            reinterpret_cast<uint32_t*>(buf->data()),
            { buf->size(), 2 },
            deleter
        );
    }

    return {out, tempos, f.ticks_per_beat};
}

NB_MODULE(tensormidi_bind, m)
{
    using namespace nanobind::literals;

    m.def("load_midi", &load_midi, 
        "filename"_a,
        "merge_tracks"_a = true,
        "microseconds"_a = true,
        "notes_only"_a = true,
        "durations"_a = false,
        "remove_note_off"_a = false
    );
}