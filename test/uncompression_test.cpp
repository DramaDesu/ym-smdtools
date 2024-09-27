#include "ym_smd_io.hpp"
#include "ym_smd_compression.hpp"
#include <format>

#include <fstream>
#include <string>

constexpr auto houses_header = 0xA8B9A;
constexpr auto key_ui_header = 0x15457A;
constexpr auto health_ui_header = 0x1545C8;

void test_game_decompressor(const char* path_file, const char* game, std::uint32_t offset)
{
	if (auto&& path = ym::smd::io::read_string(path_file); !path.empty())
	{
		if (auto&& rom = ym::smd::io::read_data(path.c_str()))
		{
			using namespace std::literals;
			if (auto decompressor = ym::smd::create_data_decompressor(game))
			{
				if (auto result = decompressor->decompress(rom, offset); !result.empty())
				{
					if (auto target = std::ofstream(std::format("decompressed_{}.bin", game), std::ios::out | std::ios::binary | std::ios::trunc))
					{
						target.write(reinterpret_cast<const char*>(result.data()), result.size());
					}
				}
			}
		}
	}
}

int main()
{
	test_game_decompressor("dune_path.txt", "virgin", houses_header);
	test_game_decompressor("oasis_path.txt", "ancient", health_ui_header);

	return 0;
}
