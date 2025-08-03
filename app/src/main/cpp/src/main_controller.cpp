
#include "main_controller.h"

void MainController::start(bool start_video, bool start_audio) {
    if (start_video) {
        video_source_.start();
    }
    if (start_audio) {
        audio_source_.start();
    }
    if (start_video || start_audio) {
        rtsp_server_.start(start_video, start_audio);
    }
}

void MainController::stop() {
    video_source_.stop();
    audio_source_.stop();
    rtsp_server_.stop();
}

void MainController::onFrameAvailable(bool is_video, const uint8_t* data,
                                      size_t size, size_t offset, int64_t time, int32_t flags) {
    if (is_video && size <= MAX_VIDEO_FRAME_SIZE) {
        FrameBuffer<MAX_VIDEO_FRAME_SIZE> frame {time, size, flags};
        std::memcpy(frame.data.data(), data + offset, size);
        video_source_.onFrameAvailable(frame);
    }
}

static MainController* g_main_controller = nullptr;

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_MainController_startNative(JNIEnv *env, jobject thiz,
                                                          jboolean video, jboolean audio) {
    if (g_main_controller == nullptr) {
        g_main_controller = new MainController();
    }
    g_main_controller->start(video, audio);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_MainController_stopNative(JNIEnv *env, jobject thiz) {
    if (g_main_controller != nullptr) {
        g_main_controller->stop();
        delete g_main_controller;
        g_main_controller = nullptr;
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_AudioSource_onAudioFrameAvailableNative(JNIEnv *env,
                                                                              jobject thiz,
                                                                              jobject buffer,
                                                                              jint offset,
                                                                              jint size, jlong time,
                                                                              jint flags) {
    if (g_main_controller != nullptr) {
        g_main_controller->onFrameAvailable(
                false,
                reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(buffer)),
                size, offset, time, flags
        );
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_CameraSource_onVideoFrameAvailableNative(JNIEnv *env,
                                                                               jobject thiz,
                                                                               jobject buffer,
                                                                               jint offset,
                                                                               jint size,
                                                                               jlong time,
                                                                               jint flags) {
    if (g_main_controller != nullptr) {
        g_main_controller->onFrameAvailable(
                true,
                reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(buffer)),
                size, offset, time, flags
        );
    }
}