#include "mediasource/M_VideoSource.h"
#include "mediasource/M_Platform.h"
#include "utils/Utils.h"

#define LOG_TAG "VideoSource"

// Forward declarations
static bool InitReader(M_VideoSource &source);
static bool PrepareCamera(M_VideoSource &source);
static bool StartCaptureRequest(M_VideoSource &source,
                                bool_t encode);
static void MarkStopped(M_VideoSource &source);
static void CleanUp(M_VideoSource &source);
static void StopCapture(M_VideoSource &source);
static void OnImageAvailable(void *context, M_ImageReader *reader);
static void OnCaptureStopped(void *context, M_CSession *session);

void M_Init(M_VideoSource &source) {
    // We mustn't reset listeners every session.
    // Listener should add and remove itself manually.
    for (auto & listener : source.listeners) {
        listener.frameCallback = nullptr;
        listener.context = nullptr;
    }

    // Initialize synchronization primitives
    Init(&source.listener_lock);

    // Initialize listeners
    source.image_listener.context = &source;
    source.image_listener.onImageAvailable = OnImageAvailable;
    source.camera_callbacks.context = &source;
    source.camera_callbacks.onClosed = OnCaptureStopped;

    // Initialize synchronization primitives
    Init(&source.is_recording);
    Init(&source.is_stopping);

    // Initialize pointers
    source.encoder_window = nullptr;
    source.image_reader = nullptr;
    source.reader_window = nullptr;
    source.camera_manager = nullptr;
    source.camera_device = nullptr;
    source.camera_session = nullptr;
    source.camera_request = nullptr;
    source.reader_target = nullptr;
    source.encoder_target = nullptr;
}

void M_Start(M_VideoSource &source, M_Window *encoder_window) {
    if (Load(&source.is_stopping)) {
        return;
    }

    if (GetAndSet(&source.is_recording, true)) {
        return; // Already running
    }

    // We need the window from MediaCodec to initialize the camera
    source.encoder_window = encoder_window;

    if (!InitReader(source)) {
        LOGE(LOG_TAG, "Failed to initialize image reader");
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    if (!PrepareCamera(source)) {
        LOGE(LOG_TAG, "Failed to open camera");
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    if (!StartCaptureRequest(source, false)) {
        LOGE(LOG_TAG, "Failed to set initial capture request");
        MarkStopped(source);
        CleanUp(source);
    }

    LOGI(LOG_TAG, "Video source started successfully");
}

void M_StartEncoder(M_VideoSource &source) {
    if (Load(&source.is_stopping)) {
        return;
    }
    if (!Load(&source.is_recording)) {
        return;
    }
    if (!source.camera_session) {
        LOGE(LOG_TAG, "Camera session is not ready");
        return;
    }
    StartCaptureRequest(source, true);
}

void M_StopEncoder(M_VideoSource &source) {
    if (Load(&source.is_stopping)) {
        return;
    }
    if (!Load(&source.is_recording)) {
        return;
    }
    if (!source.camera_session) {
        LOGE(LOG_TAG, "Camera session is not ready");
        return;
    }
    StartCaptureRequest(source, false);
}

void M_Stop(M_VideoSource &source) {
    if (!Load(&source.is_recording)) {
        return;
    }
    if (GetAndSet(&source.is_stopping, true)) {
        return;
    }
    StopCapture(source);
}

bool M_AddListener(M_VideoSource &source, M_VFrameListener cb, void *ctx) {
    Lock(&source.listener_lock);
    for (auto & listener : source.listeners) {
        if (listener.frameCallback == nullptr &&
            listener.closedCallback == nullptr &&
            listener.context == nullptr) {
            
            listener.frameCallback = cb.frameCallback;
            listener.closedCallback = cb.closedCallback;
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
            listener.frameCallback = nullptr;
            listener.closedCallback = nullptr;
            listener.context = nullptr;
            Unlock(&source.listener_lock);
            return true;
        }
    }
    Unlock(&source.listener_lock);
    return false;
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

static bool PrepareCamera(M_VideoSource &source) {
    result_t result;
    M_CContainer *container;
    M_COutput *encoder_output;
    M_COutput *reader_output;

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

    result = M_CreateSession(
            source.camera_device,
            container,
            &source.camera_callbacks,
            &source.camera_session);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to create capture session");
        return false;
    }

    return true;
}

static bool StartCaptureRequest(M_VideoSource &source,
                                bool_t encode) {
    static const int_t fps_range[] = {VIDEO_MIN_FRAME_RATE, VIDEO_DEFAULT_FRAME_RATE};
    result_t result;

    if (source.camera_request && Load(&source.is_encoding) == encode) {
        return true;
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

    M_AddTarget(source.camera_request, source.reader_target);

    // This will be added later when codec attached
    if (encode) {
        M_AddTarget(source.camera_request, source.encoder_target);
    }

    result = M_SetRepeating(source.camera_session, source.camera_request);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to set repeating request: %d", result);
        return false;
    }

    Store(&source.is_encoding, encode);
    return true;
}

static void StopCapture(M_VideoSource &source) {
    if (source.camera_session) {
        M_StopRepeating(source.camera_session);
        M_AbortCaptures(source.camera_session);
        M_CloseSession(source.camera_session);
        // This will call OnCaptureStopped
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

    if (source.image_reader) {
        M_DeleteReader(source.image_reader);
        source.image_reader = nullptr;
    }

    LOGI("CleanUp", "gracefully clean up video source");
}

static void OnImageAvailable(void *context, M_ImageReader *reader) {
    auto *source = static_cast<M_VideoSource *>(context);

    Lock(&source->listener_lock);
    for (auto &listener: source->listeners) {
        if (listener.context && listener.frameCallback) {
            listener.frameCallback(listener.context, reader);
        }
    }
    Unlock(&source->listener_lock);
}

static void OnCaptureStopped(void *context, M_CSession *session) {
    auto *source = static_cast<M_VideoSource *>(context);

    Lock(&source->listener_lock);
    for (auto &listener: source->listeners) {
        if (listener.context && listener.closedCallback) {
            listener.closedCallback(listener.context);
        }
    }
    Unlock(&source->listener_lock);

    MarkStopped(*source);
    CleanUp(*source);
}
