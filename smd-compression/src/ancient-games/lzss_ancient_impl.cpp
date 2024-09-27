#include "lzss_ancient_impl.hpp"

#include <cassert>
#include <numeric>

#include "ym_smd_io.hpp"

#include "lzss/LZSS.h"

namespace
{
	class LZSSAncientDecompressor_Impl : public ym::smd::IDataDecompressor
	{
	public:
		ym::smd::decompressed_data_t decompress(ym::smd::io::data_span_t in_data, uint32_t in_header_offset) override
		{
			ym::smd::decompressed_data_t out_data;

			const auto source = in_data->data() + in_header_offset;
			const auto max_data_size = in_data->size() - in_header_offset;

			if (const auto decompressed_data_size = 
					LZSS_Decompress(source, nullptr, max_data_size, nullptr); decompressed_data_size != static_cast<size_t>(-1))
			{
				out_data.resize(decompressed_data_size);

				size_t csize;
				LZSS_Decompress(source, out_data.data(), max_data_size, &csize);
			}

			return out_data;
		}

		ym::smd::decompressed_data_t decompress_with_size(ym::smd::io::data_span_t in_data, uint32_t in_offset, uint32_t in_data_size) override
		{
			assert(false && "Doesn't support");
			return {};
		}
	};

}

namespace ym::smd::ancient
{
	data_decompressor_t create_data_decompressor()
	{
		return std::make_shared<LZSSAncientDecompressor_Impl>();
	}
}
