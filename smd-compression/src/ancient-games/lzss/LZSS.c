#include "LZSS.h"

#include <assert.h>

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <inttypes.h>


#define WINDOW_SIZE_8  0x1FFF
#define MAX_SEQ_8      (14 + 0xFF)
#define MIN_SEQ_8      2
#define MAX_RLE        (14 + 0xFFF)
#define MIN_RLE        14
#define CONTROL_BITS   16

static int bitPos;
static uint16_t controlWord;
static uint16_t GetControlBit(const uint8_t** src)
{
    if (bitPos == 0) {
        controlWord = **src | (*(*src + 1) << 8);
        *src += 2;
        bitPos = CONTROL_BITS;
    }
    bitPos--;

    uint16_t flag = controlWord & 1;
    controlWord >>= 1;
    return flag;
}

size_t LZSS_Decompress(const uint8_t* src, uint8_t* dst, size_t max_src_size, size_t* compressed_size)
{
    // src + 1 >= src_end т. к. нужно чтение 2 байт
#define CHECK_CONTROL_BIT if (bitPos == 0 && src + 1 >= src_end) return -1;
#define CHECK_SPOS if (src >= src_end) return -1;
#define CHECK_SPOS_2BYTES if (src + 1 >= src_end) return -1;
#define CHECK_SPOS_3BYTES if (src + 2 >= src_end) return -1;
#define CHECK_SPOS_4BYTES if (src + 3 >= src_end) return -1;
    uint16_t flag;

    size_t offset = 0; // uint16_t
    size_t length = 0; // uint16_t

    size_t dpos = 0;
    const uint8_t* src_start = src;
    const uint8_t* src_end = src + max_src_size;

    CHECK_SPOS_3BYTES
    if (src[2] != 0) {
        // Method 2
        uint8_t ctrl;
        const uint8_t* src_block_start = src_start;

        for (;;) {
            CHECK_SPOS_2BYTES
            size_t comp_size = src_block_start[0] | (src_block_start[1] << 8);
            src = src_block_start + 2;
            while (src != src_block_start + comp_size) {
                CHECK_SPOS
                ctrl = *src;
                src++;
                if ((ctrl & 0x80) == 0) {
                    if ((ctrl & 0x40) == 0) {
                        // Literals
                        // len: 1-31, 1-8191
                        length = ctrl & 0x1F;
                        if ((ctrl & 0x20) != 0) {
                            CHECK_SPOS
                            length = (length << 8) | *src;
                            src++;
                        }
                        if (length == 0) {
                            return -1;
                        }
                        dpos += length;
                        if (dst != NULL) {
                            while (length != 0) {
                                CHECK_SPOS
                                *dst = *src;
                                dst++;
                                src++;
                                length--;
                            }
                        }
                        else {
                            src += length;
                        }
                    }
                    else {
                        // RLE
                        // len: 4-19, (36-51), 4-4099, (8196-12291)
                        length = ctrl & 0x2F;
                        if ((ctrl & 0x10) != 0) {
                            CHECK_SPOS
                            length = (length << 8) | *src;
                            src++;
                        }
                        length += 4;
                        CHECK_SPOS
                        ctrl = *src;
                        src++;
                        dpos += length;
                        if (dst != NULL) {
                            while (length != 0) {
                                *dst = ctrl;
                                dst++;
                                length--;
                            }
                        }
                    }
                }
                else {
                    // LZ
                    // len: сначала 4-7, потом 1-31
                    length = ((ctrl & 0x60) >> 5) + 4;
                    CHECK_SPOS
                    offset = ((ctrl & 0x1F) << 8) | *src;
                    if (dpos < offset) {
                        return -1;
                    }
                    uint8_t* p_copy = dst;
                    if (dst != NULL) {
                        p_copy = p_copy - offset;
                    }
                    src++;
                    for (;;) {
                        dpos += length;
                        if (dst != NULL) {
                            while (length != 0) {
                                *dst = *p_copy;
                                dst++;
                                p_copy++;
                                length--;
                            }
                        }
                        if (src == src_block_start + comp_size)
                            break;
                        CHECK_SPOS
                        if ((*src & 0xE0) != 0x60)
                            break;
                        length = *src & 0x1F;
                        src++;
                    }
                }
            }
            src_block_start = src + 1;
            // Если в конце нет байта для обозначения блока, не считать ошибкой
            // TODO: Или нужно?
            if (src >= src_end || *src == 0) {
                if (compressed_size != NULL) {
                    *compressed_size = src - src_start + 1;
                }
                return dpos;
            }
        }
    }

    // Method 2
    for (;;) {
        CHECK_SPOS_4BYTES
        controlWord = src[3];
        bitPos = 8;
        src += 4;

        for (;;) {
            // bit 0 + literal
            CHECK_CONTROL_BIT
            flag = GetControlBit(&src);
            if (flag == 0) {
                CHECK_SPOS
                if (dst != NULL) {
                    *dst = *src;
                    dst++;
                }
                src++;
                dpos++;
                continue;
            }

            // bit 1
            int lz_flag = 0;
            CHECK_CONTROL_BIT
            flag = GetControlBit(&src);
            if (flag == 0) {
                // bit 10. offset: 0-255
                CHECK_SPOS
                offset = *src;
                if (offset == 0) {
                    return -1;
                }
                src++;
                lz_flag = 1;
            }
            else {
                // bit 11 xxxxx
                CHECK_CONTROL_BIT
                offset =                 GetControlBit(&src);
                CHECK_CONTROL_BIT
                offset = (offset << 1) | GetControlBit(&src);
                CHECK_CONTROL_BIT
                offset = (offset << 1) | GetControlBit(&src);
                CHECK_CONTROL_BIT
                offset = (offset << 1) | GetControlBit(&src);
                CHECK_CONTROL_BIT
                offset = (offset << 1) | GetControlBit(&src);
                CHECK_SPOS
                offset = (offset << 8) | *src;
                src++;
                if (offset > 1) {
                    // offset: 2-8191 (0x1FFF)
                    lz_flag = 1;
                }
                else if (offset == 1) {
                    // bit 11 00000 && byte == 1
                    // RLE
                    CHECK_CONTROL_BIT
                    flag = GetControlBit(&src);
                    CHECK_CONTROL_BIT
                    length =                 GetControlBit(&src);
                    CHECK_CONTROL_BIT
                    length = (length << 1) | GetControlBit(&src);
                    CHECK_CONTROL_BIT
                    length = (length << 1) | GetControlBit(&src);
                    CHECK_CONTROL_BIT
                    length = (length << 1) | GetControlBit(&src);

                    if (flag == 1) {
                        // Test: 17093C
                        // length: 14-4109
                        CHECK_SPOS
                        length = (length << 8) | *src;
                        src++;
                    }
                    // else length: 14-29
                    length += 0xE;
                    CHECK_SPOS
                    uint8_t rep_byte = *src;
                    src++;
                    dpos += length;
                    if (dst != NULL) {
                        while (length != 0) {
                            *dst = rep_byte;
                            dst++;
                            length--;
                        }
                    }
                }
                else { // offset == 0
                    // bit 11 00000 && byte == 0
                    CHECK_SPOS
                    // TODO: Не считать ошибкой конец данных?
                    if (*src == 0) {
                        if (compressed_size != NULL) {
                            *compressed_size = src - src_start + 1;
                        }
                        return dpos;
                    }
                    src++;
                    //printf("TODO: Next block %X %X\n", *(src+1), *src);
                    // Next block
                    break;
                }
            }

            if (lz_flag) {
                length = 2;
                CHECK_CONTROL_BIT
                flag = GetControlBit(&src);
                if (flag == 0) {
                    // bit 0
                    length = 3;
                    CHECK_CONTROL_BIT
                    flag = GetControlBit(&src);
                    if (flag == 0) {
                        // bit 00
                        length = 4;
                        CHECK_CONTROL_BIT
                        flag = GetControlBit(&src);
                        if (flag == 0) {
                            // bit 000
                            length = 5;
                            CHECK_CONTROL_BIT
                            flag = GetControlBit(&src);
                            if (flag == 0) {
                                // bit 0000
                                CHECK_CONTROL_BIT
                                flag = GetControlBit(&src);
                                if (flag == 0) {
                                    // bit 00000
                                    // length: 14-269
                                    CHECK_SPOS
                                    length = *src + 0xE;
                                    src++;
                                }
                                else {
                                    // bit 00001 xxx
                                    // length: 6-13
                                    CHECK_CONTROL_BIT
                                    length =                 GetControlBit(&src);
                                    CHECK_CONTROL_BIT
                                    length = (length << 1) | GetControlBit(&src);
                                    CHECK_CONTROL_BIT
                                    length = (length << 1) | GetControlBit(&src);
                                    length += 6;
                                }
                            }
                        }
                    }
                }
                if (dpos < offset) {
                    return -1;
                }
                dpos += length;
                if (dst != NULL) {
                    uint8_t *p = dst - offset;
                    while (length != 0) {
                        *dst = *p;
                        dst++;
                        p++;
                        length--;
                    }
                }
            }
        }
    }
#undef CHECK_CONTROL_BIT
#undef CHECK_SPOS
}

