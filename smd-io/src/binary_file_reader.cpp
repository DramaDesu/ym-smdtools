#include "include/ym_smd_io.hpp"

#include <fstream>

namespace ym::smd::io
{
	bool load_data(const char* in_path, std::vector<std::uint8_t>& in_out_data)
	{
		if (auto file = std::ifstream(in_path, std::ios::binary))
		{
			file.seekg(0, std::ios::end);
			if (const auto file_size = file.tellg())
			{
				in_out_data.resize(file_size);

				file.seekg(0, std::ios::beg);
				file.read(reinterpret_cast<char*>(in_out_data.data()), file_size);

				return true;
			}
		}
		return false;
	}
}