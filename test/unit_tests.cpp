// Unit tests for ym-smd: synthetic fixtures only, no ROM files required.
#include <ym/smd.hpp>

#include "lzss/LZSS.h" // internal compressor, used to round-trip the ancient decoder

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ym::smd;
using namespace ym::smd::literals;

static int failures = 0;

#define CHECK(cond)                                                                 \
	do                                                                              \
	{                                                                               \
		if (!(cond))                                                                \
		{                                                                           \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			++failures;                                                             \
		}                                                                           \
	} while (0)

static rom_image make_rom(std::vector<std::uint8_t> bytes)
{
	return rom_image::from_bytes(std::move(bytes));
}

static void test_rom_image_reads()
{
	const auto rom = make_rom({0x12, 0x34, 0x56, 0x78});

	CHECK(rom.size() == 4);
	CHECK(rom.u8(0x0_rom).value() == 0x12);
	CHECK(rom.u16(0x0_rom).value() == 0x1234);
	CHECK(rom.u16(0x2_rom).value() == 0x5678);
	CHECK(rom.u32(0x0_rom).value() == 0x12345678u);

	CHECK(!rom.u8(0x4_rom) && rom.u8(0x4_rom).error() == errc::out_of_bounds);
	CHECK(!rom.u16(0x3_rom));
	CHECK(!rom.u32(0x1_rom));

	CHECK(rom.slice(0x1_rom, 3).value().size() == 3);
	CHECK(rom.slice(0x1_rom, 3).value()[0] == 0x34);
	CHECK(!rom.slice(0x2_rom, 3));
	CHECK(rom.slice(0x4_rom, 0).value().empty()); // end-of-image, zero length is fine
}

static void test_cursor()
{
	const auto rom = make_rom({0x12, 0x34, 0x56, 0x78, 0x9A});

	cursor cur = rom.at(0x0_rom);
	CHECK(cur.u16() == 0x1234);
	CHECK(cur.remaining() == 3);
	CHECK(cur.position() == 0x2_rom);
	CHECK(cur.u32() == 0 && !cur.ok()); // only 3 bytes left: sticky failure
	CHECK(cur.remaining() == 0);
	CHECK(cur.u8() == 0 && !cur.ok()); // still failed

	cursor past = rom.at(0x10_rom); // out-of-range start
	CHECK(!past.ok() && past.remaining() == 0);

	cursor view = rom.at(0x1_rom);
	const auto bytes = view.bytes(2);
	CHECK(bytes.size() == 2 && bytes[0] == 0x34 && bytes[1] == 0x56);
	view.skip(2);
	CHECK(view.ok() && view.remaining() == 0);
	view.skip(1);
	CHECK(!view.ok());
}

static void test_virgin_literals_and_end()
{
	const auto rom = make_rom({0x83, 'A', 'B', 'C', 0x80});

	const auto res = decompress(rom, 0x0_rom, codec::virgin_lz);
	CHECK(res.has_value());
	CHECK(res.value().data == std::vector<std::uint8_t>({'A', 'B', 'C'}));
	CHECK(res.value().end == 0x5_rom);
}

static void test_virgin_rle()
{
	const auto rom = make_rom({0xFE, 0x04, 0x00, 0xAA, 0x80});

	const auto res = decompress(rom, 0x0_rom, codec::virgin_lz);
	CHECK(res.has_value());
	CHECK(res.value().data == std::vector<std::uint8_t>(4, 0xAA));
	CHECK(res.value().end == 0x5_rom);
}

static void test_virgin_short_backref()
{
	// "ABC" + back-reference offset 3, length 3 => "ABCABC"
	const auto rom = make_rom({0x83, 'A', 'B', 'C', 0x00, 0x03, 0x80});

	const auto res = decompress(rom, 0x0_rom, codec::virgin_lz);
	CHECK(res.has_value());
	CHECK(res.value().data == std::vector<std::uint8_t>({'A', 'B', 'C', 'A', 'B', 'C'}));
	CHECK(res.value().end == 0x7_rom);
}

