#include "encoder/E_H265.h"
#include "utils/Utils.h"

#define LOG_TAG "H265Encoder"

// Forward declarations
static bool StartCodec(E_H265 &encoder);
static void EncodingLoop(E_H265 &encoder);
static void HandleEncoded(E_H265 &encoder,
                          const byte_t *data,
                          sz_t size,
                          tm_t presentation_time_us,
                          int_t flags,
                          FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);
static void ParseParams(E_H265 &encoder, const byte_t *data, sz_t size);
static void MarkStopped(E_H265 &encoder);
static void CleanUp(E_H265 &encoder);

static void StopEncoding(void* context) {
    auto *encoder = static_cast<E_H265 *>(context);
    if (encoder && encoder->codec) {
        E_SignalEOS(encoder->codec);
    }
}

static void StartEncoding(E_H265 &encoder) {
    SetThreadName("H265Encoder");
    M_VFrameListener cb {
        .frameCallback = nullptr,
        .closedCallback = StopEncoding,
        .context = &encoder,
    };
    M_AddListener(*encoder.source, cb, &encoder);

    LOGI(LOG_TAG, "Video encoder started successfully");
    EncodingLoop(encoder);

    M_RemoveListener(*encoder.source, &encoder);

    CleanUp(encoder);
}

static void *StartEncodingThread(void *arg) {
    auto *encoder = static_cast<E_H265 *>(arg);
    if (encoder) {
        StartEncoding(*encoder);
    }
    return nullptr;
}

static void Reset(E_H265 &encoder) {
    Reset(encoder.vps, sizeof(encoder.vps));
    Reset(encoder.sps, sizeof(encoder.sps));
    Reset(encoder.pps, sizeof(encoder.pps));
    Store(&encoder.params_initialized, false);
}

void E_Init(E_H265 &encoder, M_VideoSource* source) {
    // We mustn't reset listeners every session.
    // Listener should add and remove itself manually.
    for (auto & listener : encoder.listeners) {
        listener.callback = nullptr;
        listener.context = nullptr;
    }

    // Initialize synchronization primitives
    Init(&encoder.listener_lock);

    // Initialize threading
    Init(&encoder.thread);
    Init(&encoder.is_recording);
    Init(&encoder.is_stopping);

    // Initialize param locks
    Init(&encoder.params_lock);
    Init(&encoder.params_cond);

    // Initialize pointers
    encoder.codec = nullptr;
    encoder.format = nullptr;
    encoder.encoder_window = nullptr;

    // Initialize source
    encoder.source = source;
}

E_Window* E_Start(E_H265 &encoder) {
    if (Load(&encoder.is_stopping)) {
        return encoder.encoder_window;
    }

    if (GetAndSet(&encoder.is_recording, true)) {
        return encoder.encoder_window; // Already running
    }

    Reset(encoder);

    if (!StartCodec(encoder)) {
        LOGE(LOG_TAG, "Failed to initialize H265 encoder");
        MarkStopped(encoder);
        CleanUp(encoder);
        return encoder.encoder_window;
    }

    Start(&encoder.thread, StartEncodingThread, &encoder);

    return encoder.encoder_window;
}

void E_Stop(E_H265 &encoder) {
    if (!Load(&encoder.is_recording)) {
        return;
    }
    if (GetAndSet(&encoder.is_stopping, true)) {
        return;
    }
    Join(&encoder.thread);
    MarkStopped(encoder);
}

bool E_AddListener(E_H265 &encoder,
                   E_H265FrameCallback callback,
                   void *ctx) {
    bool_t success = false;

    Lock(&encoder.listener_lock);
    for (auto & listener : encoder.listeners) {
        if (listener.callback == nullptr &&
            listener.context == nullptr) {

            listener.callback = callback;
            listener.context = ctx;
            success = true;
            break;
        }
    }
    Unlock(&encoder.listener_lock);

    // Notify video source to start passing frames
    if (success && encoder.source) {
        M_StartEncoder(*encoder.source);
    }
    return success;
}

bool E_RemoveListener(E_H265 &encoder, void *ctx) {
    bool_t empty ;
    bool_t success = false;

    Lock(&encoder.listener_lock);
    for (auto & listener : encoder.listeners) {
        if (listener.context == ctx) {
            listener.callback = nullptr;
            listener.context = nullptr;
            success = true;
            break;
        }
    }

    for (auto & listener : encoder.listeners) {
        if (listener.context) {
            empty = false;
            break;
        }
    }

    Unlock(&encoder.listener_lock);

    // Notify video source to stop passing frames
    if (empty && encoder.source) {
        M_StopEncoder(*encoder.source);
    }
    return success;
}

void E_GetParams(E_H265 &encoder, char *vps, char *sps, char *pps) {
    // Lock until params are available
    Lock(&encoder.params_lock);
    while (!Load(&encoder.params_initialized)) {
        Wait(&encoder.params_cond, &encoder.params_lock);
    }
    Unlock(&encoder.params_lock);

    if (vps)
        Copy(vps, encoder.vps, H265_PARAMS_SIZE);
    if (sps)
        Copy(sps, encoder.sps, H265_PARAMS_SIZE);
    if (pps)
        Copy(pps, encoder.pps, H265_PARAMS_SIZE);
}

