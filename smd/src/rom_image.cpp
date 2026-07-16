#include "ym/smd.hpp"

#include <fstream>

namespace ym::smd
{
	// ------------------------------------------------------------ errc

	std::string_view to_string(errc e) noexcept
	{
		switch (e)
		{
		case errc::cannot_open_file: return "cannot_open_file";
		case errc::empty_file:       return "empty_file";
		case errc::file_too_large:   return "file_too_large";
		case errc::out_of_bounds:    return "out_of_bounds";
		case errc::corrupt_stream:   return "corrupt_stream";
		case errc::output_limit:     return "output_limit";
		}
		return "unknown";
	}

	std::string_view to_string(codec c) noexcept
	{
		switch (c)
		{
		case codec::virgin_lz:    return "virgin_lz";
		case codec::ancient_lzss: return "ancient_lzss";
		}
		return "unknown";
	}

	// ----------------------------------------------------------- cursor

	cursor::cursor(std::span<const std::uint8_t> rom, std::size_t start) noexcept
		: data_(rom)
	{
		if (start <= data_.size())
		{
			pos_ = start;
		}
		else
		{
			pos_ = data_.size();
			failed_ = true;
		}
	}

	std::uint8_t cursor::u8() noexcept
	{
		if (failed_ || data_.size() - pos_ < 1)
		{
			failed_ = true;
			return 0;
		}
		return data_[pos_++];
	}

	std::uint16_t cursor::u16() noexcept
	{
		if (failed_ || data_.size() - pos_ < 2)
		{
			failed_ = true;
			pos_ = data_.size();
			return 0;
		}
		const std::uint16_t value = static_cast<std::uint16_t>(data_[pos_] << 8 | data_[pos_ + 1]);
		pos_ += 2;
		return value;
	}

	std::uint32_t cursor::u32() noexcept
	{
		if (failed_ || data_.size() - pos_ < 4)
		{
			failed_ = true;
			pos_ = data_.size();
			return 0;
		}
		const std::uint32_t value = static_cast<std::uint32_t>(data_[pos_]) << 24
		                          | static_cast<std::uint32_t>(data_[pos_ + 1]) << 16
		                          | static_cast<std::uint32_t>(data_[pos_ + 2]) << 8
		                          | static_cast<std::uint32_t>(data_[pos_ + 3]);
		pos_ += 4;
		return value;
	}

	std::span<const std::uint8_t> cursor::bytes(std::size_t count) noexcept
	{
		if (failed_ || data_.size() - pos_ < count)
		{
			failed_ = true;
			pos_ = data_.size();
			return {};
		}
		const auto view = data_.subspan(pos_, count);
		pos_ += count;
		return view;
	}

	void cursor::skip(std::size_t count) noexcept
	{
		if (failed_ || data_.size() - pos_ < count)
		{
			failed_ = true;
			pos_ = data_.size();
			return;
		}
		pos_ += count;
	}

	// -------------------------------------------------------- rom_image

	result<rom_image> rom_image::load(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return errc::cannot_open_file;
		}

		file.seekg(0, std::ios::end);
		const auto file_size = file.tellg();
		if (file_size < 0)
		{
			return errc::cannot_open_file;
		}
		if (file_size == 0)
		{
			return errc::empty_file;
		}
		// Largest Mega Drive cartridge images (with mappers) are a few MiB;
		// 64 MiB of headroom keeps a mistaken path to a disk image from
		// turning into a giant allocation.
		constexpr std::streamoff max_rom_file_size = 64 * 1024 * 1024;
		if (file_size > max_rom_file_size)
		{
			return errc::file_too_large;
		}

		rom_image rom;
		rom.bytes_.resize(static_cast<std::size_t>(file_size));
		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(rom.bytes_.data()),
		          static_cast<std::streamsize>(rom.bytes_.size()));
		if (!file)
		{
			return errc::cannot_open_file;
		}
		return rom;
	}

	rom_image rom_image::from_bytes(std::vector<std::uint8_t> bytes) noexcept
	{
		rom_image rom;
		rom.bytes_ = std::move(bytes);
		return rom;
	}

	result<std::uint8_t> rom_image::u8(rom_offset at) const noexcept
	{
		if (at.value >= bytes_.size())
		{
			return errc::out_of_bounds;
		}
		return bytes_[at.value];
	}

	result<std::uint16_t> rom_image::u16(rom_offset at) const noexcept
	{
		if (bytes_.size() < 2 || at.value > bytes_.size() - 2)
		{
			return errc::out_of_bounds;
		}
		return static_cast<std::uint16_t>(bytes_[at.value] << 8 | bytes_[at.value + 1]);
	}

	result<std::uint32_t> rom_image::u32(rom_offset at) const noexcept
	{
		if (bytes_.size() < 4 || at.value > bytes_.size() - 4)
		{
			return errc::out_of_bounds;
		}
		return static_cast<std::uint32_t>(bytes_[at.value]) << 24
		     | static_cast<std::uint32_t>(bytes_[at.value + 1]) << 16
		     | static_cast<std::uint32_t>(bytes_[at.value + 2]) << 8
		     | static_cast<std::uint32_t>(bytes_[at.value + 3]);
	}

	result<std::span<const std::uint8_t>>
	rom_image::slice(rom_offset at, std::size_t count) const noexcept
	{
		if (at.value > bytes_.size() || bytes_.size() - at.value < count)
		{
			return errc::out_of_bounds;
		}
		return std::span<const std::uint8_t>(bytes_).subspan(at.value, count);
	}

	cursor rom_image::at(rom_offset start) const noexcept
	{
		return cursor(bytes_, start.value);
	}
}