static void test_virgin_overlapping_backref()
{
	// One literal 0x55, then offset 1 / length 6: LZ-style run replication.
	const auto rom = make_rom({0x81, 0x55, 0x30, 0x01, 0x80});

	const auto res = decompress(rom, 0x0_rom, codec::virgin_lz);
	CHECK(res.has_value());
	CHECK(res.value().data == std::vector<std::uint8_t>(7, 0x55));
}

static void test_virgin_absolute_copies()
{
	// "AB" + long copy (0xFF): length 2, absolute offset 0 => "ABAB"
	{
		const auto rom = make_rom({0x82, 'A', 'B', 0xFF, 0x02, 0x00, 0x00, 0x00, 0x80});
		const auto res = decompress(rom, 0x0_rom, codec::virgin_lz);
		CHECK(res.has_value());
		CHECK(res.value().data == std::vector<std::uint8_t>({'A', 'B', 'A', 'B'}));
	}
	// "ABC" + medium copy (0xC0): length 3, absolute offset 0 => "ABCABC"
	{
		const auto rom = make_rom({0x83, 'A', 'B', 'C', 0xC0, 0x00, 0x00, 0x80});
		const auto res = decompress(rom, 0x0_rom, codec::virgin_lz);
		CHECK(res.has_value());
		CHECK(res.value().data == std::vector<std::uint8_t>({'A', 'B', 'C', 'A', 'B', 'C'}));
	}
}

static void test_virgin_corrupt_streams()
{
	// Literal run promises 3 bytes, ROM ends after 2.
	CHECK(decompress(make_rom({0x83, 'A', 'B'}), 0x0_rom, codec::virgin_lz).error()
	      == errc::corrupt_stream);

	// No 0x80 terminator before the ROM ends.
	CHECK(decompress(make_rom({0x81, 'A'}), 0x0_rom, codec::virgin_lz).error()
	      == errc::corrupt_stream);

	// Back-reference before the start of the output.
	CHECK(decompress(make_rom({0x00, 0x05, 0x80}), 0x0_rom, codec::virgin_lz).error()
	      == errc::corrupt_stream);

	// Absolute reference beyond what has been written (growing mode).
	CHECK(decompress(make_rom({0x83, 'A', 'B', 'C', 0xC0, 0x05, 0x00, 0x80}), 0x0_rom,
	                 codec::virgin_lz).error()
	      == errc::corrupt_stream);

	// Offset past the end of the image.
	CHECK(decompress(make_rom({0x80}), 0x5_rom, codec::virgin_lz).error()
	      == errc::out_of_bounds);
}

static void test_virgin_output_limit()
{
	const auto rom = make_rom({0xFE, 0x00, 0x10, 0xAA, 0x80}); // 4096-byte RLE
	CHECK(decompress(rom, 0x0_rom, codec::virgin_lz, 16).error() == errc::output_limit);
}

static void test_virgin_sized_block()
{
	// Stream produces 3 bytes; caller asks for 8 => tail zero-filled.
	{
		const auto rom = make_rom({0x83, 'A', 'B', 'C', 0x80});
		const auto res = decompress(rom, 0x0_rom, 8, codec::virgin_lz);
		CHECK(res.has_value());
		CHECK(res.value().data == std::vector<std::uint8_t>({'A', 'B', 'C', 0, 0, 0, 0, 0}));
		CHECK(res.value().end == 0x5_rom);
	}
	// Stream produces 4 bytes; caller allows only 2 => corrupt_stream.
	{
		const auto rom = make_rom({0xFE, 0x04, 0x00, 0xAA, 0x80});
		CHECK(decompress(rom, 0x0_rom, 2, codec::virgin_lz).error() == errc::corrupt_stream);
	}
}

// Builds a ROM with a two-chunk table at 0: each chunk is one 32-byte tile.
static rom_image make_chunked_rom()
{
	std::vector<std::uint8_t> rom = {
		// chunk table: {u16 BE count, u32 BE offset} * 2 + u16 terminator
		0x00, 0x01, 0x00, 0x00, 0x00, 0x0E, // 1 tile at 0x0E
		0x80, 0x01, 0x00, 0x00, 0x00, 0x13, // 1 tile at 0x13 (bit 15 set: masked off)
		0x00, 0x00,
		// chunk 0 at 0x0E: RLE 32 x 0x11
		0xFE, 0x20, 0x00, 0x11, 0x80,
		// chunk 1 at 0x13: RLE 32 x 0x22
		0xFE, 0x20, 0x00, 0x22, 0x80,
	};
	return make_rom(std::move(rom));
}