static void Copy(size_t offset, size_t size);
static void AddControlBit(int bit);
static void Flush(void);

static uint8_t* dst_global = NULL;
static size_t cpos = 0;
static size_t dpos = 0;

static void Write8(uint8_t b)
{
    dst_global[dpos++] = b;
}

static void Write16(uint16_t b)
{
    dst_global[dpos++] = b & 0xFF;
    dst_global[dpos++] = b >> 8;
}

size_t LZSS_GetCompressedMaxSize(size_t src_len)
{
    // TODO:
    return 4 + 4             // Заголовок + конец
        + src_len            //
        + (src_len / 8 + 2); // 0xFF for every 8 bytes
}

size_t LZSS_CompressSimple(const uint8_t* src, size_t src_len, uint8_t* dst)
{
    bitPos = 8; // Сначала 8 бит
    controlWord = 0;

    size_t spos = 0;

    dst_global = dst;
    cpos = 2;
    dpos = 2 + cpos; // Skip control bytes
    while (spos < src_len) {
        // TODO: Blocks
        size_t offset = 0;
        size_t length = 0;

        for (intptr_t i = spos - 1; (i >= 0) && (i >= (intptr_t)spos - WINDOW_SIZE_8); i--) {
            if (src[i] == src[spos]) {
                size_t cur_len = 0;
                do {
                    cur_len++;
                } while ((cur_len < MAX_SEQ_8)
                    && (spos + cur_len < src_len)
                    && src[i + cur_len] == src[spos + cur_len]);

                if (cur_len > length) {
                    offset = spos - i;
                    length = cur_len;
                    if (length >= MAX_SEQ_8) {
                        break;
                    }
                }
            }
        }

        size_t rle_length = 0;
        do {
            rle_length++;
        } while ((rle_length < MAX_RLE)
            && (spos + rle_length < src_len)
            && src[spos] == src[spos + rle_length]);

        if (rle_length >= MIN_RLE && rle_length > length) {
            AddControlBit(1);
            AddControlBit(1);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            Write8(0x01);
            size_t a = rle_length - MIN_RLE;
            if (rle_length <= MIN_RLE + 0xF) {
                AddControlBit(0);
                AddControlBit((a >> 3) & 1);
                AddControlBit((a >> 2) & 1);
                AddControlBit((a >> 1) & 1);
                AddControlBit((a >> 0) & 1);
            }
            else {
                AddControlBit(1);
                AddControlBit((a >> 11) & 1);
                AddControlBit((a >> 10) & 1);
                AddControlBit((a >>  9) & 1);
                AddControlBit((a >>  8) & 1);
                Write8(a & 0xFF);
            }
            Write8(src[spos]);
            spos += rle_length;
        }
        else if (length < MIN_SEQ_8) {
            AddControlBit(0);
            Write8(src[spos++]);
        }
        else {
            assert(offset >= 1 && offset <= WINDOW_SIZE_8 && length >= MIN_SEQ_8 && length <= MAX_SEQ_8);
            AddControlBit(1);
            Copy(offset, length);
            spos += length;
        }
    }

    AddControlBit(1);
    AddControlBit(1);
    AddControlBit(0);
    AddControlBit(0);
    AddControlBit(0);
    AddControlBit(0);
    AddControlBit(0);
    Write16(0x0000);
    controlWord >>= (CONTROL_BITS - bitPos);
    Flush();

    dst[0] = (dpos - 3) & 0xFF;
    dst[1] = (dpos - 3) >> 8;

    return dpos - 2; // В Flush dpos += 2
}

