
#include <jni.h>

#include "encoder/E_AAC.h"
#include "encoder/E_H265.h"
#include "mediasource/M_AudioSource.h"
#include "mediasource/M_VideoSource.h"
#include "processor/P_VEmpty.h"
#include "server/S_RtspServer.h"

E_AAC a_encoder;
E_H265 v_encoder;
P_VEmpty v_processor;
M_AudioSource a_source;
M_VideoSource v_source;
S_RtspServer rtsp_server;

extern "C" jint JNI_OnLoad(JavaVM *vm, void* reserved) {
    M_Init(a_source);
    M_Init(v_source);
    E_Init(a_encoder, &a_source);
    E_Init(v_encoder, &v_source);
    P_Init(v_processor, &v_source);
    S_Init(rtsp_server, &v_encoder, &a_encoder);
    return JNI_VERSION_1_6;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_MainController_startNative(JNIEnv *env, jobject thiz, jboolean video,
                                                          jboolean audio) {

    if (video) {
        auto *window = E_Start(v_encoder);
        P_Start(v_processor);
        M_Start(v_source, window);
    }
    if (audio) {
        E_Start(a_encoder);
        M_Start(a_source);
    }
    S_Start(rtsp_server, video, audio);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_MainController_stopNative(JNIEnv *env, jobject thiz) {
    M_Stop(a_source);
    E_Stop(a_encoder);
    M_Stop(v_source);
    P_Stop(v_processor);
    E_Stop(v_encoder);
    S_Stop(rtsp_server);
    LOGI("CleanUp", "gracefully clean up native");
}