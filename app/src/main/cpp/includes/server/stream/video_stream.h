#pragma once

#include "jni.h"
#include "media/video_source.h"
#include "utils/server_utils.h"
#include "utils/stream_perf_monitor.h"
#include "utils/server_utils.h"
#include <mutex>
#include <pthread.h>
#include <sys/socket.h>

class VideoStream {
public:
    explicit VideoStream(NativeVideoSource *video_source) : video_source_(video_source) {};
    ~VideoStream() = default;
    void start(int32_t socket, uint8_t itl, int32_t ssrc);
    void stop();
    void processFrame(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);
    bool isRunning() const { return running_.load(); }

private:
    int32_t ssrc_ = 0;
    uint8_t interleave_ = 0;

    int32_t socket_ = 0;
    FrameBuffer<RTP_MAX_PACKET_SIZE> socket_buffer_;

    std::mutex pending_mutex_;
    std::condition_variable pending_condition_;
    FrameBuffer<MAX_VIDEO_FRAME_SIZE> pending_buffer_;
    FrameBuffer<MAX_VIDEO_FRAME_SIZE> latest_buffer_;

    FrameBuffer<MAX_VIDEO_FRAME_SIZE> pending_key_frame_buffer_;
    FrameBuffer<MAX_VIDEO_FRAME_SIZE> latest_keyframe_buffer_;

    int64_t last_presentation_time_us_ = 0L;
    uint32_t last_rtp_ts_ = 0L;

    pthread_t processing_thread_ {};
    std::atomic<bool> running_ = false;

    NativeVideoSource *video_source_;
    StreamPerfMonitor perf_monitor_ = StreamPerfMonitor(true);

private:
    void streaming();
    void markStopped();
    uint32_t calculateRtpTimestamp(int64_t next_frame_timestamp_us) const;
    int32_t sendFrameAndAdvance(uint16_t &seq, const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);
    int32_t sendFrame(uint16_t &seq, uint32_t rtp_ts, const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);

    static void* runStreamingThread(void *arg) {
        auto stream = static_cast<VideoStream *>(arg);
        stream->streaming();
        return nullptr;
    }

    static void onVideoFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &info, void* ctx) {
        auto stream = static_cast<VideoStream *>(ctx);
        stream->processFrame(info);
    }
};