static void test_chunk_table()
{
	const auto rom = make_chunked_rom();

	const auto chunks = read_chunk_table(rom, 0x0_rom);
	CHECK(chunks.has_value());
	CHECK(chunks.value().size() == 2);
	CHECK(chunks.value()[0].tiles == 1 && chunks.value()[0].at == 0xE_rom);
	CHECK(chunks.value()[1].tiles == 1 && chunks.value()[1].at == 0x13_rom); // bit 15 masked

	// Table with no terminator runs off the ROM.
	CHECK(read_chunk_table(make_rom({0x00, 0x01, 0x00, 0x00, 0x00, 0x0E}), 0x0_rom).error()
	      == errc::out_of_bounds);

	// Empty table.
	const auto empty = read_chunk_table(make_rom({0x00, 0x00}), 0x0_rom);
	CHECK(empty.has_value() && empty.value().empty());
}

static void test_decompress_chunked()
{
	const auto rom = make_chunked_rom();

	const auto res = decompress_chunked(rom, 0x0_rom);
	CHECK(res.has_value());
	CHECK(res.value().data.size() == 64);
	CHECK(res.value().data.size() % 32 == 0);
	CHECK(res.value().data[0] == 0x11 && res.value().data[31] == 0x11);
	CHECK(res.value().data[32] == 0x22 && res.value().data[63] == 0x22);
	CHECK(res.value().end == 0xE_rom); // first byte after the table terminator

	// Empty table decompresses to an empty resource.
	const auto empty = decompress_chunked(make_rom({0x00, 0x00}), 0x0_rom);
	CHECK(empty.has_value() && empty.value().data.empty());
	CHECK(empty.value().end == 0x2_rom);

	// Output cap applies to the whole table.
	CHECK(decompress_chunked(rom, 0x0_rom, codec::virgin_lz, 32).error() == errc::output_limit);
}

static void test_ancient_round_trip()
{
	// Compressible payload: text-ish repeats plus RLE-friendly runs.
	std::vector<std::uint8_t> payload;
	const std::string phrase = "the story of thor - a successor of the light. ";
	while (payload.size() < 1500)
	{
		payload.insert(payload.end(), phrase.begin(), phrase.end());
	}
	payload.insert(payload.end(), 300, 0x00);
	payload.insert(payload.end(), 77, 0x5A);

	std::vector<std::uint8_t> packed(LZSS_GetCompressedMaxSize(payload.size()));
	const std::size_t packed_size = LZSS_CompressSimple(payload.data(), payload.size(), packed.data());
	CHECK(packed_size > 0 && packed_size < payload.size());
	packed.resize(packed_size);

	// Embed at a nonzero offset to exercise addressing.
	std::vector<std::uint8_t> rom_bytes(0x10, 0xEE);
	rom_bytes.insert(rom_bytes.end(), packed.begin(), packed.end());
	const auto rom = make_rom(std::move(rom_bytes));

	const auto res = decompress(rom, 0x10_rom, codec::ancient_lzss);
	CHECK(res.has_value());
	CHECK(res.value().data == payload);
	CHECK(res.value().end.value > 0x10 && res.value().end.value <= 0x10 + packed_size);

	// Sized-block variant: exact size.
	const auto sized = decompress(rom, 0x10_rom, payload.size(), codec::ancient_lzss);
	CHECK(sized.has_value() && sized.value().data == payload);

	// Bigger size: zero-padded tail.
	const auto padded = decompress(rom, 0x10_rom, payload.size() + 8, codec::ancient_lzss);
	CHECK(padded.has_value());
	CHECK(padded.value().data.size() == payload.size() + 8);
	CHECK(std::memcmp(padded.value().data.data(), payload.data(), payload.size()) == 0);
	CHECK(padded.value().data.back() == 0);

	// Smaller size: the stream promises more than allowed.
	CHECK(decompress(rom, 0x10_rom, payload.size() - 1, codec::ancient_lzss).error()
	      == errc::corrupt_stream);

	// Output cap.
	CHECK(decompress(rom, 0x10_rom, codec::ancient_lzss, 100).error() == errc::output_limit);
}

