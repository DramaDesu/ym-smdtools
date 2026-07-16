// Integration test against real ROM images. Skipped (exit 77) when the ROMs
// are not available.
//
// ROM locations are resolved from, in order:
//   1. environment variables YM_SMD_DUNE_ROM / YM_SMD_OASIS_ROM (full paths);
//   2. dune_path.txt / oasis_path.txt in the working directory (one path per
//      file, relative paths resolved against the working directory). These
//      files are gitignored; copy the committed *.example files to create them.
//
// When YM_SMD_DUMP_DIR is set, decompressed outputs are written there as
// decompressed_<name>.bin for A/B comparison against other implementations.
#include <ym/smd.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using namespace ym::smd;
using namespace ym::smd::literals;

namespace
{
	constexpr auto skip_exit_code = 77;

	// Known asset locations in the supported ROMs (reverse-engineered).
	constexpr auto dune_houses_table = 0xA8B9A_rom;  // house-select graphics, chunked
	constexpr auto oasis_health_ui = 0x1545C8_rom;   // HUD health graphics, single stream

	int failures = 0;

#define CHECK(cond)                                                        \
	do                                                                     \
	{                                                                      \
		if (!(cond))                                                       \
		{                                                                  \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			++failures;                                                    \
		}                                                                  \
	} while (0)

	std::optional<std::filesystem::path> find_rom(const char* env_name, const char* path_file)
	{
		if (const char* env = std::getenv(env_name); env != nullptr && *env != '\0')
		{
			return std::filesystem::path(env);
		}
		std::ifstream file(path_file);
		if (std::string line; file && std::getline(file, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
			{
				line.pop_back();
			}
			if (!line.empty())
			{
				return std::filesystem::path(line);
			}
		}
		return std::nullopt;
	}

	void dump(const char* name, const std::vector<std::uint8_t>& data)
	{
		const char* dir = std::getenv("YM_SMD_DUMP_DIR");
		if (dir == nullptr || *dir == '\0')
		{
			return;
		}
		const auto path = std::filesystem::path(dir) / (std::string("decompressed_") + name + ".bin");
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(data.data()),
		          static_cast<std::streamsize>(data.size()));
	}

	bool test_dune(const std::filesystem::path& rom_path)
	{
		auto rom = rom_image::load(rom_path);
		if (!rom)
		{
			std::printf("dune: cannot load '%s' (%.*s)\n", rom_path.string().c_str(),
			            static_cast<int>(to_string(rom.error()).size()), to_string(rom.error()).data());
			return false;
		}

		const auto chunks = read_chunk_table(rom.value(), dune_houses_table);
		CHECK(chunks.has_value() && !chunks.value().empty());

		const auto houses = decompress_chunked(rom.value(), dune_houses_table);
		CHECK(houses.has_value());
		if (houses)
		{
			CHECK(!houses.value().data.empty());
			CHECK(houses.value().data.size() % 32 == 0);
			std::printf("dune houses: %zu chunks, %zu bytes (%zu tiles)\n",
			            chunks ? chunks.value().size() : 0, houses.value().data.size(),
			            houses.value().data.size() / 32);
			dump("virgin", houses.value().data);
		}
		return true;
	}

	bool test_oasis(const std::filesystem::path& rom_path)
	{
		auto rom = rom_image::load(rom_path);
		if (!rom)
		{
			std::printf("oasis: cannot load '%s' (%.*s)\n", rom_path.string().c_str(),
			            static_cast<int>(to_string(rom.error()).size()), to_string(rom.error()).data());
			return false;
		}

		const auto health = decompress(rom.value(), oasis_health_ui, codec::ancient_lzss);
		CHECK(health.has_value());
		if (health)
		{
			CHECK(!health.value().data.empty());
			CHECK(health.value().end > oasis_health_ui);
			std::printf("oasis health ui: %zu bytes, stream ends at 0x%X\n",
			            health.value().data.size(), health.value().end.value);
			dump("ancient", health.value().data);
		}
		return true;
	}
}

int main()
{
	bool ran_any = false;

	if (const auto dune = find_rom("YM_SMD_DUNE_ROM", "dune_path.txt"))
	{
		ran_any = test_dune(*dune) || ran_any;
	}
	if (const auto oasis = find_rom("YM_SMD_OASIS_ROM", "oasis_path.txt"))
	{
		ran_any = test_oasis(*oasis) || ran_any;
	}

	if (!ran_any)
	{
		std::printf("no ROM images available - skipping\n");
		return skip_exit_code;
	}
	if (failures == 0)
	{
		std::printf("all rom tests passed\n");
		return 0;
	}
	std::printf("%d check(s) failed\n", failures);
	return 1;
}
