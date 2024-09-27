#include <fstream>

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

	class RomDataMemoryReader_Impl final : public ym::smd::io::IRomReader
	{
	public:
		explicit RomDataMemoryReader_Impl(ym::smd::io::data_span_t in_data)
			: data_(std::move(in_data)), source_data_(data_->data()), current_data_(data_->data())
		{
		}

		void seek(size_t in_offset) override
		{
			if (data_->size() > 0)
			{
				current_data_ = source_data_ + std::min(data_->size() - 1, in_offset);
			}
		}

		size_t tell() override
		{
			return current_data_ - source_data_;
		}

	private:
		void read_data(void* in_destination, size_t in_size) override
		{
			memcpy(in_destination, current_data_, in_size);
			seek(tell() + in_size);
		}

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

		ym::smd::io::data_span_t data_;

		const ym::smd::io::data_t* source_data_;
		const ym::smd::io::data_t* current_data_;
	};

	class RomDataStreamReader_Impl final : public ym::smd::io::IRomReader
	{
	public:
		explicit RomDataStreamReader_Impl(std::unique_ptr<std::istream> in_stream) : stream_(std::move(in_stream)) {}

		void seek(size_t in_offset) override
		{
			stream_->seekg(static_cast<std::istream::off_type>(in_offset), std::ios::beg);
		}

		size_t tell() override
		{
			return stream_->tellg();
		}

	private:
		void read_data(void* in_destination, size_t in_size) override
		{
			stream_->read(static_cast<char*>(in_destination), in_size);
		}

		std::uint8_t read_byte() const override
		{
			std::uint8_t byte;
			stream_->read(reinterpret_cast<char*>(&byte), sizeof(byte));
			return byte;
		}

		std::uint16_t read_word() const override
		{
			std::uint16_t word;
			stream_->read(reinterpret_cast<char*>(&word), sizeof(word));
			return word;
		}

		std::uint32_t read_long() const override
		{
			std::uint32_t long_value;
			stream_->read(reinterpret_cast<char*>(&long_value), sizeof(long_value));
			return long_value;
		}

		std::unique_ptr<std::istream> stream_;
	};
}

namespace ym::smd::io
{
	rom_reader_t create_rom_reader(data_span_t in_data, size_t in_offset)
	{
		if (in_data != nullptr)
		{
			auto data_reader = std::make_shared<RomDataMemoryReader_Impl>(std::move(in_data));
			data_reader->seek(in_offset);
			return data_reader;
		}
		return nullptr;
	}

	rom_reader_t create_rom_stream_reader(const char* in_path, size_t in_offset)
	{
		if (auto stream = std::make_unique<std::ifstream>(in_path); stream->is_open())
		{
			auto data_reader = std::make_shared<RomDataStreamReader_Impl>(std::move(stream));
			data_reader->seek(in_offset);
			return data_reader;
		}
		return nullptr;
	}
}
