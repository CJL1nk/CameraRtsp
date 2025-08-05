#include "media/M_VideoSource.h"
#include "media/M_Platform.h"
#include "utils/Utils.h"

#define LOG_TAG "VideoSource"

// Forward declarations
static bool InitEncoder(M_VideoSource &source);
static bool InitReader(M_VideoSource &source);
static bool InitCamera(M_VideoSource &source);
static void EncodingLoop(M_VideoSource &source);
static void HandleEncoded(M_VideoSource &source,
                           const byte_t *data,
                           sz_t size,
                           tm_t presentation_time_us,
                           int_t flags,
                           FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);
static void ParseParams(M_VideoSource &source, const byte_t *data, sz_t size);
static void MarkStopped(M_VideoSource &source);
static void CleanUp(M_VideoSource &source);
static void ProcessFrame(M_VideoSource &source, M_ImageReader *reader);
static void StopCapture(M_VideoSource &source);
static void OnImageAvailable(void *context, M_ImageReader *reader);
static void OnCaptureStop(void *context, M_CSession *session);

static void StartEncoding(M_VideoSource &source) {
    SetThreadName("VideoSource");

    if (!InitEncoder(source)) {
        LOGE(LOG_TAG, "Failed to initialize encoder");
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    if (!InitReader(source)) {
        LOGE(LOG_TAG, "Failed to initialize image reader");
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    if (!InitCamera(source)) {
        LOGE(LOG_TAG, "Failed to initialize camera");
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    LOGI(LOG_TAG, "Video source started successfully");
    EncodingLoop(source);

    CleanUp(source);
}

static void *StartEncodingThread(void *arg) {
    auto *source = static_cast<M_VideoSource *>(arg);
    if (source) {
        StartEncoding(*source);
    }
    return nullptr;
}

static void Reset(M_VideoSource &source) {
    Reset(source.vps, sizeof(source.vps));
    Reset(source.sps, sizeof(source.sps));
    Reset(source.pps, sizeof(source.pps));
    Store(&source.params_initialized, false);
}

void M_Init(M_VideoSource &source) {
    // We mustn't reset listeners every session.
    // Listener should add and remove itself manually.
    for (auto & listener : source.listeners) {
        listener.callback = nullptr;
        listener.context = nullptr;
    }

    // Initialize synchronization primitives
    Init(&source.listener_lock);

    // Initialize listeners
    source.image_listener.context = &source;
    source.image_listener.onImageAvailable = OnImageAvailable;
    source.camera_callbacks.context = &source;
    source.camera_callbacks.onClosed = OnCaptureStop;

    // Initialize threading
    Init(&source.thread);
    Init(&source.is_recording);
    Init(&source.is_stopping);

    // Initialize pointers
    source.encoder = nullptr;
    source.format = nullptr;
    source.encoder_window = nullptr;
    source.image_reader = nullptr;
    source.reader_window = nullptr;
    source.image = nullptr;
    source.camera_manager = nullptr;
    source.camera_device = nullptr;
    source.camera_session = nullptr;
    source.camera_request = nullptr;
    source.reader_target = nullptr;
    source.encoder_target = nullptr;
}

void M_Start(M_VideoSource &source) {
    if (Load(&source.is_stopping)) {
        return;
    }

    if (GetAndSet(&source.is_recording, true)) {
        return; // Already running
    }

    Reset(source);
    Start(&source.thread, StartEncodingThread, &source);
}

void M_Stop(M_VideoSource &source) {
    if (!Load(&source.is_recording)) {
        return;
    }
    if (GetAndSet(&source.is_stopping, true)) {
        return;
    }
    StopCapture(source);

    Join(&source.thread);
    MarkStopped(source);
}

bool M_AddListener(M_VideoSource &source, M_VEncodedCallback callback, void *ctx) {
    Lock(&source.listener_lock);
    for (auto & listener : source.listeners) {
        if (listener.callback == nullptr &&
            listener.context == nullptr) {
            
            listener.callback = callback;
            listener.context = ctx;
            Unlock(&source.listener_lock);
            return true;
        }
    }
    Unlock(&source.listener_lock);
    return false;
}

bool M_RemoveListener(M_VideoSource &source, void *ctx) {
    Lock(&source.listener_lock);
    for (auto & listener : source.listeners) {
        
        if (listener.context == ctx) {
            listener.callback = nullptr;
            listener.context = nullptr;
            Unlock(&source.listener_lock);
            return true;
        }
    }
    Unlock(&source.listener_lock);
    return false;
}

bool M_ParamsReady(M_VideoSource &source) {
    return Load(&source.params_initialized);
}

void M_GetParams(M_VideoSource &source, char *vps, char *sps, char *pps) {
    if (vps)
        Copy(vps, source.vps, H265_PARAMS_SIZE);
    if (sps)
        Copy(sps, source.sps, H265_PARAMS_SIZE);
    if (pps)
        Copy(pps, source.pps, H265_PARAMS_SIZE);
}

static bool InitEncoder(M_VideoSource &source) {
    result_t result;

    // Create HEVC encoder
    source.encoder = M_CreateEncoder("video/hevc");
    if (!source.encoder) {
        LOGE(LOG_TAG, "Failed to create HEVC encoder");
        return false;
    }

    // Create media format
    source.format = M_NewFormat();
    M_SetString(source.format, M_KEY_MIME, "video/hevc");
    M_SetInt32(source.format, M_KEY_WIDTH, VIDEO_WIDTH);
    M_SetInt32(source.format, M_KEY_HEIGHT, VIDEO_HEIGHT);
    M_SetInt32(source.format, M_KEY_COLOR_FORMAT, M_COLOR_FORMAT_SURFACE);
    M_SetInt32(source.format, M_KEY_BIT_RATE, VIDEO_BIT_RATE);
    M_SetInt32(source.format, M_KEY_I_FRAME_INTERVAL, VIDEO_IFRAME_INTERVAL);
    M_SetInt32(source.format, M_KEY_FRAME_RATE, VIDEO_DEFAULT_FRAME_RATE);
    M_SetInt32(source.format, M_KEY_PROFILE, VIDEO_CODEC_PROFILE);
    M_SetInt32(source.format, M_KEY_LEVEL, VIDEO_CODEC_LEVEL);

    // Configure encoder
    result = M_Configure(
        source.encoder,
        source.format,
        nullptr,
        nullptr,
        M_CODEC_CONFIGURE_ENCODE);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to configure encoder: %d", result);
        return false;
    }

    // Create input surface
    result = M_CreateSurface(source.encoder, &source.encoder_window);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create input surface: %d", result);
        return false;
    }

    // Start encoder
    result = M_Start(source.encoder);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to start encoder: %d", result);
        return false;
    }
    return true;
}

