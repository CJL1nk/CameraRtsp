#pragma once

#include "jni.h"
#include "source/audio_source.h"
#include "utils/server_utils.h"
#include "utils/stream_perf_monitor.h"
#include "utils/constant.h"
#include <mutex>
#include <pthread.h>
#include <sys/socket.h>

class AudioStream: public NativeAudioSource::NativeMediaSource::FrameListener {
public:
    explicit AudioStream(NativeAudioSource* source) : audio_source_(source) {};
    ~AudioStream() = default;
    void start(int32_t socket, uint8_t itl, int32_t ssrc);
    void stop();
    void onFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &info) override;
    bool isRunning() const { return running_.load(); }

private:
    int32_t ssrc_ = 0;
    uint8_t interleave_ = 0;

    int32_t socket_ = 0;
    FrameBuffer<RTP_MAX_PACKET_SIZE> socket_buffer_;

    std::mutex frame_mutex_;
    std::condition_variable frame_condition_;
    FrameBuffer<MAX_AUDIO_FRAME_SIZE> frame_buffer_;

    int64_t last_presentation_time_us_ = 0L;
    uint32_t last_rtp_ts_ = 0L;

    pthread_t processing_thread_ {};
    std::atomic<bool> running_ = false;

    NativeAudioSource *audio_source_;
    StreamPerfMonitor perf_monitor_ = StreamPerfMonitor(false);

    void streaming();
    void markStopped();
    uint32_t calculateRtpTimestamp(int64_t next_frame_timestamp_us) const;

    static void* runStreamingThread(void *arg) {
        auto stream = static_cast<AudioStream *>(arg);
        stream->streaming();
        return nullptr;
    }
};