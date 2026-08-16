// Ancient-era block LZSS (The Story of Thor / Beyond Oasis) plus the public
// decompress() entry points that dispatch between codecs.
//
// The LZSS bitstream decoder lives in lzss/LZSS.c (reverse-engineered
// reference code, kept in C). It is used in two passes: a probe pass with a
// null destination validates the stream against the source bounds and yields
// the decompressed size, then the real pass writes exactly that many bytes —
// which is what makes handing it a raw destination pointer safe.
#include "ym/smd.hpp"

#include "lzss/LZSS.h"

#include <algorithm>

namespace
{
	using namespace ym::smd;

	constexpr auto lzss_error = static_cast<std::size_t>(-1);

	struct lzss_probe
	{
		std::size_t unpacked_size;
		std::size_t max_src;
	};

	result<lzss_probe> probe(const rom_image& rom, rom_offset at)
	{
		if (at.value >= rom.size())
		{
			return errc::out_of_bounds;
		}
		const std::size_t max_src = rom.size() - at.value;
		const std::size_t size = LZSS_Decompress(rom.bytes().data() + at.value, nullptr, max_src, nullptr);
		if (size == lzss_error)
		{
			return errc::corrupt_stream;
		}
		return lzss_probe{size, max_src};
	}

	// Decode into a buffer that is already at least probe.unpacked_size big.
	result<rom_offset> decode(const rom_image& rom, rom_offset at, const lzss_probe& probe,
	                          std::uint8_t* destination)
	{
		std::size_t packed_size = 0;
		const std::size_t written =
			LZSS_Decompress(rom.bytes().data() + at.value, destination, probe.max_src, &packed_size);
		if (written != probe.unpacked_size)
		{
			// The writing pass must agree with the probe; anything else means
			// the stream is malformed in a way only one of the passes caught.
			return errc::corrupt_stream;
		}
		return at + static_cast<std::uint32_t>(std::min(packed_size, probe.max_src));
	}

	result<unpacked> lzss_decompress_stream(const rom_image& rom, rom_offset at,
	                                        std::size_t max_output)
	{
		auto probed = probe(rom, at);
		if (!probed)
		{
			return probed.error();
		}
		if (probed.value().unpacked_size > max_output)
		{
			return errc::output_limit;
		}

		unpacked res;
		res.data.resize(probed.value().unpacked_size);
		auto end = decode(rom, at, probed.value(), res.data.data());
		if (!end)
		{
			return end.error();
		}
		res.end = end.value();
		return res;
	}

	result<unpacked> lzss_decompress_block(const rom_image& rom, rom_offset at,
	                                       std::size_t unpacked_size)
	{
		auto probed = probe(rom, at);
		if (!probed)
		{
			return probed.error();
		}
		if (probed.value().unpacked_size > unpacked_size)
		{
			return errc::corrupt_stream; // stream promises more than the caller's size
		}

		unpacked res;
		res.data.resize(unpacked_size); // short stream leaves the tail zero-filled
		auto end = decode(rom, at, probed.value(), res.data.data());
		if (!end)
		{
			return end.error();
		}
		res.end = end.value();
		return res;
	}
}

namespace ym::smd
{
	namespace detail
	{
		// Implemented in virgin_lz.cpp.
		result<unpacked> virgin_decompress_stream(const rom_image&, rom_offset, std::size_t);
		result<unpacked> virgin_decompress_block(const rom_image&, rom_offset, std::size_t);
		result<unpacked> virgin_decompress_chunked(const rom_image&,
		                                           const std::vector<chunk_info>&,
		                                           rom_offset, std::size_t);

		// Implemented in reverse_lz.cpp.
		result<unpacked> reverse_lz_decompress_stream(const rom_image&, rom_offset, std::size_t);
		result<unpacked> reverse_lz_decompress_block(const rom_image&, rom_offset, std::size_t);

		// Implemented in warsong_planes.cpp.
		result<unpacked> warsong_decompress_stream(const rom_image&, rom_offset, std::size_t);
		result<unpacked> warsong_decompress_block(const rom_image&, rom_offset, std::size_t);
	}

	result<unpacked> decompress(const rom_image& rom, rom_offset at, codec format,
	                            std::size_t max_output)
	{
		if (at.value >= rom.size())
		{
			return errc::out_of_bounds;
		}
		switch (format)
		{
		case codec::virgin_lz:    return detail::virgin_decompress_stream(rom, at, max_output);
		case codec::ancient_lzss: return lzss_decompress_stream(rom, at, max_output);
		case codec::reverse_lz:   return detail::reverse_lz_decompress_stream(rom, at, max_output);
		case codec::warsong_planes: return detail::warsong_decompress_stream(rom, at, max_output);
		}
		return errc::corrupt_stream;
	}

	result<unpacked> decompress(const rom_image& rom, rom_offset at,
	                            std::size_t unpacked_size, codec format)
	{
		if (at.value >= rom.size())
		{
			return errc::out_of_bounds;
		}
		if (unpacked_size > default_max_output)
		{
			// An absurd size is garbage input, not a reason to attempt a
			// gigantic allocation (which could throw).
			return errc::output_limit;
		}
		switch (format)
		{
		case codec::virgin_lz:    return detail::virgin_decompress_block(rom, at, unpacked_size);
		case codec::ancient_lzss: return lzss_decompress_block(rom, at, unpacked_size);
		case codec::reverse_lz:   return detail::reverse_lz_decompress_block(rom, at, unpacked_size);
		case codec::warsong_planes: return detail::warsong_decompress_block(rom, at, unpacked_size);
		}
		return errc::corrupt_stream;
	}

	result<unpacked> decompress_chunked(const rom_image& rom, rom_offset table_at,
	                                    codec format, std::size_t max_output)
	{
		auto chunks = read_chunk_table(rom, table_at);
		if (!chunks)
		{
			return chunks.error();
		}
		const auto table_end = table_at
			+ static_cast<std::uint32_t>(chunks.value().size() * 6 + 2);

		if (format == codec::virgin_lz)
		{
			return detail::virgin_decompress_chunked(rom, chunks.value(), table_end, max_output);
		}

		// Generic path: decode each chunk as an exact-sized block into its slot.
		std::size_t total = 0;
		for (const chunk_info& chunk : chunks.value())
		{
			total += static_cast<std::size_t>(chunk.tiles) * 0x20;
		}
		if (total > max_output)
		{
			return errc::output_limit;
		}

		unpacked res;
		res.data.reserve(total);
		res.end = table_end;
		for (const chunk_info& chunk : chunks.value())
		{
			auto block = decompress(rom, chunk.at, static_cast<std::size_t>(chunk.tiles) * 0x20, format);
			if (!block)
			{
				return block.error();
			}
			res.data.insert(res.data.end(), block.value().data.begin(), block.value().data.end());
		}
		return res;
	}
}
