// Backwards bitstream LZ77, reverse-engineered from the 68000 routine at ROM
// 0x28A2 of The Pirates of Dark Water (Mega Drive).
//
// What makes it unusual is the direction: the block opens with a 16-bit
// little-endian payload length used to seek to its *end*, and from there both
// the input cursor and the output cursor walk downwards. Bits are taken
// MSB-first from bytes consumed in descending address order.
//
//   +0            u16 le   packed payload length
//   ...                    payload
//   end-2         u16 le   unpacked length  (read first, backwards)
//   end-3         u8       initial bit count
//   end-4         u8       initial bit buffer
//
// Field widths come from a four-entry table {3, 7, 15, 0} (ROM 0x2990) giving
// 4-, 8-, 16- and 1-bit fields. Which entry applies is decided by a selector
// the *bit reader itself* leaves behind — reading a bit stores its value in
// the same register the width lookup indexes. That coupling is why the decode
// below follows the original's control flow closely instead of being
// restructured into something tidier: the shared state is the format.
//
// Verified against the game: breaking after each unpack call and comparing
// work RAM byte for byte, blocks of 630..5046 bytes matched exactly.
#include "ym/smd.hpp"

namespace
{
	using namespace ym::smd;

	// ROM 0x2990. Indexed by the selector; the field is entry+1 bits wide.
	constexpr std::uint8_t width_table[4] = {3, 7, 15, 0};

	// Reads bits MSB-first out of bytes walked downwards from `cursor`.
	class bit_reader
	{
	public:
		bit_reader(const std::uint8_t* base, std::size_t cursor,
		           std::uint8_t count, std::uint8_t buffer) noexcept
			: base_(base), cursor_(cursor), count_(count), buffer_(buffer)
		{
		}

