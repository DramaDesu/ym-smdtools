// Virgin/Westwood LZ decoder (Dune II, Sega Mega Drive), hand-ported from the
// original 68k routine. The command layout is preserved exactly; what this
// port adds over the 68k original is bounds checking on every source read,
// output write and back-reference, so no input can make it touch memory it
// does not own.
#include "ym/smd.hpp"

namespace
{
	using namespace ym::smd;

	// Where decompressed bytes go. Two modes mirror the two entry points:
	//  - growing: output size is unknown, the vector grows up to `cap`;
	//    back-references may only read what has already been written.
	//  - fixed: the buffer is preallocated (and zero-filled) to its final
	//    size; back-references may read anywhere inside it, matching the
	//    original game's behaviour of decoding into a zeroed buffer.
	struct sink
	{
		std::vector<std::uint8_t>& out;
		std::size_t w;    // write index
		std::size_t base; // start of the current stream's output (absolute refs)
		std::size_t cap;  // hard output limit
		bool fixed;

		bool put(std::uint8_t byte)
		{
			if (w >= cap)
			{
				return false;
			}
			if (w < out.size())
			{
				out[w] = byte;
			}
			else
			{
				out.push_back(byte);
			}
			++w;
			return true;
		}

		// Reads for back-references. In fixed mode any index inside the
		// buffer is readable (unwritten bytes are zeros); in growing mode
		// only already-written bytes exist.
		bool readable(std::size_t index) const
		{
			return fixed ? index < out.size() : index < w;
		}
	};

	// Decode one stream at rom[at...]; returns the index one past the 0x80
	// terminator, or an errc. errc::output_limit only in growing mode;
	// overrunning a fixed buffer is corrupt_stream (the stream promises more
	// data than the caller's size allows).
	result<std::size_t> virgin_decode(std::span<const std::uint8_t> rom,
	                                  std::size_t at, sink& dst)
	{
		std::size_t src = at;

		const auto src_has = [&](std::size_t count)
		{
			return count <= rom.size() - src;
		};

		if (src >= rom.size())
		{
			return errc::out_of_bounds;
		}

		for (;;)
		{
			if (!src_has(1))
			{
				return errc::corrupt_stream; // ran off the ROM before the 0x80 terminator
			}
			const std::uint8_t header = rom[src++];

			std::size_t copy_size = 0;
			std::size_t from = 0; // absolute index into dst.out

			if ((header & 0x80) == 0)
			{
				// Short back-reference: 12-bit offset relative to the write
				// position, length 3..10 from the top nibble.
				if (!src_has(1))
				{
					return errc::corrupt_stream;
				}
				const std::size_t offset = ((static_cast<std::size_t>(header) << 8) & 0xF00) | rom[src++];
				copy_size = (header >> 4) + 3;

				// offset == 0 reads the not-yet-written position: defined
				// (zeros) in a preallocated buffer, exactly like the 68k
				// original decoding into zeroed memory; meaningless when the
				// output size is still unknown.
				if (offset > dst.w || (offset == 0 && !dst.fixed))
				{
					return errc::corrupt_stream; // would read before the buffer
				}
				from = dst.w - offset;
			}
			else if ((header & 0x40) == 0)
			{
				if (header == 0x80)
				{
					return src; // stream terminator
				}
				// Literal run straight from the ROM.
				std::size_t count = header & 0x3F;
				if (!src_has(count))
				{
					return errc::corrupt_stream;
				}
				while (count-- > 0)
				{
					if (!dst.put(rom[src++]))
					{
						return dst.fixed ? errc::corrupt_stream : errc::output_limit;
					}
				}
				continue;
			}
			else if (header == 0xFE)
			{
				// RLE fill: little-endian u16 count, then the fill byte.
				if (!src_has(3))
				{
					return errc::corrupt_stream;
				}
				std::size_t count = static_cast<std::size_t>(rom[src]) | static_cast<std::size_t>(rom[src + 1]) << 8;
				const std::uint8_t fill = rom[src + 2];
				src += 3;
				while (count-- > 0)
				{
					if (!dst.put(fill))
					{
						return dst.fixed ? errc::corrupt_stream : errc::output_limit;
					}
				}
				continue;
			}
			else if (header == 0xFF)
			{
				// Long copy: little-endian u16 length, then little-endian u16
				// offset absolute from the start of this stream's output.
				if (!src_has(4))
				{
					return errc::corrupt_stream;
				}
				copy_size = static_cast<std::size_t>(rom[src]) | static_cast<std::size_t>(rom[src + 1]) << 8;
				const std::size_t offset = static_cast<std::size_t>(rom[src + 2]) | static_cast<std::size_t>(rom[src + 3]) << 8;
				src += 4;
				from = dst.base + offset;
			}
			else
			{
				// Medium copy: length 3..66, little-endian u16 offset absolute
				// from the start of this stream's output.
				if (!src_has(2))
				{
					return errc::corrupt_stream;
				}
				copy_size = static_cast<std::size_t>(header & 0x3F) + 3;
				const std::size_t offset = static_cast<std::size_t>(rom[src]) | static_cast<std::size_t>(rom[src + 1]) << 8;
				src += 2;
				from = dst.base + offset;
			}

			// Byte-by-byte copy, exactly like the 68k original: overlapping
			// ranges deliberately replicate the bytes being written.
			while (copy_size-- > 0)
			{
				if (!dst.readable(from))
				{
					return errc::corrupt_stream;
				}
				if (!dst.put(dst.out[from++]))
				{
					return dst.fixed ? errc::corrupt_stream : errc::output_limit;
				}
			}
		}
	}

