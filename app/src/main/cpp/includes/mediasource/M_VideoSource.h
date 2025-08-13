#pragma once

#include "mediasource/M_Platform.h"
#include "utils/Configs.h"
#include "utils/Platform.h"

typedef void (*M_VFrameCallback)(void *context, M_ImageReader *reader);

typedef void (*M_VClosedCallback)(void *context);

typedef struct {
    M_VFrameCallback frameCallback;
    M_VClosedCallback closedCallback;
    void *context;
} M_VFrameListener;

typedef struct {

    // Frame available listeners
    lock_t listener_lock;
    M_VFrameListener listeners[MAX_VIDEO_LISTENER];

    // Encoder
    M_Window *encoder_window;
    a_bool_t is_encoding;

    // Image Reader
    M_ImageReader *image_reader;
    M_Window *reader_window;
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
} M_VideoSource;

void M_Init(M_VideoSource &source);
void M_Start(M_VideoSource &source, M_Window *encoder_window);
void M_StartEncoder(M_VideoSource &source);
void M_StopEncoder(M_VideoSource &source);
void M_Stop(M_VideoSource &source);
bool M_AddListener(M_VideoSource &source,
                   M_VFrameListener listener,
                   void *ctx);
bool M_RemoveListener(M_VideoSource &source, void *ctx);
