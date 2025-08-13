#pragma once


#include "encoder/E_Platform.h"
#include "mediasource/M_VideoSource.h"
#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/Platform.h"

typedef void (*E_H265FrameCallback)(void *context, const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &);

typedef struct {
    E_H265FrameCallback callback;
    void *context;
} E_H265FrameListener;

typedef struct{
    // Parameter sets
    char vps[H265_PARAMS_SIZE];
    char sps[H265_PARAMS_SIZE];
    char pps[H265_PARAMS_SIZE];
    a_bool_t params_initialized;
    lock_t params_lock;
    cond_t params_cond;

    // Frame available listeners
    lock_t listener_lock;
    E_H265FrameListener listeners[MAX_H265_LISTENER];

    // Video source
    M_VideoSource* source;
    
    // Encoder
    E_Codec *codec;
    E_Format *format;
    E_Window *encoder_window;
    E_BufferInfo buffer_info;

    // Threading
    // Idle: recording false, stopping false
    // Start: recording true, stopping false
    // Stop: recording true, stopping true
    // Stopped: recording false, stopping false
    a_bool_t is_recording;
    a_bool_t is_stopping;
    thread_t thread;
} E_H265;

void E_Init(E_H265 &encoder, M_VideoSource* source);
E_Window* E_Start(E_H265 &encoder);
void E_Stop(E_H265 &encoder);
bool E_AddListener(E_H265 &encoder,
                   E_H265FrameCallback callback,
                   void *ctx);
bool E_RemoveListener(E_H265 &encoder, void *ctx);
// This function will lock until params are available
void E_GetParams(E_H265 &encoder, char *vps, char *sps, char *pps);