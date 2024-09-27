#include "include/ym_smd_io.hpp"

#include <fstream>

namespace ym::smd::io
{
	template <typename F>
	bool read_file(const char* in_path, F&& read_callback)
	{
		if (auto file = std::ifstream(in_path, std::ios::binary)) 
		{
			file.seekg(0, std::ios::end);
			if (const auto file_size = file.tellg()) 
			{
				file.seekg(0, std::ios::beg);
				std::forward<F>(read_callback)(file, file_size);
				return true;
			}
		}
		return false;
	}

	bool read_data(const char* in_path, std::vector<std::uint8_t>& in_out_data)
	{
		return read_file(in_path, [&](auto& stream, auto size)
		{
			in_out_data.resize(size);
			stream.read(reinterpret_cast<char*>(in_out_data.data()), size);
		});
	}

	data_span_t read_data(const char* in_path)
	{
		class DataImpl : public IData
		{
		public:
			const data_t* data() const override { return data_.data(); }
			size_t size() const override { return data_.size(); }
			operator bool() const override { return !data_.empty(); }

			std::vector<std::uint8_t> data_;
		};

		if (auto data = std::make_shared<DataImpl>())
		{
			if (read_data(in_path, data->data_))
			{
				return data;
			}
		}

		return nullptr;
	}

	std::string read_string(const char* in_path)
	{
		std::string out_value;

		read_file(in_path, [&](auto& stream, std::streamsize size)
		{
			out_value.resize(size + 1);
			stream.read(out_value.data(), size);
		});

		return out_value;
	}
}
