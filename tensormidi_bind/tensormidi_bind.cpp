#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/ndarray.h>

#include <fstream>
#include <sstream>

#include "tensormidi/tensormidi.h"

namespace nb = nanobind;
using namespace nb::literals;
using namespace tensormidi;


nb::ndarray<nb::numpy, uint8_t>
load_merge(std::string filename)
{
    std::stringstream iss;
    iss << std::ifstream(filename, std::ios::binary).rdbuf();
    std::string data_str = iss.str();
    uint8_t const* raw = (uint8_t const*)data_str.data();
    Mem mem {raw, raw+data_str.size()};

    File head { mem };
    std::vector<Track> tracks;
    for(int i=0 ; i<head.n_tracks ; i++)
    {
        tracks.emplace_back(mem);
    }
    Track m = merge(tracks, head.ticks_per_beat);

    std::vector<Event> * buf = new std::vector<Event>(std::move(m.events));
    nb::capsule deleter(buf, [] (void *p) noexcept {
        delete (std::vector<Event> *) p;
    });

    return nb::ndarray<nb::numpy, uint8_t>(
        reinterpret_cast<uint8_t*>(buf->data()),
        { buf->size(), 8 },
        deleter
    );
}


NB_MODULE(tensormidi_bind, m)
{
    m.def("load_merge", &load_merge, "filename"_a);
}