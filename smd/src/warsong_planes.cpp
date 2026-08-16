// The graphics container of Warsong / Langrisser (Mega Drive), reverse-
// engineered from the 68000 routine at ROM 0x53B4 and its two decoders,
// 0x53D8 and 0x54AC.
//
// A resource opens with a big-endian type word and the game dispatches on it:
//
//   type 1 -> a nibble RLE (0x53D8), streamed straight into the VDP port
//   type 2 -> a bitplane pack (0x54AC), reassembled to 4-bpp tiles through
//             an asl/roxl cascade on a 32-byte scratch (RAM 0xFF8F64)
//
// Both are decoded here under one codec name because that is the shape of
// the game's own data: the caller passes an index into the resource table at
// ROM 0x3BA00 and gets VDP-ready tiles back, whichever encoding the artist's
// tool chose for that picture. Of the 188 table entries 176 are type 2 and 11
// are type 1.
//
// Type-2 layout, exact:
//
//   +0   u16   type = 2
//   +2   u8    variant   bit 7 set -> a 16-entry colour remap follows count
//   +3   u8    count     the row WIDTH in bytes (the copy loop's d4 = count-1),
//                        and it also selects the plane count: 2 -> 2 planes,
//                        otherwise (count ^ 5), so 1 -> 4 planes
//  [+4   16 x u8         remap: decoded nibble -> palette index]
//   +N   u16   off       plane data starts at (address after off) + off
//   ...  flag bytes      one per (band, plane); bit 7 first, one bit per row
//   ...  plane data      the copied rows, `count` bytes each, in stream order
//
// The band loop (0x550E..0x55A6) runs while the flag cursor is below the data
// start: per plane one flag byte, then eight rows -- a 1 bit copies `count`
// bytes from the data cursor, a 0 bit writes zeros -- into the scratch, plane
// after plane. The interleave then reads the scratch as four plane words at
// +0x18, +0x08, +0x10, +0x00 and roxl's them together, so a pixel's nibble is
// (plane3 << 3) | (plane1 << 2) | (plane2 << 1) | plane0; the remap variant
// looks that nibble up in its table. Two nibbles a byte, VDP order.
//
// A resource may CHAIN: four table entries (#1, #121, #129, #180) carry a
// second `00 02 FF ..` header exactly where the first group's data ends, and
// the game decodes them back to back at consecutive VRAM addresses. This
// decoder returns ONE group and points `end` at the next header, so a caller
// with the table in hand loops until the resource's extent is spent -- the
// codec stays a format, not a Warsong-only routine that knows 0x3BA00.
//
// Verified two ways against the running game: the stream invariant (the flag
// walk ends exactly at the data start, the data walk exactly at the next
// resource, over all 176 + 4 chained entries), and byte-for-byte matches of
// decoded tiles against a live VRAM capture (#9: 225 of 225 tiles present,
// #123: 90 of 90, #127: 13 of 13, and the RLE-coded font #2: 189 of 192).
#include "ym/smd.hpp"

#include <algorithm>
#include <cstring>

namespace
{
	using namespace ym::smd;

	// ---------------------------------------------------------- type 1

