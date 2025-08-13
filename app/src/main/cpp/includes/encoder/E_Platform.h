#pragma once

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include "utils/Platform.h"

#define E_RESULT_OK 0

#define E_CODEC_CONFIGURE_ENCODE 1
#define E_KEY_MIME "mime"
#define E_KEY_SAMPLE_RATE "sample-rate"
#define E_KEY_CHANNEL_COUNT "channel-count"
#define E_KEY_BIT_RATE "bitrate"
#define E_KEY_AAC_PROFILE "aac-profile"
#define E_KEY_MAX_INPUT_SIZE "max-input-size"
#define E_KEY_WIDTH "width"
#define E_KEY_HEIGHT "height"
#define E_KEY_COLOR_FORMAT "color-format"
#define E_KEY_I_FRAME_INTERVAL "i-frame-interval"
#define E_KEY_FRAME_RATE "frame-rate"
#define E_KEY_PROFILE "profile"
#define E_KEY_LEVEL "level"

#define E_COLOR_FORMAT_SURFACE 0x7F000789


#define E_INFO_FLAG_KEY_FRAME 1
#define E_INFO_FLAG_CODEC_CONFIG 2
#define E_INFO_FLAG_END_OF_STREAM 4

typedef AMediaCodec E_Codec;
typedef AMediaFormat E_Format;
typedef ANativeWindow E_Window;
typedef AMediaCrypto E_Crypto;
typedef AMediaCodecBufferInfo E_BufferInfo;
typedef int_t result_t;

static inline E_Codec* E_CreateEncoder(const char* mime_type) {
    return AMediaCodec_createEncoderByType(mime_type);
}
static inline E_Format* E_NewFormat() {
    return AMediaFormat_new();
}
static inline void E_SetString(E_Format* format, const char* name, const char* value) {
    AMediaFormat_setString(format, name, value);
}
static inline void E_SetInt32(E_Format* format, const char* name, int_t value) {
    AMediaFormat_setInt32(format, name, value);
}
static inline result_t E_Configure(
        E_Codec* codec,
        E_Format* format,
        E_Window* surface,
        E_Crypto* crypto,
        int_t flags) {
    return AMediaCodec_configure(codec, format, surface, crypto, flags);
}
static inline result_t E_CreateSurface(E_Codec *codec, E_Window **surface) {
    return AMediaCodec_createInputSurface(codec, surface);
}
static inline result_t E_Start(E_Codec* codec) {
    return AMediaCodec_start(codec);
}
static inline result_t E_Stop(E_Codec* codec) {
    return AMediaCodec_stop(codec);
}
static inline void E_Delete(E_Codec* codec) {
    AMediaCodec_delete(codec);
}
static inline void E_Delete(E_Format* format) {
    AMediaFormat_delete(format);
}
static inline ssz_t E_DequeueInput(E_Codec* codec, tm_t timeoutUs) {
    return AMediaCodec_dequeueInputBuffer(codec, timeoutUs);
}
static inline byte_t* E_InputBuffer(E_Codec* codec, sz_t idx, sz_t* out_size) {
    return AMediaCodec_getInputBuffer(codec, idx, out_size);
}
static inline result_t E_QueueInput(
        E_Codec* codec,
        sz_t idx,
        sz_t offset,
        sz_t size,
        tm_t time,
        int_t flags) {
    return AMediaCodec_queueInputBuffer(codec, idx, offset, size, time, flags);
}
static inline ssz_t E_DequeueOutput(E_Codec* codec, E_BufferInfo* info, tm_t timeoutUs) {
    return AMediaCodec_dequeueOutputBuffer(codec, info, timeoutUs);
}
static inline byte_t* E_OutputBuffer(E_Codec* codec, sz_t idx, sz_t* out_size) {
    return AMediaCodec_getOutputBuffer(codec, idx, out_size);
}
static inline result_t E_ReleaseOutput(E_Codec* codec, sz_t idx, bool_t render) {
    return AMediaCodec_releaseOutputBuffer(codec, idx, render);
}
static inline result_t E_SignalEOS(E_Codec *codec) {
    return AMediaCodec_signalEndOfInputStream(codec);
}
