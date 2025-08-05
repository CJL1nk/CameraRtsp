#include "utils/StreamStats.h"
#include "utils/Configs.h"
#include "utils/Platform.h"

void Init(StreamStats& stats, bool_t video) {
    stats.send_us = 0;
    stats.delta_send_us = 0;
    stats.receive_us = 0;
    stats.delta_receive_us = 0;
    stats.var_us = 0;
    stats.v_count = 0;

    stats.start_us = 0;
    stats.elapsed_us = 0;
    stats.process_us = 0;
    stats.p_count = 0;

    stats.receive = 0;
    stats.sent = 0;
    stats.video = video;
}

static void Print(const StreamStats& stats) {
    const char_t* name = stats.video ? "video" : "audio";
    LOGI("StreamStats",
         "Track %s: "
         "Sent (%zu), "
         "Skipped (%zu), "
         "Avg process: (%.2f) us, "
         "Avg frame variances: (%.2f) us",
         name,
         stats.sent,
         stats.receive - stats.sent,
         stats.process_us,
         stats.var_us);
}

void ReceiveFrame(StreamStats &stats) {
    tm_t now = NowMicros();
    if (stats.receive > 0 && stats.receive % STATS_LOG_INTERVAL == 0) {
        Print(stats);
    }

    stats.receive++;

    if (stats.receive_us != 0) {
        stats.delta_receive_us = now - stats.receive_us;
    }
    stats.receive_us = now;
}

/**
 * Stability => Delta send frame ~ delta receive frame
 * Use the same average formula as EndProcess
 */
void SendFrame(StreamStats &stats) {
    tm_t now = NowMicros();

    stats.sent++;

    if (stats.send_us != 0) {
        stats.delta_send_us = now - stats.send_us;
    }
    stats.send_us = now;

    if (stats.delta_send_us != 0 && stats.delta_receive_us != 0) {
        stats.v_count += 1;
        long_t delta_us = (long_t)(stats.delta_send_us) - stats.delta_receive_us;
        stats.var_us += (delta_us - stats.var_us) / stats.v_count;
    }
}

void StartProcess(StreamStats& stats) {
    stats.start_us = NowMicros();
    stats.elapsed_us = 0;
}

void PauseProcess(StreamStats& stats) {
    stats.elapsed_us += NowMicros() - stats.start_us;
    stats.start_us = 0;
}

void ResumeProcess(StreamStats& stats) {
    stats.start_us = NowMicros();
}

/**
 * New_avg - old_avg
 * = (X + T) / (N + 1) - X / N
 * = ((X + T) * N - X * (N + 1)) / N / (N + 1)
 * = (T * N - X) / N / (N + 1)
 * = T / (N + 1) - X / N / (N + 1)
 * = T / (N + 1) - old_avg / (N + 1)
 * = (T - old_avg) / (N + 1)
 *
 * => New avg = old_avg + (T - old_avg) / (N + 1)
 */
void EndProcess(StreamStats& stats) {
    if (stats.start_us != 0) {
        PauseProcess(stats);
    }
    stats.p_count += 1;
    stats.process_us += (stats.elapsed_us - stats.process_us) / stats.p_count;
}