static bool StartCodec(E_H265 &encoder) {
    result_t result;

    // Create HEVC encoder
    encoder.codec = E_CreateEncoder("video/hevc");
    if (!encoder.codec) {
        LOGE(LOG_TAG, "Failed to create HEVC encoder");
        return false;
    }

    // Create media format
    encoder.format = E_NewFormat();
    E_SetString(encoder.format, E_KEY_MIME, "video/hevc");
    E_SetInt32(encoder.format, E_KEY_WIDTH, VIDEO_WIDTH);
    E_SetInt32(encoder.format, E_KEY_HEIGHT, VIDEO_HEIGHT);
    E_SetInt32(encoder.format, E_KEY_COLOR_FORMAT, E_COLOR_FORMAT_SURFACE);
    E_SetInt32(encoder.format, E_KEY_BIT_RATE, VIDEO_BIT_RATE);
    E_SetInt32(encoder.format, E_KEY_I_FRAME_INTERVAL, VIDEO_IFRAME_INTERVAL);
    E_SetInt32(encoder.format, E_KEY_FRAME_RATE, VIDEO_DEFAULT_FRAME_RATE);
    E_SetInt32(encoder.format, E_KEY_PROFILE, VIDEO_CODEC_PROFILE);
    E_SetInt32(encoder.format, E_KEY_LEVEL, VIDEO_CODEC_LEVEL);

    // Configure encoder
    result = E_Configure(
            encoder.codec,
            encoder.format,
            nullptr,
            nullptr,
            E_CODEC_CONFIGURE_ENCODE);
    if (result != E_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to configure encoder: %d", result);
        return false;
    }

    // Create input surface
    result = E_CreateSurface(encoder.codec, &encoder.encoder_window);
    if (result != E_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create input surface: %d", result);
        return false;
    }

    result = E_Start(encoder.codec);
    if (result != E_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to start encoder: %d", result);
        return false;
    }
    return true;
}

static void EncodingLoop(E_H265 &encoder) {
    bool finish = false;
    ssz_t output_idx;
    sz_t output_size;
    byte_t *output_buffer;
    FrameBuffer<MAX_VIDEO_FRAME_SIZE> frame;

    while (!finish) {
        // Wait max 100ms for encoder finish,
        // timeout 0 = busy-waiting -> cost CPU
        output_idx = E_DequeueOutput(
                encoder.codec,
                &encoder.buffer_info,
                100000);

        if (output_idx >= 0) {
            output_buffer = E_OutputBuffer(
                    encoder.codec,
                    (sz_t)output_idx,
                    &output_size);

            if (output_buffer && encoder.buffer_info.size > 0) {
                HandleEncoded(encoder,
                              output_buffer,
                              encoder.buffer_info.size,
                              encoder.buffer_info.presentationTimeUs,
                              static_cast<int_t>(encoder.buffer_info.flags),
                              frame);
            }

            E_ReleaseOutput(encoder.codec, (sz_t)output_idx, false);

            if (encoder.buffer_info.flags & E_INFO_FLAG_END_OF_STREAM) {
                finish = true;
            }
        }
    }
}

static void HandleEncoded(E_H265 &encoder,
                          const byte_t *data,
                          sz_t size,
                          tm_t presentation_time_us,
                          int_t flags,
                          FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame) {

    if (flags & E_INFO_FLAG_CODEC_CONFIG) {
        ParseParams(encoder, data, size);
        return;
    }

    if (size <= MAX_VIDEO_FRAME_SIZE) {
        Reset(frame);
        Copy(frame.data, data, size);
        frame.size = size;
        frame.flags = flags;
        frame.timeUs = presentation_time_us;

        {
            Lock(&encoder.listener_lock);
            for (auto & listener : encoder.listeners) {
                if (listener.callback != nullptr &&
                    listener.context != nullptr) {
                    listener.callback(
                            listener.context,
                            frame);
                }
            }
            Unlock(&encoder.listener_lock);
        }
    }
    else {
        LOGE(LOG_TAG, "Frame size is too large: %zu, skipped", size);
    }
}

static void ParseParams(E_H265 &encoder, const byte_t *data, sz_t size) {
    NalUnit nals[3];
    NalUnit nal;
    int_t nal_type;
    char_t *dst;
    sz_t count = ExtractNal(data, 0, size, nals, 3);

    for (sz_t i = 0; i < count; ++i) {
        nal = nals[i];
        nal_type = NAL_TYPE(data, nal);
        dst = nullptr;
        if (nal_type == 32) {
            dst = encoder.vps;
        }
        else if (nal_type == 33) {
            dst = encoder.sps;
        }
        else if (nal_type == 34) {
            dst = encoder.pps;
        }
        if (dst != nullptr) {
            Base64(data, nal.start + nal.codeSize, nal.end, dst);
        }
    }
    Store(&encoder.params_initialized, true);
    Signal(&encoder.params_cond);
}

static void MarkStopped(E_H265 &encoder) {
    Store(&encoder.is_recording, false);
    Store(&encoder.is_stopping, false);
}

static void CleanUp(E_H265 &encoder) {
    if (encoder.codec) {
        E_Stop(encoder.codec);
        E_Delete(encoder.codec);
        encoder.codec = nullptr;
    }

    if (encoder.format) {
        E_Delete(encoder.format);
        encoder.format = nullptr;
    }

    LOGI("CleanUp", "gracefully clean up H265 encoder");
}