// 1 << math.ceil(math.log2(WINDOW_SIZE_8 + 1))
#define QUEUE_SIZE 0x2000
#define QUEUE_MASK (QUEUE_SIZE - 1)
static size_t off_queue[256][QUEUE_SIZE];
static size_t off_queue_i[256];
static size_t off_queue_j[256];

void InitOffsetQueue(void)
{
    memset(off_queue_i, 0x00, sizeof(off_queue_i));
    memset(off_queue_j, 0x00, sizeof(off_queue_j));
}

#define Remove(curb, sp)                                            \
{                                                                   \
    while (off_queue_j[curb] != off_queue_i[curb]                   \
        && sp > WINDOW_SIZE_8                                       \
        && off_queue[curb][off_queue_j[curb]] < sp - WINDOW_SIZE_8) \
    {                                                               \
        off_queue_j[curb]++;                                        \
        off_queue_j[curb] &= QUEUE_MASK;                            \
    }                                                               \
}                                                                   \

#define AddOne(curb, sp)                          \
{                                                 \
    off_queue[curb][off_queue_i[curb]++] = sp;    \
    off_queue_i[curb] &= QUEUE_MASK;              \
    if (off_queue_i[curb] == off_queue_j[curb]) { \
        off_queue_j[curb]++;                      \
        off_queue_j[curb] &= QUEUE_MASK;          \
    }                                             \
}                                                 \

