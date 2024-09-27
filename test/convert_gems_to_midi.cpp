#include <fstream>
#include <iostream>

#include "tests.hpp"

#include "ym_smd_gems.hpp"

constexpr auto instrumets_offset = 0xFE2B6;
constexpr auto sequences_offset = 0xF6363;

int main()
{
	if (auto&& rom_data = ym::tests::load_rom_data())
	{
		if (auto&& bank = ym::gems::create_bank(rom_data))
		{
			bank->set_instruments_offset(instrumets_offset);
			bank->set_sequences_offset(sequences_offset);

			if (auto&& converter = ym::gems::create_midi_converter())
			{
				if (auto&& out_midi = std::ofstream("my_midi.mid"))
				{
					if (auto&& out_midi_info = std::ofstream("my_midi.txt"))
					{
						converter->convert_sequence(*bank, 0x1, out_midi, out_midi_info);
					}
				}
			}

			std::cout << bank->sequences_num();
		}
	}

	return 0;
}