static void test_ancient_corrupt()
{
	// Garbage that cannot be a valid stream: sizes point outside the data.
	const auto rom = make_rom({0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
	CHECK(!decompress(rom, 0x0_rom, codec::ancient_lzss).has_value());

	CHECK(decompress(rom, 0x9_rom, codec::ancient_lzss).error() == errc::out_of_bounds);
}

// Hand-crafted streams for the byte-oriented block format ("method 1",
// selected when the third stream byte is nonzero), which the compressor-based
// round trip never produces.
static void test_ancient_method1_blocks()
{
	// One block: literal "ABC", then LZ copy length 4 / offset 3 => "ABCABCA".
	{
		const auto rom = make_rom({0x08, 0x00, 0x03, 'A', 'B', 'C', 0x80, 0x03, 0x00});
		const auto res = decompress(rom, 0x0_rom, codec::ancient_lzss);
		CHECK(res.has_value());
		CHECK(res.value().data == std::vector<std::uint8_t>({'A', 'B', 'C', 'A', 'B', 'C', 'A'}));
		CHECK(res.value().end == 0x9_rom);
	}
	// One block: RLE (ctrl 0x42 => length 2 + 4) => six 0x55 bytes.
	{
		const auto rom = make_rom({0x04, 0x00, 0x42, 0x55, 0x00});
		const auto res = decompress(rom, 0x0_rom, codec::ancient_lzss);
		CHECK(res.has_value());
		CHECK(res.value().data == std::vector<std::uint8_t>(6, 0x55));
		CHECK(res.value().end == 0x5_rom);
	}
	// Two chained blocks: literal "AB", block marker, RLE 6 x 0x55.
	// (comp_size counts from the block start, including its own 2 bytes.)
	{
		const auto rom = make_rom({0x05, 0x00, 0x02, 'A', 'B', 0x01,
		                           0x04, 0x00, 0x42, 0x55, 0x00});
		const auto res = decompress(rom, 0x0_rom, codec::ancient_lzss);
		CHECK(res.has_value());
		std::vector<std::uint8_t> expected = {'A', 'B'};
		expected.insert(expected.end(), 6, 0x55);
		CHECK(res.value().data == expected);
		CHECK(res.value().end == 0xB_rom);
	}
	// Stream ending exactly at the image boundary with no block marker:
	// valid, and end must be rom.size(), not one past it.
	{
		const auto rom = make_rom({0x05, 0x00, 0x02, 'A', 'B'});
		const auto res = decompress(rom, 0x0_rom, codec::ancient_lzss);
		CHECK(res.has_value());
		CHECK(res.value().data == std::vector<std::uint8_t>({'A', 'B'}));
		CHECK(res.value().end == 0x5_rom);
	}
	// Chained block header truncated by the image end: must be corrupt_stream,
	// not an out-of-bounds read (regression test for the header re-read).
	{
		const auto rom = make_rom({0x04, 0x00, 0x01, 0xAA, 0x55, 0x77});
		CHECK(decompress(rom, 0x0_rom, codec::ancient_lzss).error() == errc::corrupt_stream);
	}
	// Literal run truncated by the image end: the size probe must reject it
	// just like the writing pass does (probe/decode symmetry).
	{
		CHECK(decompress(make_rom({0x22, 0x00, 0x1F}), 0x0_rom, codec::ancient_lzss).error()
		      == errc::corrupt_stream);
		CHECK(decompress(make_rom({0x06, 0x00, 0x03, 0xAA}), 0x0_rom, codec::ancient_lzss).error()
		      == errc::corrupt_stream);
	}
}

static void test_chunked_ancient()
{
	// Two 32-byte tiles, each compressed with the in-repo LZSS compressor,
	// addressed through a chunk table => generic per-chunk path.
	std::vector<std::uint8_t> tile_a(32), tile_b(32);
	for (std::size_t i = 0; i < 32; ++i)
	{
		tile_a[i] = static_cast<std::uint8_t>(i);
		tile_b[i] = static_cast<std::uint8_t>(0xF0 ^ i);
	}

	auto pack = [](const std::vector<std::uint8_t>& tile)
	{
		std::vector<std::uint8_t> packed(LZSS_GetCompressedMaxSize(tile.size()));
		packed.resize(LZSS_CompressSimple(tile.data(), tile.size(), packed.data()));
		return packed;
	};
	const auto packed_a = pack(tile_a);
	const auto packed_b = pack(tile_b);

	const auto offset_a = static_cast<std::uint32_t>(14);
	const auto offset_b = static_cast<std::uint32_t>(14 + packed_a.size());

	std::vector<std::uint8_t> rom_bytes = {
		0x00, 0x01, 0x00, 0x00, 0x00, static_cast<std::uint8_t>(offset_a),
		0x00, 0x01, 0x00, 0x00, 0x00, static_cast<std::uint8_t>(offset_b),
		0x00, 0x00,
	};
	rom_bytes.insert(rom_bytes.end(), packed_a.begin(), packed_a.end());
	rom_bytes.insert(rom_bytes.end(), packed_b.begin(), packed_b.end());
	const auto rom = make_rom(std::move(rom_bytes));

	const auto res = decompress_chunked(rom, 0x0_rom, codec::ancient_lzss);
	CHECK(res.has_value());
	CHECK(res.value().data.size() == 64);
	CHECK(std::memcmp(res.value().data.data(), tile_a.data(), 32) == 0);
	CHECK(std::memcmp(res.value().data.data() + 32, tile_b.data(), 32) == 0);
	CHECK(res.value().end == 0xE_rom);

	CHECK(decompress_chunked(rom, 0x0_rom, codec::ancient_lzss, 32).error()
	      == errc::output_limit);
}

static void test_virgin_zero_offset_backref()
{
	// Offset 0 reads the not-yet-written position: zeros in the preallocated
	// sized path (matching the original), corrupt in the growing path.
	const auto rom = make_rom({0x10, 0x00, 0x80});

	const auto sized = decompress(rom, 0x0_rom, 4, codec::virgin_lz);
	CHECK(sized.has_value());
	CHECK(sized.value().data == std::vector<std::uint8_t>(4, 0x00));

	CHECK(decompress(rom, 0x0_rom, codec::virgin_lz).error() == errc::corrupt_stream);
}

static void test_sized_rejects_absurd_size()
{
	const auto rom = make_rom({0x80});
	CHECK(decompress(rom, 0x0_rom, static_cast<std::size_t>(-1), codec::virgin_lz).error()
	      == errc::output_limit);
	CHECK(decompress(rom, 0x0_rom, static_cast<std::size_t>(-1), codec::ancient_lzss).error()
	      == errc::output_limit);
	CHECK(decompress(rom, 0x0_rom, static_cast<std::size_t>(-1), codec::reverse_lz).error()
	      == errc::output_limit);
}

static void test_reverse_lz_literals()
{
	// Hand-built block. Layout: [packed le16][payload][init bit buffer]
	// [init bit count][unpacked le16]; the first bits come from the trailer
	// buffer MSB-first and refills walk down from end-5.
	// 0xC0 = 1 (literal run) then 10 (two bits => length 2, +1 = 3).
	const auto rom = make_rom({0x07, 0x00, 'A', 'B', 'C', 0xC0, 0x03, 0x03, 0x00});

	const auto out = decompress(rom, 0x0_rom, codec::reverse_lz);
	CHECK(out.has_value());
	if (out)
	{
		CHECK(out.value().data == std::vector<std::uint8_t>({'A', 'B', 'C'}));
		CHECK(out.value().end.value == 0x9);   // blocks chain from here
	}
}

static void test_reverse_lz_match()
{
	// Literal "ABC" followed by a back-reference, which exercises the 8-bit
	// distance field, the selector the bit reader leaves behind, and a
	// mid-stream refill (the trailer buffer runs out part way through).
	const auto rom = make_rom({0x08, 0x00, 0x10, 'A', 'B', 'C', 0xD0, 0x08, 0x05, 0x00});

	const auto out = decompress(rom, 0x0_rom, codec::reverse_lz);
	CHECK(out.has_value());
	if (out)
	{
		CHECK(out.value().data == std::vector<std::uint8_t>({'A', 'B', 'A', 'B', 'C'}));
		CHECK(out.value().end.value == 0xA);
	}

	// Asking for more than the stream produces zero-fills the tail, matching
	// the other codecs' sized path.
	const auto padded = decompress(rom, 0x0_rom, 8, codec::reverse_lz);
	CHECK(padded.has_value());
	if (padded)
	{
		CHECK(padded.value().data.size() == 8);
		CHECK(padded.value().data[5] == 0);
	}

	// Asking for less than it produces is a contradiction, not a truncation.
	CHECK(decompress(rom, 0x0_rom, 3, codec::reverse_lz).error() == errc::corrupt_stream);

	// And the allocation cap is honoured before anything is decoded.
	CHECK(decompress(rom, 0x0_rom, codec::reverse_lz, 4).error() == errc::output_limit);
}

static void test_reverse_lz_rejects_garbage()
{
	// A length word that runs past the ROM is a malformed *stream*, not a bad
	// address: the caller's offset was perfectly valid. Probes that scan for
	// blocks rely on telling those two apart.
	const auto overrun = make_rom({0xFF, 0xFF, 0x00, 0x00});
	CHECK(decompress(overrun, 0x0_rom, codec::reverse_lz).error() == errc::corrupt_stream);

	// Too short to even hold the four trailer bytes.
	const auto stub = make_rom({0x02, 0x00, 0x00, 0x00});
	CHECK(decompress(stub, 0x0_rom, codec::reverse_lz).error() == errc::corrupt_stream);

	// Past the end of the ROM, on the other hand, is out_of_bounds.
	const auto tiny = make_rom({0x00, 0x00});
	CHECK(decompress(tiny, 0x10_rom, codec::reverse_lz).error() == errc::out_of_bounds);
}

static void test_load_roundtrip()
{
	namespace fs = std::filesystem;
	const auto dir = fs::temp_directory_path();

	const auto rom_path = dir / "ym_smd_unit_test_rom.bin";
	{
		std::ofstream out(rom_path, std::ios::binary | std::ios::trunc);
		const unsigned char bytes[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
		out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
	}
	const auto rom = rom_image::load(rom_path);
	CHECK(rom.has_value());
	CHECK(rom.value().size() == 5);
	CHECK(rom.value().u32(0x0_rom).value() == 0xDEADBEEFu);
	fs::remove(rom_path);

	const auto empty_path = dir / "ym_smd_unit_test_empty.bin";
	{
		std::ofstream out(empty_path, std::ios::binary | std::ios::trunc);
	}
	CHECK(rom_image::load(empty_path).error() == errc::empty_file);
	fs::remove(empty_path);
}

static void test_names_and_result()
{
	CHECK(to_string(codec::virgin_lz) == "virgin_lz");
	CHECK(to_string(codec::ancient_lzss) == "ancient_lzss");
	CHECK(to_string(codec::reverse_lz) == "reverse_lz");
	CHECK(to_string(errc::corrupt_stream) == "corrupt_stream");
	CHECK(to_string(errc::file_too_large) == "file_too_large");

	result<int> good(42);
	CHECK(std::move(good).value_or(7) == 42);
	result<int> bad(errc::out_of_bounds);
	CHECK(std::move(bad).value_or(7) == 7);
}

static void test_load_errors()
{
	CHECK(rom_image::load("no-such-file-here.bin").error() == errc::cannot_open_file);
}

int main()
{
	test_rom_image_reads();
	test_cursor();
	test_virgin_literals_and_end();
	test_virgin_rle();
	test_virgin_short_backref();
	test_virgin_overlapping_backref();
	test_virgin_absolute_copies();
	test_virgin_corrupt_streams();
	test_virgin_output_limit();
	test_virgin_sized_block();
	test_chunk_table();
	test_decompress_chunked();
	test_ancient_round_trip();
	test_ancient_corrupt();
	test_ancient_method1_blocks();
	test_chunked_ancient();
	test_virgin_zero_offset_backref();
	test_sized_rejects_absurd_size();
	test_load_roundtrip();
	test_reverse_lz_literals();
	test_reverse_lz_match();
	test_reverse_lz_rejects_garbage();
	test_names_and_result();
	test_load_errors();

	if (failures == 0)
	{
		std::printf("all unit tests passed\n");
		return 0;
	}
	std::printf("%d check(s) failed\n", failures);
	return 1;
}
