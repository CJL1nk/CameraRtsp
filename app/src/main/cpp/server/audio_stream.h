#pragma once

#include "jni.h"
#include "packetizer/aac_latm_packetizer.h"
#include "source/audio_source.h"
#include "utils/server_utils.h"
#include "utils/stream_perf_monitor.h"
#include <mutex>
#include <thread>
#include <sys/socket.h>

class AudioStream: public NativeAudioSource::NativeMediaSource::FrameListener {
public:
    explicit AudioStream(uint8_t itl) : interleave_(itl) {};
    ~AudioStream() = default;
    void start(int32_t socket);
    void stop();
    void onFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &info) override;

private:
    int32_t ssrc_ = genSSRC();
    uint8_t interleave_;

    int32_t socket_ = 0;
    FrameBuffer<RTP_MAX_PACKET_SIZE> socket_buffer_;

    std::mutex frame_mutex_;
    std::condition_variable frame_condition_;
    FrameBuffer<MAX_AUDIO_FRAME_SIZE> frame_buffer_;

    int64_t last_presentation_time_us_ = 0L;
    uint32_t last_rtp_ts_ = genRtpTimestamp();

    AacLatmPacketizer packetizer_ = AacLatmPacketizer(interleave_, ssrc_);

    std::thread thread_;
    std::atomic<bool> running_ = false;

    StreamPerfMonitor perf_monitor_ = StreamPerfMonitor(false);

    void streaming();
    uint32_t calculateRtpTimestamp(int64_t next_frame_timestamp_us) const;
};