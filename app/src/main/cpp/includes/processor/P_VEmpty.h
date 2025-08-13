#pragma once

#include "mediasource/M_VideoSource.h"

typedef struct {
    M_VideoSource* source;
    M_Image* image;
} P_VEmpty;

void P_Init(P_VEmpty &processor, M_VideoSource* source) {
    processor.source = source;
}

static void ProcessFrame(void *context, M_ImageReader *reader) {
    auto *source = (P_VEmpty *)context;
    if (!source) return;

    if (M_AcquireImage(reader, &source->image) != M_RESULT_OK)
        return;

    // TODO: Do something fun later here

    M_DeleteImage(source->image);
}

void P_Start(P_VEmpty &processor) {
    if (processor.source) {
        M_VFrameListener cb {
                .frameCallback = ProcessFrame,
                .closedCallback = nullptr,
                .context = &processor,
        };
        M_AddListener(*processor.source, cb, &processor);
    }
}

void P_Stop(P_VEmpty &processor) {
    if (processor.source) {
        M_RemoveListener(*processor.source, &processor);
    }
}