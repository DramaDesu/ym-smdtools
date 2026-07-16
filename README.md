# ym-smdtools
Yet More SMD (Sega Mega Drive) Tools Library.

One public header, one library: load a ROM image, read it big-endian with
bounds checking, and decompress the packed resources shipped in supported
games. Nothing throws, nothing reads or writes out of bounds whatever the
input, and everything taking `const rom_image&` is thread-safe.

```cpp
#include <ym/smd.hpp>

using namespace ym::smd;
using namespace ym::smd::literals;
```

## Loading a ROM

```cpp
auto rom = rom_image::load("Dune - The Battle for Arrakis (U).gen");
if (!rom) { /* rom.error(): cannot_open_file / empty_file */ }
```

All multi-byte reads are big-endian, as the 68000 saw them:

```cpp
auto word = rom.value().u16(0xF0C22_rom);   // result<uint16_t>, bounds-checked

cursor cur = rom.value().at(0xA8000_rom);   // sequential parsing
auto a = cur.u32();
auto b = cur.u16();
if (!cur) { /* ran past the end — checked once, after the burst */ }
```

## Decompression

Two orthogonal things the caller knows from reverse engineering: the
compression algorithm (`codec`) and where the data lives.

```cpp
// A resource addressed through a chunk table the ROM itself stores
// ({u16 tile count, u32 offset} records). Tiles arrive VDP-ready,
// 32 bytes per 8x8 tile:
auto gfx = decompress_chunked(rom.value(), 0xA8B9A_rom);            // codec::virgin_lz

// A single self-terminating stream:
auto ui = decompress(rom.value(), 0x1545C8_rom, codec::ancient_lzss);

// A block whose decompressed size you know:
auto map = decompress(rom.value(), 0x9F200_rom, 64 * 28 * 2, codec::virgin_lz);

// Every result carries where the packed data ended - resource N ends
// where resource N+1 begins:
map.value().end;  // rom_offset of the first byte after the stream

// Chunk boundaries are the game's own sprite subdivisions:
auto chunks = read_chunk_table(rom.value(), 0xA8B9A_rom);
```

Supported codecs:

| codec | games tested | format |
|---|---|---|
| `codec::virgin_lz` | Dune II — The Battle for Arrakis | Virgin/Westwood LZ: literal / back-reference / RLE commands, 0x80 terminator |
| `codec::ancient_lzss` | The Story of Thor / Beyond Oasis | block-framed LZSS, self-sizing |

Failures are reported through `result<T>` / `errc` — no exceptions, no
ambiguous empty vectors: `cannot_open_file`, `empty_file`, `out_of_bounds`,
`corrupt_stream`, `output_limit`.

## Building

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Requires a C++20 compiler. Unit tests run on synthetic data; the ROM
integration test skips itself unless ROM paths are provided via
`YM_SMD_DUNE_ROM` / `YM_SMD_OASIS_ROM` (or `test/dune_path.txt` /
`test/oasis_path.txt`).

To embed in another project: `add_subdirectory(ym-smdtools)` and link
`ym-smd::ym-smd`.
