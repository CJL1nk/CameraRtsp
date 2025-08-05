#pragma once

#include "media/M_Platform.h"
#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/Platform.h"

typedef void (*M_VEncodedCallback)(void *context, const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &);

typedef struct {
    M_VEncodedCallback callback;
    void *context;
} M_VEncodedListener;

struct M_VideoSource {
    // Parameter sets
    char vps[H265_PARAMS_SIZE];
    char sps[H265_PARAMS_SIZE];
    char pps[H265_PARAMS_SIZE];
    a_bool_t params_initialized;

    // Frame available listeners
    lock_t listener_lock;
    M_VEncodedListener listeners[MAX_VIDEO_LISTENER];

    // Encoder
    M_Codec *encoder;
    M_Format *format;
    M_Window *encoder_window;
    M_BufferInfo buffer_info;

    // Image Reader
    M_ImageReader *image_reader;
    M_Window *reader_window;
    M_Image *image;
    M_ImageListener image_listener;

    // Camera
    M_CManager *camera_manager;
    M_CDevice *camera_device;
    M_CSession *camera_session;
    M_CRequest *camera_request;
    M_CTarget *reader_target;
    M_CTarget *encoder_target;
    M_CStateCallbacks camera_callbacks;

    // Threading
    // Idle: recording false, stopping false
    // Start: recording true, stopping false
    // Stop: recording true, stopping true
    // Stopped: recording false, stopping false
    a_bool_t is_recording;
    a_bool_t is_stopping;
    thread_t thread;
};

void M_Init(M_VideoSource &source);
void M_Start(M_VideoSource &source);
void M_Stop(M_VideoSource &source);
bool M_AddListener(M_VideoSource &source, M_VEncodedCallback callback, void *ctx);
bool M_RemoveListener(M_VideoSource &source, void *ctx);
bool M_ParamsReady(M_VideoSource &source);
void M_GetParams(M_VideoSource &source, char *vps, char *sps, char *pps);