#define AddRange(source, sp, len)                            \
{                                                            \
    for (size_t i = 0; i < len; i++) {                       \
        size_t curbyte = source[sp + i];                     \
        off_queue[curbyte][off_queue_i[curbyte]++] = sp + i; \
        off_queue_i[curbyte] &= QUEUE_MASK;                  \
        if (off_queue_i[curbyte] == off_queue_j[curbyte]) {  \
            off_queue_j[curbyte]++;                          \
            off_queue_j[curbyte] &= QUEUE_MASK;              \
        }                                                    \
    }                                                        \
}                                                            \

size_t LZSS_CompressSimpleFast(const uint8_t* src, size_t src_len, uint8_t* dst, uint16_t* custom_lens)
{
    size_t dpos_total = 0;

    InitOffsetQueue();

    bitPos = 8; // Сначала 8 бит
    controlWord = 0;

    size_t spos = 0;

    dst_global = dst;
    cpos = 2;
    dpos = 2 + cpos; // Skip control bytes
    while (spos < src_len) {
        // TODO: Blocks
        if (dpos >= 0x10000 - 16) {
            AddControlBit(1);
            AddControlBit(1);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            Write8(0x00);
            Write8(0xFF); // Ещё блок
            controlWord >>= (CONTROL_BITS - bitPos);
            Flush();

            dst_global[0] = (dpos - 3) & 0xFF;
            dst_global[1] = (dpos - 3) >> 8;


            InitOffsetQueue();

            bitPos = 8; // Сначала 8 бит
            controlWord = 0;

            src += spos;
            src_len -= spos;
            spos = 0;

            dpos_total += dpos - 2;

            dst_global += dpos - 2;
            cpos = 2;
            dpos = 2 + cpos; // Skip control bytes
        }

        size_t offset = 0;
        size_t length = 0;

        // RemoveOldEntries
        uint8_t curbyte = src[spos];
        Remove(curbyte, spos);

        for (size_t i = off_queue_i[curbyte]; i != off_queue_j[curbyte]; i = (i - 1) & QUEUE_MASK) {
            size_t off = off_queue[curbyte][(i - 1) & QUEUE_MASK];
            assert(src[off] == curbyte);

            size_t cur_len = 0;
            do {
                cur_len++;
            } while ((cur_len < MAX_SEQ_8)
                && (spos + cur_len < src_len)
                && src[off + cur_len] == src[spos + cur_len]);

            if (cur_len > length) {
                offset = spos - off;
                length = cur_len;
                if (length >= MAX_SEQ_8) {
                    break;
                }
            }
        }

        // if (custom_lens != NULL) {
        //     length = custom_lens[spos];
        // }

        size_t rle_length = 0;
        do {
            rle_length++;
        } while ((rle_length < MAX_RLE)
            && (spos + rle_length < src_len)
            && src[spos] == src[spos + rle_length]);

        // if (custom_lens != NULL) {
        //     // TODO: Нужно что-то делать, если rle маленькое
        //     if (rle_length >= MIN_RLE && custom_lens[spos] >= MIN_RLE) {
        //         rle_length = custom_lens[spos];
        //         length = 0;
        //     }
        //     else {
        //         length = custom_lens[spos];
        //         rle_length = 0;
        //     }
        // }

        if (rle_length >= MIN_RLE && rle_length > length) {
            AddRange(src, spos, rle_length);

            AddControlBit(1);
            AddControlBit(1);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            AddControlBit(0);
            Write8(0x01);
            size_t a = rle_length - MIN_RLE;
            if (rle_length <= MIN_RLE + 0xF) {
                AddControlBit(0);
                AddControlBit((a >> 3) & 1);
                AddControlBit((a >> 2) & 1);
                AddControlBit((a >> 1) & 1);
                AddControlBit((a >> 0) & 1);
            }
            else {
                AddControlBit(1);
                AddControlBit((a >> 11) & 1);
                AddControlBit((a >> 10) & 1);
                AddControlBit((a >>  9) & 1);
                AddControlBit((a >>  8) & 1);
                Write8(a & 0xFF);
            }
            Write8(src[spos]);
            spos += rle_length;
        }
        else if (length < MIN_SEQ_8) {
            AddOne(curbyte, spos);

            AddControlBit(0);
            Write8(src[spos++]);
        }
        else {
            assert(offset >= 1 && offset <= WINDOW_SIZE_8 && length >= MIN_SEQ_8 && length <= MAX_SEQ_8);

            AddRange(src, spos, length);

            AddControlBit(1);
            Copy(offset, length);
            spos += length;
        }
    }

    AddControlBit(1);
    AddControlBit(1);
    AddControlBit(0);
    AddControlBit(0);
    AddControlBit(0);
    AddControlBit(0);
    AddControlBit(0);
    Write16(0x0000);
    controlWord >>= (CONTROL_BITS - bitPos);
    Flush();

    // X blocks
    dst_global[0] = (dpos - 3) & 0xFF;
    dst_global[1] = (dpos - 3) >> 8;
    dpos_total += dpos - 2;
    return dpos_total;

    // 1 block
    //dst[0] = (dpos - 3) & 0xFF;
    //dst[1] = (dpos - 3) >> 8;
    //return dpos - 2; // В Flush dpos += 2
}