	constexpr std::size_t bytes_per_tile = 0x20;
}

namespace ym::smd
{
	namespace detail
	{
		// Entry points shared with ancient_lzss.cpp via decompress() dispatch.
		result<unpacked> virgin_decompress_stream(const rom_image& rom, rom_offset at,
		                                          std::size_t max_output)
		{
			unpacked res;
			sink dst{res.data, 0, 0, max_output, /*fixed*/ false};

			auto end = virgin_decode(rom.bytes(), at.value, dst);
			if (!end)
			{
				return end.error();
			}
			res.end = rom_offset{static_cast<std::uint32_t>(end.value())};
			return res;
		}

		result<unpacked> virgin_decompress_block(const rom_image& rom, rom_offset at,
		                                         std::size_t unpacked_size)
		{
			unpacked res;
			res.data.resize(unpacked_size); // zero-filled; a short stream leaves the tail as zeros
			sink dst{res.data, 0, 0, unpacked_size, /*fixed*/ true};

			auto end = virgin_decode(rom.bytes(), at.value, dst);
			if (!end)
			{
				return end.error();
			}
			res.end = rom_offset{static_cast<std::uint32_t>(end.value())};
			return res;
		}

		result<unpacked> virgin_decompress_chunked(const rom_image& rom,
		                                           const std::vector<chunk_info>& chunks,
		                                           rom_offset table_end,
		                                           std::size_t max_output)
		{
			std::size_t total = 0;
			for (const chunk_info& chunk : chunks)
			{
				total += static_cast<std::size_t>(chunk.tiles) * bytes_per_tile;
			}
			if (total > max_output)
			{
				return errc::output_limit;
			}

			unpacked res;
			res.data.resize(total); // zero-filled, decoded in place per chunk slot
			res.end = table_end;

			std::size_t slot = 0;
			for (const chunk_info& chunk : chunks)
			{
				// Each chunk decodes into its own slot; absolute references
				// inside the stream are relative to the slot start, exactly
				// as the original passes a per-chunk destination pointer.
				sink dst{res.data, slot, slot, total, /*fixed*/ true};
				if (auto end = virgin_decode(rom.bytes(), chunk.at.value, dst); !end)
				{
					return end.error();
				}
				slot += static_cast<std::size_t>(chunk.tiles) * bytes_per_tile;
			}
			return res;
		}
	}

	result<std::vector<chunk_info>> read_chunk_table(const rom_image& rom, rom_offset table_at)
	{
		std::vector<chunk_info> chunks;

		cursor cur = rom.at(table_at);
		for (;;)
		{
			const std::uint16_t raw = cur.u16();
			if (!cur.ok())
			{
				return errc::out_of_bounds; // ran off the ROM before the terminator
			}
			if ((raw & 0x7FFF) == 0)
			{
				return chunks;
			}
			const std::uint32_t offset = cur.u32();
			if (!cur.ok())
			{
				return errc::out_of_bounds;
			}
			chunks.push_back(chunk_info{static_cast<std::uint16_t>(raw & 0x7FFF), rom_offset{offset}});
		}
	}
}
