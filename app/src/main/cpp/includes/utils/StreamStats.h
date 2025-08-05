#pragma once

#include "utils/Platform.h"

struct StreamStats {
    // Log stability
    tm_t send_us;
    tm_t delta_send_us;
    tm_t receive_us;
    tm_t delta_receive_us;
    double_t var_us;
    sz_t v_count;

    // Log process time
    tm_t start_us;
    tm_t elapsed_us;
    double_t process_us;
    sz_t p_count;

    // Log skipped
    sz_t receive;
    sz_t sent;
    bool video;
};

void Init(StreamStats& stats, bool_t video);
void ReceiveFrame(StreamStats &stats);
void SendFrame(StreamStats &stats);
void StartProcess(StreamStats& stats);
void PauseProcess(StreamStats& stats);
void ResumeProcess(StreamStats& stats);
void EndProcess(StreamStats& stats);