static void Copy(size_t offset, size_t size)
{
    if (offset <= 255) {
        AddControlBit(0);
        Write8(offset);
    }
    else if (offset >= 256 && offset <= WINDOW_SIZE_8) {
        AddControlBit(1);
        AddControlBit((offset >> 12) & 1);
        AddControlBit((offset >> 11) & 1);
        AddControlBit((offset >> 10) & 1);
        AddControlBit((offset >>  9) & 1);
        AddControlBit((offset >>  8) & 1);
        Write8(offset & 0xFF);
    }
    else {
        printf("BAD offset\n");
    }

    if (size == 2) {
        AddControlBit(1);
    }
    else if (size == 3) {
        AddControlBit(0);
        AddControlBit(1);
    }
    else if (size == 4) {
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(1);
    }
    else if (size == 5) {
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(1);
    }
    else if (size >= 6 && size <= 13) {
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(1);
        size -= 6;
        AddControlBit((size >> 2) & 1);
        AddControlBit((size >> 1) & 1);
        AddControlBit((size >> 0) & 1);
    }
    else if (size >= 14 && size <= 269) {
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(0);
        AddControlBit(0);
        size -= 14;
        Write8(size);
    }
    else {
        printf("BAD length\n");
    }
}

static void AddControlBit(int bit)
{
    if (bitPos == CONTROL_BITS) {
        Flush();
    }
    controlWord >>= 1;
    controlWord |= bit << (CONTROL_BITS - 1);
    bitPos++;
}

static void Flush(void)
{
    dst_global[cpos] = controlWord & 0xFF;
    dst_global[cpos+1] = controlWord >> 8;
    cpos = dpos;
    dpos += 2;
    controlWord = 0;
    bitPos = 0;
}
