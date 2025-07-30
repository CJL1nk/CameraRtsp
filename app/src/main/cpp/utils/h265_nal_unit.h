#pragma once

#include "jni.h"
#include <array>

struct NalUnit {
public:
    size_t start;
    size_t codeSize;
    size_t end;
    bool isValid() const { return end > start; }
};

static int findNalStart(const uint8_t* data, size_t start, size_t size) {
    for (size_t i = start; i + 3 < size; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01)
                return static_cast<int>(i);

            if (i + 4 <= size && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

template <size_t MaxUnit>
static std::array<NalUnit, MaxUnit> extractNalUnits(const uint8_t *data, size_t start, size_t size) {
    std::array<NalUnit, MaxUnit> nalUnits {};
    size_t nalCount = 0;
    size_t offset = start;
    while (true) {
        int nalStart = findNalStart(data, offset, size);
        if (nalStart == -1) {
            break;
        }
        size_t codeSize = data[nalStart + 2] == 0x01 ? 3 : 4;
        int nalEnd = findNalStart(data, nalStart + codeSize, size);
        if (nalEnd == -1) {
            nalEnd = static_cast<int>(size);
        }
        nalUnits[nalCount].start = nalStart;
        nalUnits[nalCount].end = nalEnd;
        nalUnits[nalCount].codeSize = codeSize;
        ++nalCount;
        if (nalCount >= MaxUnit) {
            break;
        }
        offset = nalEnd;
    }

    return std::move(nalUnits);
}