static bool InitReader(M_VideoSource &source) {
    result_t result;
    result = M_CreateReader(VIDEO_WIDTH,
                            VIDEO_HEIGHT,
                            M_FORMAT_YUV_420_888,
                            IMAGE_READER_CACHE_SIZE,
                            &source.image_reader);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create image reader");
        return false;
    }

    result = M_GetWindow(source.image_reader, &source.reader_window);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create image reader window");
        return false;
    }

    M_SetListener(source.image_reader, &source.image_listener);
    return true;
}

static bool InitCamera(M_VideoSource &source) {
    result_t result;
    M_CContainer *container;
    M_COutput *encoder_output;
    M_COutput *reader_output;
    static const int_t fps_range[] = {VIDEO_MIN_FRAME_RATE, VIDEO_DEFAULT_FRAME_RATE};

    source.camera_manager = M_CreateManager();
    if (!source.camera_manager) {
        LOGE(LOG_TAG, "Failed to create camera manager");
        return false;
    }

    M_CStateCb cb {};
    result = M_OpenCamera(
            source.camera_manager,
            CAMERA_ID,
            &cb,
            &source.camera_device);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to open camera");
        return false;
    }

    result = M_CreateRequest(
            source.camera_device,
            M_TEMPLATE_PREVIEW,
            &source.camera_request);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create capture request");
        return false;
    }

    result = M_SetFpsRange(source.camera_request, fps_range);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to set FPS range");
        return false;
    }

    result = M_CreateContainer(&container);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create container");
        return false;
    }

    result = M_CreateOutput(source.encoder_window, &encoder_output);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create encoder output");
        return false;
    }
    M_AddOutput(container, encoder_output);

    result = M_CreateTarget(source.encoder_window, &source.encoder_target);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create encoder target");
        return false;
    }
    M_AddTarget(source.camera_request, source.encoder_target);

    result = M_CreateOutput(source.reader_window, &reader_output);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create reader output");
        return false;
    }
    M_AddOutput(container, reader_output);

    result = M_CreateTarget(source.reader_window, &source.reader_target);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create reader target");
        return false;
    }
    M_AddTarget(source.camera_request, source.reader_target);


    result = M_CreateSession(
            source.camera_device,
            container,
            &source.camera_callbacks,
            &source.camera_session);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create capture session");
        return false;
    }

    result = M_SetRepeating(source.camera_session, source.camera_request);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to set repeating request: %d", result);
        return false;
    }

    return true;
}

