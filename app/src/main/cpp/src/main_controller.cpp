
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