	// 0x53D8. Header: count.b flag.b. flag == 0 -> raw copy of count*32 bytes.
	// Otherwise a nibble stream: a nibble equal to the previous one begins a
	// run whose next nibble is (extra repeats); after a run the "previous" is
	// reset so a following equal nibble is a literal again (0x5472).
	// `count` of 0 means 256 tiles. Output is exactly count*32 bytes.
	result<unpacked> rle_decode(const rom_image& rom, rom_offset at,
	                            std::size_t max_output)
	{
		const auto count_b = rom.u8(at);
		const auto flag = rom.u8(rom_offset{static_cast<std::uint32_t>(at.value + 1)});
		if (!count_b || !flag)
		{
			return errc::out_of_bounds;
		}
		const std::size_t count = count_b.value() ? count_b.value() : 256u;
		const std::size_t total = count * 32u;
		if (total > max_output)
		{
			return errc::output_limit;
		}
		std::size_t pos = at.value + 2;
		unpacked out;
		out.data.reserve(total);

		if (flag.value() == 0)
		{
			auto raw = rom.slice(rom_offset{static_cast<std::uint32_t>(pos)}, total);
			if (!raw)
			{
				return errc::corrupt_stream;
			}
			out.data.assign(raw.value().begin(), raw.value().end());
			out.end = rom_offset{static_cast<std::uint32_t>(pos + total)};
			return out;
		}

		bool high = true;
		auto nib = [&](std::uint8_t& v) -> bool {
			const auto b = rom.u8(rom_offset{static_cast<std::uint32_t>(pos)});
			if (!b)
			{
				return false;
			}
			v = high ? static_cast<std::uint8_t>(b.value() >> 4)
			         : static_cast<std::uint8_t>(b.value() & 0xF);
			if (high)
			{
				high = false;
			}
			else
			{
				high = true;
				++pos;
			}
			return true;
		};

		std::uint8_t prev = 0xFF;
		std::uint8_t acc = 0;
		bool half = false;
		auto emit = [&](std::uint8_t n) {
			acc = static_cast<std::uint8_t>((acc << 4) | (n & 0xF));
			half = !half;
			if (!half)
			{
				out.data.push_back(acc);
			}
		};
		while (out.data.size() < total)
		{
			std::uint8_t v;
			if (!nib(v))
			{
				return errc::corrupt_stream;
			}
			if (v == prev)
			{
				std::uint8_t n;
				if (!nib(n))
				{
					return errc::corrupt_stream;
				}
				for (unsigned i = 0; i <= n && out.data.size() < total; ++i)
				{
					emit(prev);
				}
				prev = 0xFF;
			}
			else
			{
				prev = v;
				emit(v);
			}
		}
		out.end = rom_offset{static_cast<std::uint32_t>(pos + (high ? 0u : 1u))};
		return out;
	}

	// ---------------------------------------------------------- type 2

	struct group_result
	{
		std::size_t data_end = 0; // first byte after the plane data
	};

