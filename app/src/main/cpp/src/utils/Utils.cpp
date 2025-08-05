#include "utils/Utils.h"

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

void Base64(
    const byte_t *data,
    sz_t start,
    sz_t end,
    char_t *dst) {

    sz_t len = end - start;
    sz_t i = 0;
    sz_t o = 0;

    while (i + 2 < len) {
        int_t val = (data[start + i] << 16) |
                    (data[start + i + 1] << 8) |
                    data[start + i + 2];
        dst[o++] = kBase64Table[(val >> 18) & 0x3F];
        dst[o++] = kBase64Table[(val >> 12) & 0x3F];
        dst[o++] = kBase64Table[(val >> 6) & 0x3F];
        dst[o++] = kBase64Table[val & 0x3F];
        i += 3;
    }

    // Handle remainder
    if (i < len) {
        int_t val = data[start + i] << 16;
        if (i + 1 < len)
            val |= data[start + i + 1] << 8;

        dst[o++] = kBase64Table[(val >> 18) & 0x3F];
        dst[o++] = kBase64Table[(val >> 12) & 0x3F];
        dst[o++] = (i + 1 < len) ? kBase64Table[(val >> 6) & 0x3F] : '=';
        dst[o++] = '=';
    }

    dst[o] = '\0'; // null-terminate if needed
}

// Find 00 00 01 or 00 00 00 01
int_t NalStart(
    const byte_t *data,
    sz_t start,
    sz_t end) {

    for (sz_t i = start; i + 3 < end; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01)
                return static_cast<int_t>(i);

            if (i + 4 <= end &&
                data[i + 2] == 0x00 &&
                data[i + 3] == 0x01) {
                return static_cast<int_t>(i);
            }
        }
    }
    return -1;
}

sz_t ExtractNal(
    const byte_t *data,
    sz_t start,
    sz_t end,
    NalUnit *nal,
    sz_t max) {

    sz_t count;
    sz_t offset;
    sz_t codeSize;

    int_t tempStart;
    sz_t nalStart;

    int_t tempEnd;
    sz_t nalEnd;

    if (max <= 0)
        return 0;

    count = 0;
    offset = start;

    while (true) {
        // Find start position
        tempStart = NalStart(data, offset, end);
        if (tempStart == -1) {
            break;
        }
        nalStart = tempStart;

        // Find code size
        codeSize = data[nalStart + 2] == 0x01 ? 3 : 4;

        // Find end position (next start or end of stream)
        tempEnd = NalStart(data, nalStart + codeSize, end);
        if (tempEnd == -1) {
            nalEnd = end;
        }
        else {
            nalEnd = tempEnd;
        }

        nal[count].start = nalStart;
        nal[count].end = nalEnd;
        nal[count].codeSize = codeSize;
        ++count;

        if (count >= max) {
            break;
        }
        offset = nalEnd;
    }
    return count;
}

bool_t IsNalValid(const NalUnit &nal) {
    return nal.start < nal.end && nal.codeSize > 0;
}