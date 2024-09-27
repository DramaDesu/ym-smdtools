cmake_minimum_required (VERSION 3.8)

include(${CMAKE_SOURCE_DIR}/cmake/library_utils.cmake)

create_ym_library(ym-smd-compression)

target_sources(ym-smd-compression PRIVATE src/supported_games.cpp src/virgin-games/dune_impl.cpp src/ancient-games/lzss_ancient_impl.cpp src/ancient-games/lzss/LZSS.c)

target_link_libraries(ym-smd-compression PRIVATE ym-smd-io)