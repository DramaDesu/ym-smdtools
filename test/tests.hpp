#pragma once
#include <string>
#include "ym_smd_io.hpp"

namespace ym::tests
{
	inline std::string get_rom_path();

	inline ym::smd::io::data_span_t load_rom_data()
	{
		return ym::smd::io::read_data(get_rom_path().c_str());
	}

	inline std::string get_rom_path()
	{
		return ym::smd::io::read_string("dune_path.txt");
	}

}