static void EncodingLoop(M_VideoSource &source) {
    bool finish = false;
    ssz_t output_idx;
    sz_t output_size;
    byte_t *output_buffer;
    FrameBuffer<MAX_VIDEO_FRAME_SIZE> frame;

    while (!finish) {
        // Wait max 100ms for encoder finish,
        // timeout 0 = busy-waiting -> cost CPU
        output_idx = M_DequeueOutput(
            source.encoder,
            &source.buffer_info,
            100000);

        if (output_idx >= 0) {
            output_buffer = M_OutputBuffer(
                source.encoder,
                (sz_t)output_idx,
                &output_size);

            if (output_buffer && source.buffer_info.size > 0) {
                HandleEncoded(source,
                              output_buffer,
                              source.buffer_info.size,
                              source.buffer_info.presentationTimeUs,
                              static_cast<int_t>(source.buffer_info.flags),
                              frame);
            }

            M_ReleaseOutput(source.encoder, (sz_t)output_idx, false);

            if (source.buffer_info.flags & M_INFO_FLAG_END_OF_STREAM) {
                finish = true;
            }
        }
    }
}

static void HandleEncoded(M_VideoSource &source,
                   const byte_t *data,
                   sz_t size,
                   tm_t presentation_time_us,
                   int_t flags,
                   FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame) {

    if (flags & M_INFO_FLAG_CODEC_CONFIG) {
        ParseParams(source, data, size);
        return;
    }

    if (size <= MAX_VIDEO_FRAME_SIZE) {
        Reset(frame);
        Copy(frame.data, data, size);
        frame.size = size;
        frame.flags = flags;
        frame.timeUs = presentation_time_us;

        {
            Lock(&source.listener_lock);
            for (auto & listener : source.listeners) {
                if (listener.callback != nullptr &&
                    listener.context != nullptr) {
                    listener.callback(
                            listener.context,
                            frame);
                }
            }
            Unlock(&source.listener_lock);
        }
    }
    else {
        LOGE(LOG_TAG, "Frame size is too large: %zu, skipped", size);
    }
}

static void ParseParams(M_VideoSource &source, const byte_t *data, sz_t size) {
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
            dst = source.vps;
        }
        else if (nal_type == 33) {
            dst = source.sps;
        }
        else if (nal_type == 34) {
            dst = source.pps;
        }
        if (dst != nullptr) {
            Base64(data, nal.start + nal.codeSize, nal.end, dst);
        }
    }
    Store(&source.params_initialized, true);
}

static void ProcessFrame(M_VideoSource &source, M_ImageReader *reader) {
    if (M_AcquireImage(reader, &source.image) != M_RESULT_OK)
        return;
    // TODO: Do something fun later here
    M_DeleteImage(source.image);
}

static void StopCapture(M_VideoSource &source) {
    if (source.camera_session) {
        M_StopRepeating(source.camera_session);
        M_AbortCaptures(source.camera_session);
        M_CloseSession(source.camera_session);
        // This will call OnCaptureStop -> StopEncoding
    }
}

static void StopEncoding(M_VideoSource &source) {
    if (source.encoder) {
        // This function will enqueue EOS to EncodingLoop;
        M_SignalEOS(source.encoder);
    }
}

static void MarkStopped(M_VideoSource &source) {
    Store(&source.is_recording, false);
    Store(&source.is_stopping, false);
}

static void CleanUp(M_VideoSource &source) {
    // Already close camera_session in StopCapture()
    source.camera_session = nullptr;

    if (source.camera_device) {
        M_CloseCamera(source.camera_device);
        source.camera_device = nullptr;
    }

    if (source.camera_request) {
        M_FreeRequest(source.camera_request);
        source.camera_request = nullptr;
    }

    if (source.reader_target) {
        M_FreeTarget(source.reader_target);
        source.reader_target = nullptr;
    }

    if (source.encoder_target) {
        M_FreeTarget(source.encoder_target);
        source.encoder_target = nullptr;
    }

    if (source.camera_manager) {
        M_DeleteManager(source.camera_manager);
        source.camera_manager = nullptr;
    }

    if (source.encoder) {
        M_Stop(source.encoder);
        M_Delete(source.encoder);
        source.encoder = nullptr;
    }

    if (source.format) {
        M_Delete(source.format);
        source.format = nullptr;
    }

    if (source.image_reader) {
        M_DeleteReader(source.image_reader);
        source.image_reader = nullptr;
    }

    LOGI("CleanUp", "gracefully clean up video source");
}

static void OnImageAvailable(void *context, M_ImageReader *reader) {
    auto *source = static_cast<M_VideoSource *>(context);
    ProcessFrame(*source, reader);
}

static void OnCaptureStop(void *context, M_CSession *session) {
    auto *source = static_cast<M_VideoSource *>(context);
    StopEncoding(*source);
}
