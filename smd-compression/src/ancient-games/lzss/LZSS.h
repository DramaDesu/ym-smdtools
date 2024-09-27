#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

size_t LZSS_Decompress(const uint8_t* src, uint8_t* dst, size_t max_src_size, size_t* compressed_size);
//size_t LZSS_GetDecompressedSize(const uint8_t* src, size_t size, size_t* compressed_size);
size_t LZSS_GetCompressedMaxSize(size_t src_len);
size_t LZSS_CompressSimple(const uint8_t* src, size_t src_len, uint8_t* dst);
size_t LZSS_CompressSimpleFast(const uint8_t* src, size_t src_len, uint8_t* dst, uint16_t* custom_lens);

#ifdef __cplusplus
}
#endif
