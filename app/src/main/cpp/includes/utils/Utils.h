#pragma once

#include "Platform.h"

// NAL Unit
#define NAL_TYPE(data, nal) ((data[nal.start + nal.codeSize] >> 1) & 0x3F)

typedef struct {
        sz_t start;
        sz_t end;
        byte_t codeSize;
} NalUnit;

int_t NalStart(
    const byte_t *data,
    sz_t start,
    sz_t end);

sz_t ExtractNal(
    const byte_t *data,
    sz_t start,
    sz_t end,
    NalUnit *nal,
    sz_t max);

void Base64(
    const byte_t *data,
    sz_t start,
    sz_t end,
    char_t *dst);

bool_t IsNalValid(const NalUnit &nal);