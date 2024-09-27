#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ym::smd::io
{
	using data_t = std::uint8_t;

	class IData
	{
	public:
		IData() = default;
		virtual ~IData() = default;

		IData(IData&&) noexcept = default;
		IData& operator=(IData&&) noexcept = default;

		IData(const IData&) = delete;
		IData& operator=(const IData&) = delete;

		virtual const data_t* data() const = 0;
		virtual size_t size() const = 0;

		virtual operator bool() const = 0;
	};

	class IRomReader
	{
	public:
		virtual ~IRomReader() = default;

		virtual void seek(size_t in_offset) = 0;
		virtual size_t tell() = 0;

		template<typename T>
		T read()
		{
			static_assert(sizeof(T) < std::numeric_limits<std::uint8_t>::max());
			static_assert(std::is_arithmetic_v<T> == true);

			struct read_on_scope
			{
				read_on_scope(IRomReader* in_data_reader, std::uint8_t in_offset) : data_reader_(in_data_reader), offset_(in_offset) {}
				~read_on_scope()
				{
					const auto target_offset = data_reader_->tell() + offset_;
					data_reader_->seek(target_offset);
				}

			private:
				IRomReader* data_reader_;
				std::uint8_t offset_;
			};

			const read_on_scope scope(this, sizeof(T));

			if constexpr (sizeof(T) == sizeof(std::uint8_t))
			{
				return read_byte();
			}
			else if constexpr (sizeof(T) == sizeof(std::uint16_t))
			{
				return read_word();
			}
			else if constexpr (sizeof(T) == sizeof(std::uint32_t))
			{
				return read_long();
			}
			else
			{
				return {};
			}
		}

		virtual void read_data(void* in_destination, size_t in_size) = 0;

	protected:
		virtual std::uint8_t read_byte() const = 0;
		virtual std::uint16_t read_word() const = 0;
		virtual std::uint32_t read_long() const = 0;
	};

	using data_span_t = std::shared_ptr<IData>;
	using rom_reader_t = std::shared_ptr<IRomReader>;

	std::string read_string(const char* in_path);
	data_span_t read_data(const char* in_path);

	rom_reader_t create_rom_reader(data_span_t in_data, size_t in_offset = 0);
	rom_reader_t create_rom_stream_reader(const char* in_path, size_t in_offset = 0);
}
