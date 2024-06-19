#include <iostream>
#include <fstream>
#include <sstream>

#include "tensormidi/tensormidi.h"

using namespace tensormidi;

// This just parses the given file and prints any parse errors encountered
// Can be used with find | xargs to discover parse errors on big datasets
// Can also be used with gdb to drill deeply into any problematic files

int main(int argc, char ** argv)
{
    std::string fname = argv[1];
    
    std::stringstream iss;
    iss << std::ifstream(fname, std::ios::binary).rdbuf();

    std::string data_str = iss.str();
    uint8_t const* raw = (uint8_t const*)data_str.data();

    Stream src {raw, raw+data_str.size()};

    try
    {
        File midi { src };
        midi.merge_tracks();
    }
    catch(std::exception const& e)
    {
        std::cout << fname << std::endl;
        std::cout << e.what() << std::endl;
    }
}