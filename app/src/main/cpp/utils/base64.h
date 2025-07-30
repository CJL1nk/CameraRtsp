#pragma once

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

#include <cstddef>
#include <cstdint>

static void convertBase64(const uint8_t* data, size_t start, size_t end, char* dst) {
    size_t input_len = end - start;
    size_t i = 0;
    size_t o = 0;

    while (i + 2 < input_len) {
        uint32_t val = (data[start + i] << 16) |
                       (data[start + i + 1] << 8) |
                       data[start + i + 2];
        dst[o++] = kBase64Table[(val >> 18) & 0x3F];
        dst[o++] = kBase64Table[(val >> 12) & 0x3F];
        dst[o++] = kBase64Table[(val >> 6) & 0x3F];
        dst[o++] = kBase64Table[val & 0x3F];
        i += 3;
    }

    // Handle remainder
    if (i < input_len) {
        uint32_t val = data[start + i] << 16;
        if (i + 1 < input_len)
            val |= data[start + i + 1] << 8;

        dst[o++] = kBase64Table[(val >> 18) & 0x3F];
        dst[o++] = kBase64Table[(val >> 12) & 0x3F];
        dst[o++] = (i + 1 < input_len) ? kBase64Table[(val >> 6) & 0x3F] : '=';
        dst[o++] = '=';
    }

    dst[o] = '\0';  // null-terminate if needed
}