		[[nodiscard]] bool exhausted() const noexcept { return underflow_; }
		[[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
		void retreat(std::size_t n) noexcept { cursor_ = (cursor_ >= n) ? cursor_ - n : (underflow_ = true, 0); }

		/// One bit. Also latches it as the field-width selector, exactly as the
		/// original does — several call sites rely on that side effect.
		std::uint16_t bit() noexcept
		{
			const std::uint16_t v = shift(0);
			selector_ = v;
			return v;
		}

		/// n+1 bits, matching the original's post-decrement loop.
		std::uint16_t bits(unsigned n) noexcept
		{
			std::uint16_t v = 0;
			for (unsigned i = 0; i <= n; ++i)
			{
				v = shift(v);
			}
			selector_ = v;
			return v;
		}

		/// A field whose width the current selector chooses.
		std::uint16_t sized() noexcept { return bits(width_table[selector_ & 3]); }

		void set_selector(std::uint16_t v) noexcept { selector_ = v; }
		[[nodiscard]] std::uint16_t selector() const noexcept { return selector_; }

	private:
		std::uint16_t shift(std::uint16_t acc) noexcept
		{
			if (count_ == 0)
			{
				if (cursor_ == 0)
				{
					underflow_ = true;
					return acc;
				}
				buffer_ = base_[cursor_];
				--cursor_;
				count_ = 8;
			}
			const std::uint16_t out = static_cast<std::uint16_t>((acc << 1) | ((buffer_ >> 7) & 1));
			buffer_ = static_cast<std::uint8_t>(buffer_ << 1);
			--count_;
			return out;
		}

		const std::uint8_t* base_;
		std::size_t   cursor_;
		std::uint8_t  count_;
		std::uint8_t  buffer_;
		std::uint16_t selector_ = 0;
		bool          underflow_ = false;
	};

	struct header
	{
		std::size_t unpacked_size;
		std::size_t payload_end;   // one past the last payload byte
		std::size_t bit_cursor;    // where the bit reader starts, walking down
		std::uint8_t bit_count;
		std::uint8_t bit_buffer;
	};

	result<header> read_header(const rom_image& rom, rom_offset at)
	{
		const auto& bytes = rom.bytes();
		const std::size_t base = at.value;
		if (base + 2 > rom.size())
		{
			return errc::out_of_bounds;
		}
		const std::size_t packed = static_cast<std::size_t>(bytes[base])
		                         | (static_cast<std::size_t>(bytes[base + 1]) << 8);

		header h{};
		h.payload_end = base + 2 + packed;
		// Four trailer bytes are read before any bit is consumed.
		if (h.payload_end > rom.size() || packed < 4)
		{
			return errc::out_of_bounds;
		}
		h.unpacked_size = static_cast<std::size_t>(bytes[h.payload_end - 1]) << 8
		                | static_cast<std::size_t>(bytes[h.payload_end - 2]);
		h.bit_count  = bytes[h.payload_end - 3];
		h.bit_buffer = bytes[h.payload_end - 4];
		h.bit_cursor = h.payload_end - 5;
		return h;
	}

	/// Decode into `out`, which must already be `header.unpacked_size` long.
	/// Returns the offset just past the packed data.
	result<rom_offset> decode(const rom_image& rom, rom_offset at, const header& h,
	                          std::vector<std::uint8_t>& out)
	{
		const auto& bytes = rom.bytes();
		const std::size_t out_size = h.unpacked_size;

		bit_reader in(bytes.data(), h.bit_cursor, h.bit_count, h.bit_buffer);

		std::size_t write = out_size;   // output is filled downwards
		std::size_t written = 0;

		// Both copies are bounds-checked: a misdecode naturally produces an
		// out-of-range index, and silently wrapping it would yield a buffer
		// that still reaches the declared size and looks plausible.
		const auto copy_literal = [&](std::size_t src, std::size_t len) -> bool {
			if (len > write || src + len > rom.size())
			{
				return false;
			}
			write -= len;
			for (std::size_t i = 0; i < len; ++i)
			{
				out[write + i] = bytes[src + i];
			}
			written += len;
			return true;
		};

		const auto copy_match = [&](std::size_t src, std::size_t len) -> bool {
			if (len > write || src + len > out_size)
			{
				return false;
			}
			write -= len;
			for (std::size_t i = 0; i < len; ++i)
			{
				out[write + i] = out[src + i];   // may overlap; must stay byte-wise
			}
			written += len;
			return true;
		};

		while (written < out_size)
		{
			if (in.exhausted())
			{
				return errc::corrupt_stream;
			}

			if (in.bit())
			{
				// Literal run: 2 bits, escaping to a wider field at 3.
				std::uint16_t len = in.bits(1);
				if (len >= 3)
				{
					std::uint16_t sel = 0;
					if (in.bit())
					{
						sel = static_cast<std::uint16_t>(in.bit() + 1);
					}
					in.set_selector(sel);
					len = static_cast<std::uint16_t>(in.sized() + 3);
				}
				++len;

				in.retreat(len);
				if (in.exhausted() || !copy_literal(in.cursor() + 1, len))
				{
					return errc::corrupt_stream;
				}
				if (written >= out_size)
				{
					break;
				}
			}

			// A match always follows a literal run, and is also what a leading
			// zero bit selects on its own.
			std::uint16_t length = 0;
			std::uint16_t distance = 0;
			if (in.bit())
			{
				distance = in.sized();          // selector is 1 here: 8-bit field
				length = 2;
			}
			else
			{
				std::uint16_t extra = 0;
				if (in.bit())
				{
					in.bit();
					extra = static_cast<std::uint16_t>(in.sized() + 1);
				}
				length = static_cast<std::uint16_t>(extra + 3);
				in.bit();                       // read for its selector effect
				in.set_selector(static_cast<std::uint16_t>(in.selector() + 1));
				distance = in.sized();
			}

			if (distance == 0 || write + distance - 1 >= out_size)
			{
				return errc::corrupt_stream;
			}
			if (!copy_match(write + distance - 1, length))
			{
				return errc::corrupt_stream;
			}
		}

		if (written != out_size || in.exhausted())
		{
			return errc::corrupt_stream;
		}
		return rom_offset{static_cast<std::uint32_t>(h.payload_end)};
	}
}

namespace ym::smd::detail
{
	result<unpacked> reverse_lz_decompress_stream(const rom_image& rom, rom_offset at,
	                                              std::size_t max_output)
	{
		auto head = read_header(rom, at);
		if (!head)
		{
			return head.error();
		}
		if (head.value().unpacked_size > max_output)
		{
			return errc::output_limit;
		}

		unpacked res;
		res.data.resize(head.value().unpacked_size);
		auto end = decode(rom, at, head.value(), res.data);
		if (!end)
		{
			return end.error();
		}
		res.end = end.value();
		return res;
	}

	result<unpacked> reverse_lz_decompress_block(const rom_image& rom, rom_offset at,
	                                             std::size_t unpacked_size)
	{
		auto head = read_header(rom, at);
		if (!head)
		{
			return head.error();
		}
		if (head.value().unpacked_size > unpacked_size)
		{
			return errc::corrupt_stream;   // stream promises more than asked for
		}

		unpacked res;
		res.data.resize(head.value().unpacked_size);
		auto end = decode(rom, at, head.value(), res.data);
		if (!end)
		{
			return end.error();
		}
		res.data.resize(unpacked_size);    // short stream leaves the tail zeroed
		res.end = end.value();
		return res;
	}
}
