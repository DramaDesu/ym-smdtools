// ============================================================================
// ym/smd.hpp — Sega Mega Drive ROM asset extraction.
//
// The single public header of ym-smdtools.
//
//     auto rom = ym::smd::rom_image::load("dune2.gen");        // whole file, binary
//     auto gfx = ym::smd::decompress_chunked(rom.value(), 0xA8B9A_rom);
//     auto pal = rom.value().u16(0xF0C22_rom);                 // big-endian, checked
//
// Contract, in one line: every operation either fully succeeds or reports an
// errc; nothing here throws (allocation failure aside), nothing reads or
// writes out of bounds, whatever the input bytes are, and everything taking
// `const rom_image&` is safe to call concurrently from any number of threads.
//
// All multi-byte ROM reads are big-endian, because the target CPU is a
// Motorola 68000. There is no byte order to configure and none to get wrong.
// (Values embedded inside compressed streams are the codecs' own business.)
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ym::smd
{
	// ----------------------------------------------------------- errors

	/// Every failure this library can report. Deliberately small: most
	/// operations are total and cannot fail at all.
	enum class errc : std::uint8_t
	{
		cannot_open_file, ///< load: path missing or unreadable
		empty_file,       ///< load: file exists but holds no bytes
		file_too_large,   ///< load: far bigger than any Mega Drive ROM (64 MiB cap)
		out_of_bounds,    ///< requested offset/extent lies outside the ROM
		corrupt_stream,   ///< compressed data is malformed or self-inconsistent
		output_limit,     ///< decompression would exceed the given output cap
	};

	/// Stable lowercase name ("out_of_bounds", ...) for logs.
	[[nodiscard]] std::string_view to_string(errc) noexcept;

	/// Success-or-errc carrier — the C++20 stand-in for std::expected<T, errc>.
	/// The member surface is the common subset, so moving to C++23 is a
	/// one-line alias change with no call-site churn.
	template <class T>
	class [[nodiscard]] result
	{
	public:
		result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
		result(errc error) noexcept : storage_(std::in_place_index<1>, error) {}

		[[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
		explicit operator bool() const noexcept { return has_value(); }

		/// Preconditions: has_value() for value(), !has_value() for error().
		[[nodiscard]] T&       value() &       { return std::get<0>(storage_); }
		[[nodiscard]] const T& value() const & { return std::get<0>(storage_); }
		[[nodiscard]] T&&      value() &&      { return std::get<0>(std::move(storage_)); }
		[[nodiscard]] errc     error() const   { return std::get<1>(storage_); }

		[[nodiscard]] T value_or(T fallback) &&
		{
			return has_value() ? std::get<0>(std::move(storage_)) : std::move(fallback);
		}

	private:
		std::variant<T, errc> storage_;
	};

	// ------------------------------------------------------- rom_offset

	/// A position inside a ROM image. A distinct type so ROM addresses,
	/// byte counts and tile counts cannot be swapped silently at call sites.
	struct rom_offset
	{
		std::uint32_t value = 0;

		constexpr rom_offset() = default;
		explicit constexpr rom_offset(std::uint32_t v) noexcept : value(v) {}

		friend constexpr bool operator==(rom_offset, rom_offset) noexcept = default;
		friend constexpr auto operator<=>(rom_offset, rom_offset) noexcept = default;

		friend constexpr rom_offset operator+(rom_offset at, std::uint32_t delta) noexcept
		{
			return rom_offset{at.value + delta};
		}
	};

	inline namespace literals
	{
		/// 0xA8B9A_rom — addresses read exactly like the disassembly they came
		/// from. A literal that does not fit 32 bits is a compile error, not a
		/// silent wrap.
		consteval rom_offset operator""_rom(unsigned long long v)
		{
			if (v > 0xFFFFFFFFull)
			{
				throw "rom_offset literal exceeds 32 bits";
			}
			return rom_offset{static_cast<std::uint32_t>(v)};
		}
	}

	// ----------------------------------------------------------- cursor

	/// Sequential big-endian reader for parsing headers and tables.
	///
	/// Failure is sticky, not per-call: once a read would cross the end of
	/// the image, that read and every later one return 0 and ok() stays
	/// false. Parse a whole header, then check the cursor once.
	///
	/// CAUTION: because failed reads return 0, a data-dependent loop like
	/// `while (cur.u8() != 0xFF)` must also test `cur.ok()` or it will spin
	/// forever on truncated input. Zero-terminated loops are safe as-is.
	///
	/// Plain value type over a borrowed view: copy it to bookmark a
	/// position. It must not outlive the rom_image it came from.
	class cursor
	{
	public:
		std::uint8_t  u8()  noexcept;
		std::uint16_t u16() noexcept; ///< big-endian, 68k "word"
		std::uint32_t u32() noexcept; ///< big-endian, 68k "long"

		/// View of the next `count` bytes; advances. Empty span (and sticky
		/// failure) on overrun.
		std::span<const std::uint8_t> bytes(std::size_t count) noexcept;

		void skip(std::size_t count) noexcept;

		/// After any failed read, position() reports end-of-image, not the
		/// offset where the failure occurred.
		[[nodiscard]] rom_offset  position()  const noexcept { return rom_offset{static_cast<std::uint32_t>(pos_)}; }
		[[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }
		[[nodiscard]] bool ok() const noexcept { return !failed_; }
		explicit operator bool() const noexcept { return ok(); }

	private:
		friend class rom_image;
		cursor(std::span<const std::uint8_t> rom, std::size_t start) noexcept;

		std::span<const std::uint8_t> data_;
		std::size_t pos_ = 0;   // invariant: pos_ <= data_.size()
		bool failed_ = false;
	};

	// -------------------------------------------------------- rom_image

	/// A fully loaded Sega Mega Drive ROM image. A value type: it owns its
	/// bytes, copies are deep, and const access is thread-safe.
	///
	/// Index 0 of the image IS ROM address 0 — this invariant is what lets
	/// the Virgin codec resolve the absolute offsets stored inside its own
	/// chunk tables, which is why decompression takes a rom_image and not an
	/// arbitrary byte span.
	class rom_image
	{
	public:
		/// Read a whole file as binary bytes — the only I/O in the library.
		/// Errors: cannot_open_file, empty_file, file_too_large.
		[[nodiscard]] static result<rom_image> load(const std::filesystem::path& path);

		/// Adopt bytes already in memory (tests, embedded resources, pack
		/// files). Images are addressed through 32-bit rom_offset, so bytes
		/// beyond 4 GiB are unreachable — far above any Mega Drive ROM.
		[[nodiscard]] static rom_image from_bytes(std::vector<std::uint8_t> bytes) noexcept;

		[[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

		/// The whole image, zero-copy. Escape hatch for hot loops; the view
		/// lives only as long as this rom_image.
		[[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

		/// Big-endian scalar reads at an absolute offset. A read that would
		/// cross the end of the image reports out_of_bounds instead of
		/// touching memory — no value is ever fabricated.
		[[nodiscard]] result<std::uint8_t>  u8 (rom_offset at) const noexcept;
		[[nodiscard]] result<std::uint16_t> u16(rom_offset at) const noexcept;
		[[nodiscard]] result<std::uint32_t> u32(rom_offset at) const noexcept;

		/// A view of [at, at + count) — no copy. out_of_bounds if the range
		/// does not fit entirely inside the image.
		[[nodiscard]] result<std::span<const std::uint8_t>>
		slice(rom_offset at, std::size_t count) const noexcept;

		/// Sequential reader starting at `at`. An out-of-range start yields
		/// an already-failed cursor (remaining() == 0, ok() == false).
		[[nodiscard]] cursor at(rom_offset start) const noexcept;

	private:
		rom_image() = default;
		std::vector<std::uint8_t> bytes_;
	};

	// ---------------------------------------------------- decompression
	//
	// Two orthogonal pieces of caller knowledge, both learned by reverse
	// engineering, both irreducible:
	//
	//   1. codec  — HOW the bytes are encoded (the compression algorithm);
	//   2. where the data lives — either a single stream/block at an offset,
	//      or a chunk table the ROM itself stores ({tile count, offset}
	//      records). The table is a data structure of the game, not a
	//      property of the codec, so it gets its own functions and every
	//      codec works with every addressing form.

	/// Compression algorithms recognised by decompress().
	enum class codec : std::uint8_t
	{
		/// Virgin/Westwood LZ (Dune II). A byte-oriented stream of literal /
		/// back-reference / RLE commands, terminated by its 0x80 sentinel.
		virgin_lz,

		/// Block-framed LZSS used by Ancient-era titles (tested on
		/// The Story of Thor / Beyond Oasis). Self-sizing.
		ancient_lzss,
	};

	/// Stable lowercase name ("virgin_lz", ...) for logs and tools.
	[[nodiscard]] std::string_view to_string(codec) noexcept;

	/// Decompressed bytes plus where the packed stream ended — the raw
	/// material of ROM mapping: resource N ends where resource N+1 begins.
	struct unpacked
	{
		std::vector<std::uint8_t> data;
		rom_offset end; ///< first ROM byte after the packed data / table
	};

	/// One record of a chunk table: `tiles` 8x8 VDP tiles (32 bytes each)
	/// packed at absolute ROM offset `at`. Chunk boundaries are meaningful —
	/// they are the game's own sprite/resource subdivisions.
	struct chunk_info
	{
		std::uint16_t tiles = 0;
		rom_offset at;
	};

	/// Default allocation cap for self-sized decompression: no Mega Drive
	/// asset is anywhere near this large, so hitting it means garbage input.
	inline constexpr std::size_t default_max_output = 4u * 1024 * 1024;

	/// Decompress the single stream starting at `at`; its extent and output
	/// size are discovered from the stream itself.
	///
	/// Guarantees: never reads outside `rom`, never writes outside the
	/// returned buffer, allocates at most `max_output` bytes. Malformed
	/// input yields corrupt_stream; `at` past the end yields out_of_bounds.
	/// A successful empty `data` means a genuinely empty resource.
	[[nodiscard]] result<unpacked>
	decompress(const rom_image& rom, rom_offset at, codec format,
	           std::size_t max_output = default_max_output);

	/// Decompress a block whose decompressed size the caller knows (from
	/// reverse-engineering notes). Returns exactly `unpacked_size` bytes; a
	/// stream that would produce more is corrupt_stream, one that produces
	/// fewer leaves the tail zero-filled (that is what the original game's
	/// sized decode path did). A size beyond default_max_output is rejected
	/// as output_limit before anything is allocated.
	[[nodiscard]] result<unpacked>
	decompress(const rom_image& rom, rom_offset at, std::size_t unpacked_size,
	           codec format);

	/// Parse a chunk table at `table_at`: big-endian {u16 count, u32 offset}
	/// records, terminated by a record whose (count & 0x7FFF) is zero. Bit 15
	/// of the count is masked off (purpose unknown; preserved behaviour of
	/// the original 68k code).
	[[nodiscard]] result<std::vector<chunk_info>>
	read_chunk_table(const rom_image& rom, rom_offset table_at);

	/// Read the chunk table at `table_at`, decompress every chunk and
	/// concatenate the results in table order — tiles arrive VDP-ready,
	/// `data.size()` is a multiple of 32. `end` is the first byte after the
	/// table's terminator (per-chunk stream ends are available by calling
	/// decompress() on individual chunk_info records).
	[[nodiscard]] result<unpacked>
	decompress_chunked(const rom_image& rom, rom_offset table_at,
	                   codec format = codec::virgin_lz,
	                   std::size_t max_output = default_max_output);
}
