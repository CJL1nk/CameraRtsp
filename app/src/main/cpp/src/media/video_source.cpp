#include "media/video_source.h"

#include "utils/android_log.h"
#include "utils/base64.h"
#include "utils/h265_nal_unit.h"

void NativeVideoSource::start() {
    if (is_stopping_.load()) {
        return;
    }
    if (is_recording_.exchange(true)) {
        return;
    }
    pthread_create(&encoding_thread_, nullptr, startEncodingThread, this);
}

void NativeVideoSource::stop() {
    if (!is_recording_.load()) {
        return;
    }
    if (is_stopping_.exchange(true)) {
        return;
    }
    stopCapture();
}

bool NativeVideoSource::addListener(FrameAvailableFunction callback, void* ctx) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.callback == nullptr && i.context == nullptr) {
            i.callback = callback;
            i.context = ctx;
            return true;
        }
    }
    return false;
}

bool NativeVideoSource::removeListener(void* ctx) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.context == ctx) {
            i.callback = nullptr;
            i.context = nullptr;
            return true;
        }
    }
    return false;
}

void NativeVideoSource::startEncoding() {
    pthread_setname_np(pthread_self(), "CameraSource");

    if (!initializeEncoder()) {
        LOGE(LOG_TAG, "Failed to initialize encoder");
        is_recording_.store(false);
        cleanup();
        return;
    }

    if (!initializeImageReader()) {
        LOGE(LOG_TAG, "Failed to initialize image reader");
        is_recording_.store(false);
        cleanup();
        return;
    }

    if (!initializeCamera()) {
        LOGE(LOG_TAG, "Failed to initialize camera");
        is_recording_.store(false);
        cleanup();
        return;
    }

    encodingLoop();

    cleanup();
    LOGD("CleanUp", "gracefully clean up video source");
}

bool NativeVideoSource::initializeEncoder() {
    encoder_ = AMediaCodec_createEncoderByType("video/hevc");
    if (!encoder_) {
        LOGE(LOG_TAG, "Failed to create AAC encoder");
        return false;
    }

    format_ = AMediaFormat_new();
    AMediaFormat_setString(format_, AMEDIAFORMAT_KEY_MIME, "video/hevc");
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_WIDTH, VIDEO_WIDTH);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_HEIGHT, VIDEO_HEIGHT);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FORMAT_SURFACE);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_BIT_RATE, VIDEO_BITRATE);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, VIDEO_IFRAME_INTERVAL);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_FRAME_RATE, VIDEO_FRAME_RATE);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_PROFILE, CODEC_PROFILE);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_LEVEL, CODEC_LEVEL);

    media_status_t status = AMediaCodec_configure(encoder_, format_, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to configure encoder: %d", status);
        return false;
    }

    status = AMediaCodec_createInputSurface(encoder_, &encoder_window_);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to create input surface: %d", status);
        return false;
    }

    status = AMediaCodec_start(encoder_);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to start encoder: %d", status);
        return false;
    }

    return true;
}

bool NativeVideoSource::initializeImageReader() {
    AImageReader_new(VIDEO_WIDTH,
                     VIDEO_HEIGHT,
                     AIMAGE_FORMAT_YUV_420_888,
                     IMAGE_READER_CACHE_SIZE,
                     &image_reader_);
    if (!image_reader_) {
        LOGE(LOG_TAG, "Failed to create image reader");
        return false;
    }

    AImageReader_getWindow(image_reader_, &reader_window_);
    if (!reader_window_) {
        LOGE(LOG_TAG, "Failed to create image reader window");
        return false;
    }

    struct AImageReader_ImageListener listener {
            .context = this,
            .onImageAvailable = onImageAvailable
    };
    AImageReader_setImageListener(image_reader_, &listener);
    return true;
}

bool NativeVideoSource::initializeCamera() {
    camera_manager_ = ACameraManager_create();
    if (!camera_manager_) {
        LOGE(LOG_TAG, "Failed to create camera manager");
        return false;
    }

    ACameraDevice_StateCallbacks callbacks = {};
    ACameraManager_openCamera(camera_manager_, CAMERA_ID, &callbacks, &camera_device_);
    if (!camera_device_) {
        LOGE(LOG_TAG, "Failed to open camera");
        return false;
    }

    ACameraDevice_createCaptureRequest(camera_device_, TEMPLATE_PREVIEW, &camera_request_);
    if (!camera_request_) {
        LOGE(LOG_TAG, "Failed to create capture request");
        return false;
    }

    ACaptureRequest_setEntry_i32(
            camera_request_,
            ACAMERA_CONTROL_AE_TARGET_FPS_RANGE,
            2, VIDEO_FRAME_RATE_RANGE);

    ACaptureSessionOutputContainer* container;
    ACaptureSessionOutputContainer_create(&container);

    ACaptureSessionOutput* encoder_output;
    ACaptureSessionOutput_create(encoder_window_, &encoder_output);
    ACaptureSessionOutputContainer_add(container, encoder_output);

    ACameraOutputTarget_create(encoder_window_, &encoder_target_);
    ACaptureRequest_addTarget(camera_request_, encoder_target_);
    if (!encoder_target_) {
        LOGE(LOG_TAG, "Failed to create encoder target");
        return false;
    }

    ACaptureSessionOutput* reader_output;
    ACaptureSessionOutput_create(reader_window_, &reader_output);
    ACaptureSessionOutputContainer_add(container, reader_output);

    ACameraOutputTarget_create(reader_window_, &reader_target_);
    ACaptureRequest_addTarget(camera_request_, reader_target_);
    if (!reader_target_) {
        LOGE(LOG_TAG, "Failed to create reader target");
        return false;
    }

    ACameraCaptureSession_stateCallbacks sessionCb = {
            .context = this,
            .onClosed = CaptureStopCallback,
            .onReady {},
            .onActive {}
    };
    ACameraDevice_createCaptureSession(camera_device_, container, &sessionCb, &camera_session_);
    if (!camera_session_) {
        LOGE(LOG_TAG, "Failed to create capture session");
        return false;
    }

    auto status = ACameraCaptureSession_setRepeatingRequest(camera_session_, nullptr, 1, &camera_request_, nullptr);
    if (status != ACAMERA_OK) {
        LOGE(LOG_TAG, "Failed to set repeating request: %d", status);
        return false;
    }
    return true;
}

