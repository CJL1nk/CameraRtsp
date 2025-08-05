#pragma once

#include <jni.h>
#include <mutex>
#include <camera/NdkCameraManager.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include "utils/constant.h"
#include "utils/frame_buffer.h"

class NativeVideoSource {
public:
    using FrameAvailableFunction = void (*)(const FrameBuffer<MAX_VIDEO_FRAME_SIZE>&, void* context);

    NativeVideoSource() = default;
    ~NativeVideoSource() = default;
    void start();
    void stop();

    // Frame listeners
    bool addListener(FrameAvailableFunction callback, void* ctx);
    bool removeListener(void* ctx);

    // Parameter set
    char vps[64] {};
    char sps[64] {};
    char pps[64] {};
    bool areParamsInitialized() const { return params_initialized_.load(); }

private:
    struct FrameListener {
        FrameAvailableFunction callback;
        void* context;
    };

private:
    static constexpr const char* LOG_TAG = "VideoSource";
    static constexpr size_t MAX_VIDEO_LISTENER = 2;
    static constexpr size_t VIDEO_WIDTH = 1280;
    static constexpr size_t VIDEO_HEIGHT = 720;
    static constexpr size_t VIDEO_BITRATE = 2'000'000;
    static constexpr size_t VIDEO_IFRAME_INTERVAL = 1;
    static constexpr size_t VIDEO_FRAME_RATE = 30;
    static constexpr const int32_t VIDEO_FRAME_RATE_RANGE[] = { 15, 30 };
    static constexpr size_t CODEC_PROFILE = 1;
    static constexpr size_t CODEC_LEVEL = 2097152;
    static constexpr size_t COLOR_FORMAT_SURFACE = 0x7F000789;
    static constexpr size_t IMAGE_READER_CACHE_SIZE = 1;
    static constexpr char CAMERA_ID[] = "0";

    // Thread
    std::atomic_bool is_recording_ { false };
    std::atomic_bool is_stopping_ { false };
    pthread_t encoding_thread_ {};

    // Encoder
    AMediaCodec* encoder_;
    AMediaFormat* format_;
    ANativeWindow* encoder_window_;
    AMediaCodecBufferInfo buffer_info_;

    // Image Reader
    AImageReader* image_reader_;
    ANativeWindow* reader_window_;
    AImage* image_;

    // Camera
    ACameraManager* camera_manager_;
    ACameraDevice* camera_device_;
    ACameraCaptureSession* camera_session_;
    ACaptureRequest* camera_request_;
    ACameraOutputTarget* reader_target_;
    ACameraOutputTarget* encoder_target_;

    // Listeners
    std::mutex listener_mutex_;
    std::atomic_bool params_initialized_ { false };
    FrameListener listeners_[MAX_VIDEO_LISTENER] {};

private:
    void startEncoding();
    bool initializeEncoder();
    bool initializeImageReader();
    bool initializeCamera();
    void processCameraFrame(AImageReader* reader);
    void encodingLoop();
    void handleEncodedFrame(const uint8_t* data, size_t size, int64_t presentation_time_us, uint32_t flags);
    void onEncodedFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);
    void stopCapture();
    void stopEncoding();
    void cleanup();
    void parseParameterSets(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);

private:
    static void* startEncodingThread(void* arg) {
        auto* self = static_cast<NativeVideoSource*>(arg);
        self->startEncoding();  // call the member function
        return nullptr;
    }

    static void onImageAvailable(void* ctx, AImageReader* reader) {
        auto* self = static_cast<NativeVideoSource*>(ctx);
        self->processCameraFrame(reader);
    }

    static void CaptureStopCallback(void* ctx, ACameraCaptureSession *session) {
        auto* self = static_cast<NativeVideoSource*>(ctx);
        self->stopEncoding();
    }
};