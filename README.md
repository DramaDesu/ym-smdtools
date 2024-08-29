# ym-smdtools
Yet More SMD (Sega Mega Drive) Tools Library

Collection of tools for smd:
Decompression Library
etc

# Decompression
```
// Main api header
#include "ym_smd_compression.hpp"
```

```
// Supported games set
auto supported_games = ym::smd::get_supported_games();
```

```
// Create data decompressor to unpack graphics from games
if (auto decompressor = ym::smd::create_data_decompressor("virgin"sv)) // For virgin games (Tested only on original Dune 2)
{
  auto result = decompressor->decompress(rom_data, offset); // get tiles, or data for vdp (could be graphics tile, could be plane tiles, etc)
  // do something here, convert 8x8 (32 bytes tiles) to something you want
}
```
