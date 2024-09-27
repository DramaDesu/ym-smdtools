#include "dune_impl.hpp"

#include <numeric>

#include "ym_smd_io.hpp"

namespace 
{
	class DuneDecompressor_Impl : public ym::smd::IDataDecompressor
	{
		struct data_header_chunk_t : std::tuple<std::uint16_t, std::uint32_t>
		{
			data_header_chunk_t(std::uint16_t in_size, std::uint32_t in_offset) : std::tuple<std::uint16_t, std::uint32_t>(in_size, in_offset) {}

			std::uint16_t size() const { return std::get<0>(*this); }
			std::uint32_t offset() const { return std::get<1>(*this); }
		};

		struct data_header_t
		{
			bool is_valid() const { return !chunks.empty(); }
			void add(std::uint16_t in_size, std::uint32_t in_offset) { chunks.emplace_back(in_size, in_offset); }

			std::vector<data_header_chunk_t> chunks;
		};

		ym::smd::decompressed_data_t decompress(const uint8_t* in_data, uint32_t in_header_offset) override
		{
			ym::smd::decompressed_data_t out_data;

			data_header_t header;
			if (auto reader = ym::smd::io::create_rom_reader(in_data, in_header_offset)) [[likely]]
			{
				while (const std::uint16_t size = reader->read<std::uint16_t>() & 0x7FFF)
				{
					const auto offset = reader->read<std::uint32_t>();
					header.add(size, offset);
				}
			}

			if (header.is_valid()) [[likely]]
			{
				const auto total_data_size = std::accumulate(header.chunks.cbegin(), header.chunks.cend(), 0, [](auto&& value, const data_header_chunk_t& chunk)
				{
					return value + chunk.size();
				});

				constexpr size_t data_chunk_size = 0x20;
				out_data.resize(total_data_size * data_chunk_size);

				auto current_out_data = out_data.data();
				for (auto&& chunk : header.chunks)
				{
					decompress_internal(in_data + chunk.offset(), current_out_data);
					current_out_data += chunk.size() * data_chunk_size;
				}
			}

			return out_data;
		}

		ym::smd::decompressed_data_t decompress_with_size(const uint8_t* in_data, uint32_t in_data_size) override
		{
            ym::smd::decompressed_data_t out_data(in_data_size);

            decompress_internal(in_data, out_data.data());

            return out_data;
		}

		static void decompress_internal(const uint8_t* in_source, uint8_t* const in_destination)
		{
            unsigned char* const destination = in_destination; // A1

            const unsigned char* current_source = in_source; // A2
            unsigned char* current_destination = destination; // A3

            const unsigned char* target_source = destination; // A4

            while (true)
            {
                const unsigned char current_header = *current_source++; // d1

                size_t copy_data_size = 0; // d1

                // GetAddressFromRam
                if ((current_header & 0x80) == 0) // More than zero
                {
                    unsigned int offset = current_header << 8; // d2
                    offset &= 0xFFF;
                    const unsigned char header_offset = *current_source++;

                    offset &= 0xFFFFFF00;
                    offset |= static_cast<unsigned int>(header_offset);

                    copy_data_size = (current_header >> 0x4) + 0x3;

                    target_source = current_destination - offset;
                }
                else if ((current_header & (1 << 6)) == 0) // 6th bit is 0
                {
                    if (current_header == 0x80)
                    {
                        break;
                    }

                    copy_data_size = current_header & 0x3F;

                    target_source = current_source;
                    current_source += copy_data_size;
                }
                else if (current_header == static_cast<unsigned char>(0xFE))
                {
                    unsigned short clean_data_size = *(current_source + 1); // d1
                    clean_data_size <<= 8;
                    clean_data_size += *current_source;

                    const unsigned char clean_data = *(current_source + 2);

                    current_source += 0x3;

                    while (clean_data_size > 0)
                    {
                        *current_destination++ = clean_data;
                        clean_data_size--;
                    }
                }
                else if (current_header == static_cast<unsigned char>(0xFF))
                {
                    copy_data_size = *(current_source + 1); // d1
                    copy_data_size <<= 8;
                    copy_data_size += *current_source;

                    unsigned short offset = *(current_source + 3); // d2
                    offset <<= 8;
                    offset += *(current_source + 2);

                    current_source += 0x4;
                    target_source = destination + offset;
                }
                else
                {
                    copy_data_size = current_header & 0x3F;

                    unsigned short offset = *(current_source + 1); // d2
                    offset <<= 8;
                    offset += *current_source;

                    current_source += 0x2;
                    copy_data_size += 0x3;

                    target_source = destination + offset;
                }

                while (copy_data_size > 0)
                {
                    *current_destination++ = *target_source++;
                    copy_data_size--;
                }
            }
		}
	};
}

namespace ym::smd::virgin
{
	data_decompressor_t create_data_decompressor()
	{
		return std::make_shared<DuneDecompressor_Impl>();
	}
}
