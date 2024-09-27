#include "ym_smd_io.hpp"
#include "ym_smd_compression.hpp"
#include <format>
#include <iostream>

#include <fstream>
#include <string>
#include <string_view>

constexpr auto houses_header = 0xA8B9A;
// constexpr auto houses_tiles_header = 0xC3AEC;
// constexpr auto houses_palette = 0xA6A18;

int main()
{
	std::ifstream file("dune_path.txt");
	std::string path;
	if (std::getline(file, path))
	{
		if (auto&& rom = ym::smd::io::read_data(path.c_str()))
		{
			using namespace std::literals;
			if (auto decompressor = ym::smd::create_data_decompressor("virgin"sv))
			{
				if (auto result = decompressor->decompress(rom, houses_header); !result.empty())
				{
					if (auto target = std::ofstream("decompressed.bin", std::ios::out | std::ios::binary | std::ios::trunc))
					{
						target.write(reinterpret_cast<const char*>(result.data()), result.size());
					}
				}
			}
		}
	}

	return 0;
}
