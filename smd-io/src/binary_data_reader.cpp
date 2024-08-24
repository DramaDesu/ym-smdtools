#include "include/ym_smd_io.hpp"

namespace
{
	std::uint32_t get_word_be(const std::uint8_t* in_data)
	{
		return in_data[1] | in_data[0] << 8;
	}

	std::uint32_t get_long_be(const uint8_t* in_data)
	{
		return in_data[3] | in_data[2] << 8 | in_data[1] << 16 | in_data[0] << 24;
	}

	class RomDataReader_Impl final : public ym::smd::io::IDataReader
	{
	public:
		explicit RomDataReader_Impl(ym::smd::io::data_t in_data)
			: IDataReader(in_data), current_data_(in_data)
		{
		}

		void seek(size_t in_offset) override
		{
			current_data_ = data_ + in_offset;
		}

		size_t tell() const override
		{
			return current_data_ - data_;
		}

	private:
		std::uint8_t read_byte() const override
		{
			return *current_data_;
		}
		std::uint16_t read_word() const override
		{
			return static_cast<std::uint16_t>(get_word_be(current_data_));
		}
		std::uint32_t read_long() const override
		{
			return get_long_be(current_data_);
		}

		ym::smd::io::data_t current_data_;
	};
}

namespace ym::smd::io
{
	data_reader_t create_data_reader(data_t in_data, size_t in_offset)
	{
		auto data_reader = std::make_shared<RomDataReader_Impl>(in_data);
		data_reader->seek(in_offset);
		return data_reader;
	}
}
