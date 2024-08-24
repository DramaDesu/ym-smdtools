#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace ym::smd::io
{
	class IDataReader;

	using data_reader_t = std::shared_ptr<IDataReader>;
	using data_t = const std::uint8_t*;

	class IDataReader
	{
	public:
		explicit IDataReader(data_t in_data) : data_(in_data) {}
		virtual ~IDataReader() = default;

		virtual void seek(size_t in_offset) = 0;
		virtual size_t tell() const = 0;

		template<typename T>
		T read()
		{
			static_assert(sizeof(T) < std::numeric_limits<std::uint8_t>::max());
			static_assert(std::is_arithmetic_v<T> == true);

			struct read_on_scope
			{
				read_on_scope(IDataReader* in_data_reader, std::uint8_t in_offset) : data_reader_(in_data_reader), offset_(in_offset) {}
				~read_on_scope()
				{
					const auto target_offset = data_reader_->tell() + offset_;
					data_reader_->seek(target_offset);
				}

			private:
				IDataReader* data_reader_;
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

	protected:
		virtual std::uint8_t read_byte() const = 0;
		virtual std::uint16_t read_word() const = 0;
		virtual std::uint32_t read_long() const = 0;

		data_t data_;
	};

	bool load_data(const char* in_path, std::vector<std::uint8_t>& in_out_data);

	data_reader_t create_data_reader(data_t in_data, size_t in_offset = 0);
}