void NativeVideoSource::processCameraFrame(AImageReader *reader) {
    if (AImageReader_acquireNextImage(reader, &image_) != AMEDIA_OK) return;
    // TODO: Do something fun later here
    AImage_delete(image_);
}

void NativeVideoSource::encodingLoop() {
    bool finish = false;
    while (!finish) {
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(encoder_, &buffer_info_, 100'000); // 100ms
        if (outIdx >= 0) {
            if (!(buffer_info_.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)) {
                size_t size;
                uint8_t *data = AMediaCodec_getOutputBuffer(encoder_, outIdx, &size);
                if (data) {
                    handleEncodedFrame(data, size, buffer_info_.presentationTimeUs,
                                       buffer_info_.flags);
                }
            } else {
                finish = true;
            }
            AMediaCodec_releaseOutputBuffer(encoder_, outIdx, false);
        }
    }
}

void NativeVideoSource::handleEncodedFrame(const uint8_t *data, size_t size,
                                           int64_t presentation_time_us, uint32_t flags) {
    if (size <= MAX_VIDEO_FRAME_SIZE) {
        FrameBuffer<MAX_VIDEO_FRAME_SIZE> frame;
        memcpy(frame.data.data(), data, size);
        frame.size = size;
        frame.flags = static_cast<int32_t>(flags);
        frame.presentation_time_us = presentation_time_us;
        onEncodedFrameAvailable(frame);
    } else {
        LOGE(LOG_TAG, "Frame size is too large: %zu, skipped", size);
    }
}

void NativeVideoSource::onEncodedFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame) {
    if (frame.flags & BUFFER_FLAG_CODEC_CONFIG) {
        parseParameterSets(frame);
        return;
    }
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.callback != nullptr && i.context != nullptr) {
            i.callback(frame, i.context);
        }
    }
}

void NativeVideoSource::parseParameterSets(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame) {
    auto nals = extractNalUnits<3>(frame.data.data(), 0, frame.size);
    for (auto nal : nals) {
        auto nal_type = NAL_TYPE(frame.data, nal);
        char* dst = nullptr;
        if (nal_type == 32) {
            dst = vps;
        } else if (nal_type == 33) {
            dst = sps;
        } else if (nal_type == 34) {
            dst = pps;
        }
        if (dst != nullptr) {
            convertBase64(frame.data.data(), nal.start, nal.end, dst);
        }
    }
    params_initialized_.exchange(true);
}

void NativeVideoSource::stopCapture() {
    if (camera_session_) {
        ACameraCaptureSession_stopRepeating(camera_session_);
        ACameraCaptureSession_abortCaptures(camera_session_);
        ACameraCaptureSession_close(camera_session_);
    }
}

void NativeVideoSource::stopEncoding() {
    if (encoder_) {
        AMediaCodec_signalEndOfInputStream(encoder_);
    }
    pthread_join(encoding_thread_, nullptr);
    cleanup();
    LOGD("CleanUp", "gracefully clean up video source");
    is_stopping_.store(false);
    is_recording_.store(false);
}

void NativeVideoSource::cleanup() {
    camera_session_ = nullptr;

    if (camera_device_) {
        ACameraDevice_close(camera_device_);
        camera_device_ = nullptr;
    }

    if (camera_request_) {
        ACaptureRequest_free(camera_request_);
        camera_request_ = nullptr;
    }

    if (reader_target_) {
        ACameraOutputTarget_free(reader_target_);
        reader_target_ = nullptr;
    }

    if (encoder_target_) {
        ACameraOutputTarget_free(encoder_target_);
        encoder_target_ = nullptr;
    }

    if (camera_manager_) {
        ACameraManager_delete(camera_manager_);
        camera_manager_ = nullptr;
    }

    if (encoder_) {
        AMediaCodec_stop(encoder_);
        AMediaCodec_delete(encoder_);
        encoder_ = nullptr;
    }

    if (format_) {
        AMediaFormat_delete(format_);
        format_ = nullptr;
    }

    if (encoder_window_) {
        ANativeWindow_release(encoder_window_);
        encoder_window_ = nullptr;
    }

    if (image_) {
        AImage_delete(image_);
        image_ = nullptr;
    }

    if (reader_window_) {
        ANativeWindow_release(reader_window_);
        reader_window_ = nullptr;
    }

    if (image_reader_) {
        AImageReader_delete(image_reader_);
        image_reader_ = nullptr;
    }
}
