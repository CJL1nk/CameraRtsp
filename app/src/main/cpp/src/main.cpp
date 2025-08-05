
#include <jni.h>

#include "media/M_AudioSource.h"
#include "media/M_VideoSource.h"
#include "server/S_RtspServer.h"

M_AudioSource a_source;
M_VideoSource v_source;
S_RtspServer rtsp_server;

extern "C" jint JNI_OnLoad(JavaVM *vm, void* reserved) {
    M_Init(a_source);
    M_Init(v_source);
    S_Init(rtsp_server, &v_source, &a_source);
    return JNI_VERSION_1_6;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_MainController_startNative(JNIEnv *env, jobject thiz, jboolean video,
                                                          jboolean audio) {

    if (video) {
        M_Start(v_source);
    }
    if (audio) {
        M_Start(a_source);
    }
    S_Start(rtsp_server, video, audio);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_MainController_stopNative(JNIEnv *env, jobject thiz) {
    M_Stop(a_source);
    M_Stop(v_source);
    S_Stop(rtsp_server);
    LOGI("CleanUp", "gracefully clean up native");
}