	// One group. Appends tiles to `out`, returns where its data ended.
	result<group_result> planes_group(const rom_image& rom, std::size_t at,
	                                  std::size_t limit,
	                                  std::vector<std::uint8_t>& out,
	                                  std::size_t max_output)
	{
		const auto variant_r = rom.u8(rom_offset{static_cast<std::uint32_t>(at)});
		const auto count_r = rom.u8(rom_offset{static_cast<std::uint32_t>(at + 1)});
		if (!variant_r || !count_r)
		{
			return errc::out_of_bounds;
		}
		const std::uint8_t variant = variant_r.value();
		const unsigned count = count_r.value() ? count_r.value() : 256u;
		std::size_t p = at + 2;

		std::uint8_t remap[16];
		bool have_remap = false;
		if (variant & 0x80)
		{
			auto r = rom.slice(rom_offset{static_cast<std::uint32_t>(p)}, 16);
			if (!r)
			{
				return errc::corrupt_stream;
			}
			std::memcpy(remap, r.value().data(), 16);
			have_remap = true;
			p += 16;
		}
		const auto off = rom.u16(rom_offset{static_cast<std::uint32_t>(p)});
		if (!off)
		{
			return errc::corrupt_stream;
		}
		p += 2;
		const std::size_t data_start = p + off.value();
		if (data_start > limit)
		{
			return errc::corrupt_stream;
		}
		const unsigned nplanes = (count == 2) ? 2u : (count ^ 5u);
		if (nplanes == 0 || nplanes > 4)
		{
			return errc::corrupt_stream;
		}

		std::size_t f = p;          // flag cursor
		std::size_t d = data_start; // data cursor
		std::vector<std::uint8_t> plane[4];
		for (auto& pl : plane)
		{
			pl.assign(8u * count, 0);
		}

		while (f < data_start)
		{
			for (unsigned pi = 0; pi < 4; ++pi)
			{
				std::fill(plane[pi].begin(), plane[pi].end(), 0);
			}
			for (unsigned pi = 0; pi < nplanes; ++pi)
			{
				if (f >= data_start)
				{
					break;
				}
				const auto bits_r = rom.u8(rom_offset{static_cast<std::uint32_t>(f)});
				if (!bits_r)
				{
					return errc::corrupt_stream;
				}
				const std::uint8_t bits = bits_r.value();
				++f;
				for (unsigned row = 0; row < 8; ++row)
				{
					if (bits & (0x80u >> row))
					{
						auto src = rom.slice(rom_offset{static_cast<std::uint32_t>(d)}, count);
						if (!src || d + count > limit)
						{
							return errc::corrupt_stream;
						}
						std::memcpy(plane[pi].data() + row * count,
						            src.value().data(), count);
						d += count;
					}
				}
			}
			// Interleave: `count` 8x8 tiles, column c from byte c of each row.
			if (out.size() + 32u * count > max_output)
			{
				return errc::output_limit;
			}
			for (unsigned c = 0; c < count; ++c)
			{
				for (unsigned row = 0; row < 8; ++row)
				{
					const std::uint8_t p0 = plane[0][row * count + c];
					const std::uint8_t p1 = plane[1][row * count + c];
					const std::uint8_t p2 = plane[2][row * count + c];
					const std::uint8_t p3 = plane[3][row * count + c];
					std::uint8_t nibs[8];
					for (int bit = 7; bit >= 0; --bit)
					{
						std::uint8_t n = static_cast<std::uint8_t>(
							(((p3 >> bit) & 1) << 3) | (((p1 >> bit) & 1) << 2)
							| (((p2 >> bit) & 1) << 1) | ((p0 >> bit) & 1));
						if (have_remap)
						{
							n = static_cast<std::uint8_t>(remap[n] & 0xF);
						}
						nibs[7 - bit] = n;
					}
					for (int i = 0; i < 8; i += 2)
					{
						out.push_back(static_cast<std::uint8_t>((nibs[i] << 4) | nibs[i + 1]));
					}
				}
			}
		}
		return group_result{d};
	}
} // namespace

namespace ym::smd
{
	namespace detail
	{
		result<unpacked> warsong_decompress_stream(const rom_image& rom, rom_offset at,
		                                           std::size_t max_output)
		{
			const auto type = rom.u16(at);
			if (!type)
			{
				return errc::out_of_bounds;
			}
			if (type.value() == 1)
			{
				return rle_decode(rom, rom_offset{static_cast<std::uint32_t>(at.value + 2)}, max_output);
			}
			if (type.value() != 2)
			{
				return errc::corrupt_stream;
			}
			// ONE group. The four chained entries in the table (#1, #121,
			// #129, #180) are the CALLER's business: `end` lands exactly on
			// the next group's type word, so decoding a chain is a loop of
			// decompress(rom, previous.end) until the word there is not 2 --
			// and only the caller knows the table, i.e. where the resource
			// truly stops. Baking 0x3BA00 into a codec would make it a
			// Warsong-only routine rather than a format.
			unpacked out;
			auto g = planes_group(rom, at.value + 2, rom.size(), out.data, max_output);
			if (!g)
			{
				return g.error();
			}
			out.end = rom_offset{static_cast<std::uint32_t>(g.value().data_end)};
			return out;
		}

		result<unpacked> warsong_decompress_block(const rom_image& rom, rom_offset at,
		                                          std::size_t unpacked_size)
		{
			auto r = warsong_decompress_stream(rom, at, default_max_output);
			if (!r)
			{
				return r.error();
			}
			if (r.value().data.size() > unpacked_size)
			{
				return errc::corrupt_stream;
			}
			r.value().data.resize(unpacked_size, 0);
			return r;
		}
	}
}
