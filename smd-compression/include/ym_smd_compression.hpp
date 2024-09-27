#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "ym_smd_io.hpp"

namespace ym::smd
{
	class IDataDecompressor;

	using supported_games_t = const std::vector<std::string_view>&;
	using decompressed_data_t = std::vector<uint8_t>;
	using data_decompressor_t = std::shared_ptr<IDataDecompressor>;

	class IDataDecompressor
	{
	public:
		virtual ~IDataDecompressor() = default;
		virtual decompressed_data_t decompress(io::data_span_t in_data, uint32_t in_header_offset) = 0;
		virtual decompressed_data_t decompress_with_size(io::data_span_t in_data, uint32_t in_offset, uint32_t in_data_size) = 0;
	};

	supported_games_t get_supported_games();
	data_decompressor_t create_data_decompressor(std::string_view in_games_set);
}
