#pragma once

#include <aaudio/AAudio.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>

#include "utils/Platform.h"

/* Media */

#define M_RESULT_OK 0

#define M_CODEC_CONFIGURE_ENCODE 1
#define M_KEY_MIME "mime"
#define M_KEY_SAMPLE_RATE "sample-rate"
#define M_KEY_CHANNEL_COUNT "channel-count"
#define M_KEY_BIT_RATE "bitrate"
#define M_KEY_AAC_PROFILE "aac-profile"
#define M_KEY_MAX_INPUT_SIZE "max-input-size"
#define M_KEY_WIDTH "width"
#define M_KEY_HEIGHT "height"
#define M_KEY_COLOR_FORMAT "color-format"
#define M_KEY_I_FRAME_INTERVAL "i-frame-interval"
#define M_KEY_FRAME_RATE "frame-rate"
#define M_KEY_PROFILE "profile"
#define M_KEY_LEVEL "level"

#define M_INFO_FLAG_KEY_FRAME 1
#define M_INFO_FLAG_CODEC_CONFIG 2
#define M_INFO_FLAG_END_OF_STREAM 4

typedef AMediaCodec M_Codec;
typedef AMediaFormat M_Format;
typedef ANativeWindow M_Window;
typedef AMediaCrypto M_Crypto;
typedef AMediaCodecBufferInfo M_BufferInfo;
typedef int_t result_t;

static inline M_Codec* M_CreateEncoder(const char* mime_type) {
    return AMediaCodec_createEncoderByType(mime_type);
}
static inline M_Format* M_NewFormat() {
    return AMediaFormat_new();
}
static inline void M_SetString(M_Format* format, const char* name, const char* value) {
    AMediaFormat_setString(format, name, value);
}
static inline void M_SetInt32(M_Format* format, const char* name, int_t value) {
    AMediaFormat_setInt32(format, name, value);
}
static inline result_t M_Configure(
        M_Codec* codec,
        M_Format* format,
        M_Window* surface,
        M_Crypto* crypto,
        int_t flags) {
    return AMediaCodec_configure(codec, format, surface, crypto, flags);
}
static inline result_t M_Start(M_Codec* codec) {
    return AMediaCodec_start(codec);
}
static inline result_t M_Stop(M_Codec* codec) {
    return AMediaCodec_stop(codec);
}
static inline void M_Delete(M_Codec* codec) {
    AMediaCodec_delete(codec);
}
static inline void M_Delete(M_Format* format) {
    AMediaFormat_delete(format);
}
static inline ssz_t M_DequeueInput(M_Codec* codec, tm_t timeoutUs) {
    return AMediaCodec_dequeueInputBuffer(codec, timeoutUs);
}
static inline byte_t* M_InputBuffer(M_Codec* codec, sz_t idx, sz_t* out_size) {
    return AMediaCodec_getInputBuffer(codec, idx, out_size);
}
static inline result_t M_QueueInput(
        M_Codec* codec,
        sz_t idx,
        sz_t offset,
        sz_t size,
        tm_t time,
        int_t flags) {
    return AMediaCodec_queueInputBuffer(codec, idx, offset, size, time, flags);
}
static inline ssz_t M_DequeueOutput(M_Codec* codec, M_BufferInfo* info, tm_t timeoutUs) {
    return AMediaCodec_dequeueOutputBuffer(codec, info, timeoutUs);
}
static inline byte_t* M_OutputBuffer(M_Codec* codec, sz_t idx, sz_t* out_size) {
    return AMediaCodec_getOutputBuffer(codec, idx, out_size);
}
static inline result_t M_ReleaseOutput(M_Codec* codec, sz_t idx, bool_t render) {
    return AMediaCodec_releaseOutputBuffer(codec, idx, render);
}

/* Audio */

#define M_AUDIO_DIRECTION_INPUT 1
#define M_AUDIO_FORMAT_PCM_I16 1
#define M_AUDIO_PERFORMANCE_MODE_LOW_LATENCY 12
#define M_AUDIO_CALLBACK_RESULT_CONTINUE 0
#define M_AUDIO_CALLBACK_RESULT_STOP 1

typedef AAudioStream M_AStream;
typedef AAudioStreamBuilder M_ABuilder;

typedef result_t (*M_ADataCallback)(
        M_AStream *stream,
        void *userData,
        void *audioData,
        int_t numSamples
);

// Platform wrapper functions (to be implemented per platform)
static inline result_t M_CreateBuilder(M_ABuilder** builder) {
    return AAudio_createStreamBuilder(builder);
}
static inline void M_SetDirection(M_ABuilder* builder, int_t direction) {
    AAudioStreamBuilder_setDirection(builder, direction);
}
static inline void M_SetSampleRate(M_ABuilder* builder, int_t sampleRate) {
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
}
static inline void M_SetChannelCount(M_ABuilder* builder, int_t channelCount) {
    AAudioStreamBuilder_setChannelCount(builder, channelCount);
}
static inline void M_SetFormat(M_ABuilder* builder, int_t format) {
    AAudioStreamBuilder_setFormat(builder, format);
}
static inline void M_SetPerformanceMode(M_ABuilder* builder, int_t mode) {
    AAudioStreamBuilder_setPerformanceMode(builder, mode);
}
static inline void M_SetDataCallback(M_ABuilder* builder, M_ADataCallback callback, void* userData) {
    AAudioStreamBuilder_setDataCallback(builder, callback, userData);
}
static inline result_t M_OpenStream(M_ABuilder* builder, M_AStream** stream) {
    return AAudioStreamBuilder_openStream(builder, stream);
}
static inline void M_Delete(M_ABuilder* builder) {
    AAudioStreamBuilder_delete(builder);
}
static inline result_t M_RequestStart(M_AStream* stream) {
    return AAudioStream_requestStart(stream);
}
static inline result_t M_RequestStop(M_AStream* stream) {
    return AAudioStream_requestStop(stream);
}
static inline result_t M_Close(M_AStream* stream) {
    return AAudioStream_close(stream);
}

