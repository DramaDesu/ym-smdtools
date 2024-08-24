#include "include/ym_smd_compression.hpp"

#include "src/virgin-games/dune_impl.hpp"

#include <string>

namespace 
{
	using namespace std::literals;

	constexpr auto ANCIENT_GAMES = "ancient"sv;
	constexpr auto VIRGIN_GAMES = "virgin"sv;

	ym::smd::supported_games_t supported_games = { ANCIENT_GAMES, VIRGIN_GAMES };
}

namespace ym::smd
{
	supported_games_t get_supported_games() { return supported_games; }

	data_decompressor_t create_data_decompressor(std::string_view in_games_set)
	{
		if (in_games_set == VIRGIN_GAMES)
		{
			return 	ym::smd::virgin::create_data_decompressor();
		}
		return nullptr;
	}
}
