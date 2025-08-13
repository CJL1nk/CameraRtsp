#pragma once

#include "encoder/E_AAC.h"
#include "server/S_Platform.h"
#include "server/S_StreamState.h"
#include "utils/StreamStats.h"


typedef struct {
    // Double buffer
    FrameBuffer<MAX_AUDIO_FRAME_SIZE> frame_buffer[2];

    // Socket buffer
    FrameBuffer<RTP_MAX_PACKET_SIZE> socket_buffer;

    // Stats
    StreamStats stats;

    // Socket data
    CancellableSocket* socket;
    tm_t last_time_us;
    int_t ssrc;
    uint_t last_rtp_ts;
    byte_t interleave;

    // Report data
    tm_t last_report_sec;
    uint_t packet_count;
    uint_t octet_count;

    // Double buffer synchronization
    lock_t frame_mutex;
    cond_t frame_condition;
    bool_t frame_ready[2];
    a_int_t write_idx;
    a_int_t read_idx;

    // Threading
    thread_t thread;

    // Status
    a_int_t state;

    // Encoder
    E_AAC* encoder;
} S_AACStream;

void S_Init(S_AACStream& stream, E_AAC* encoder);
void S_Prepare(S_AACStream& stream);
void S_Start(S_AACStream& stream,
             CancellableSocket* socket,
             byte_t interleave,
             int_t ssrc);
void S_Stop(S_AACStream& stream);
bool_t S_IsRunning(const S_AACStream& stream);