/* Camera */

#define M_TEMPLATE_PREVIEW 1
#define M_FORMAT_YUV_420_888 0x23
#define M_COLOR_FORMAT_SURFACE 0x7F000789

typedef ACameraManager M_CManager;
typedef ACameraDevice M_CDevice;
typedef ACameraCaptureSession M_CSession;
typedef ACameraDevice_StateCallbacks M_CStateCb;
typedef ACaptureRequest M_CRequest;
typedef ACameraOutputTarget M_CTarget;
typedef ACaptureSessionOutput M_COutput;
typedef ACaptureSessionOutputContainer M_CContainer;
typedef AImageReader M_ImageReader;
typedef AImage M_Image;

typedef struct M_CStateCallbacks {
    void *context;
    void (*onClosed)(void *context, M_CSession *session);
} M_CStateCallbacks;

typedef struct M_ImageListener {
    void *context;
    void (*onImageAvailable)(void *context, M_ImageReader *reader);
} M_ImageListener;

static inline M_CManager *M_CreateManager() {
    return ACameraManager_create();
}
static inline void M_DeleteManager(M_CManager *manager) {
    ACameraManager_delete(manager);
}
static inline result_t M_OpenCamera(
        M_CManager *manager,
        const char_t *id,
        M_CStateCb *callbacks,
        M_CDevice **device) {
    return ACameraManager_openCamera(
            manager,
            id,
            callbacks,
            device);
}
static inline void M_CloseCamera(M_CDevice *device) {
    ACameraDevice_close(device);
}
static inline result_t M_CreateRequest(
        M_CDevice *device,
        int_t template_type,
        M_CRequest **request) {
    return ACameraDevice_createCaptureRequest(device, (ACameraDevice_request_template)template_type, request);
}
static inline void M_FreeRequest(M_CRequest *request) {
    ACaptureRequest_free(request);
}
static inline result_t M_CreateSurface(M_Codec *codec, M_Window **surface) {
    return AMediaCodec_createInputSurface(codec, surface);
}
static inline result_t M_SetFpsRange(M_CRequest *request, const int_t *range) {
    return ACaptureRequest_setEntry_i32(request, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, range);
}
static inline result_t M_CreateTarget(M_Window *window, M_CTarget **target) {
    return ACameraOutputTarget_create(window, target);
}
static inline void M_FreeTarget(M_CTarget *target) {
    ACameraOutputTarget_free(target);
}
static inline result_t M_AddTarget(M_CRequest *request, M_CTarget *target) {
    return ACaptureRequest_addTarget(request, target);
}
static inline result_t M_CreateContainer(M_CContainer **container) {
    return ACaptureSessionOutputContainer_create(container);
}
static inline result_t M_CreateOutput(M_Window *window, M_COutput **output) {
    return ACaptureSessionOutput_create(window, output);
}
static inline result_t M_AddOutput(M_CContainer *container, M_COutput *output) {
    return ACaptureSessionOutputContainer_add(container, output);
}
static inline result_t M_CreateSession(
        M_CDevice *device,
        M_CContainer *container,
        M_CStateCallbacks *callbacks,
        M_CSession **session) {

    ACameraCaptureSession_stateCallbacks cb = {
        .context = callbacks->context,
        .onClosed = callbacks->onClosed,
        .onReady = {},
        .onActive = {}};
    return ACameraDevice_createCaptureSession(device, container, &cb, session);
}
static inline result_t M_SetRepeating(M_CSession *session, M_CRequest *request) {
    return ACameraCaptureSession_setRepeatingRequest(session, nullptr, 1, &request, nullptr);
}
static inline result_t M_StopRepeating(M_CSession *session) {
    return ACameraCaptureSession_stopRepeating(session);
}
static inline result_t M_AbortCaptures(M_CSession *session) {
    return ACameraCaptureSession_abortCaptures(session);
}
static inline void M_CloseSession(M_CSession *session) {
    ACameraCaptureSession_close(session);
}

/* Image Reader */
static inline result_t M_CreateReader(
        int_t width,
        int_t height,
        int_t format,
        int_t maxImages,
        M_ImageReader **reader) {
    return AImageReader_new(width, height, format, maxImages, reader);
}
static inline void M_DeleteReader(M_ImageReader *reader) {
    AImageReader_delete(reader);
}
static inline result_t M_GetWindow(M_ImageReader *reader, M_Window **window) {
    return AImageReader_getWindow(reader, window);
}
static inline void M_SetListener(M_ImageReader *reader, M_ImageListener *listener) {
    struct AImageReader_ImageListener cb{
        .context = listener->context,
        .onImageAvailable = listener->onImageAvailable};
    AImageReader_setImageListener(reader, &cb);
}
static inline result_t M_AcquireImage(M_ImageReader *reader, M_Image **image) {
    return AImageReader_acquireNextImage(reader, image);
}
static inline void M_DeleteImage(M_Image *image) {
    AImage_delete(image);
}
static inline result_t M_SignalEOS(M_Codec *codec) {
    return AMediaCodec_signalEndOfInputStream(codec);
}
