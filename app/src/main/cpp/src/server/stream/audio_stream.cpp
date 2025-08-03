#include "server/stream/audio_stream.h"
#include "media/audio_source.h"
#include "utils/android_log.h"
#include "utils/packetizer.h"
#include "utils/server_utils.h"

#define LOG_TAG "AudioStream"

void AudioStream::start(int32_t socket, uint8_t itl, int32_t ssrc) {
    if (running_.exchange(true)) {
        return; // Already running
    }
    if (audio_source_ != nullptr) {
        audio_source_->addListener(onAudioFrameAvailable, this);
    }
    socket_ = socket;
    interleave_ = itl;
    ssrc_ = ssrc;
    last_rtp_ts_ = genRtpTimestamp();
    pthread_create(&processing_thread_, nullptr, runStreamingThread, this);
}

void AudioStream::stop() {
    bool was_running = running_.load();
    markStopped();
    if (was_running) {
        frame_condition_.notify_one();
        pthread_join(processing_thread_, nullptr);
    }
    LOGD("CleanUp", "gracefully clean up audio stream");
}

void AudioStream::processFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame) {
    int index = 1 - frame_read_idx_.load(std::memory_order_acquire);
    frame_buffer_[index] = frame;
    frame_buffer_ready_[index] = true;
    frame_write_idx_.store(index, std::memory_order_release);
    frame_condition_.notify_one();
    perf_monitor_.onFrameAvailable(frame.presentation_time_us);
}

void AudioStream::streaming() {
    pthread_setname_np(pthread_self(), "AudioStream");

    auto seq = genSequenceNumber();

    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(frame_mutex_);
            // Wait for frames or stop signal
            frame_condition_.wait(lock, [this]{
                auto idx = frame_write_idx_.load(std::memory_order_acquire);
                auto buffer_timestamp_us = frame_buffer_[idx].presentation_time_us;
                return (frame_buffer_ready_[idx] && buffer_timestamp_us > last_presentation_time_us_) ||
                       !running_.load();
            });
        }

        if (!running_.load()) {
            break;
        }

        auto idx = frame_write_idx_.load(std::memory_order_acquire);
        frame_read_idx_.store(idx, std::memory_order_release);
        auto &frame_buffer = frame_buffer_[idx];
        frame_buffer_ready_[idx] = false;

        if (last_presentation_time_us_ >= frame_buffer.presentation_time_us) {
            continue;
        }

        auto rtp_ts = calculateRtpTimestamp(frame_buffer.presentation_time_us);
        auto read = packetizeAACFrame(
            interleave_, ssrc_,
            seq, rtp_ts,
            frame_buffer,
            socket_buffer_.data.data(),
            socket_buffer_.data.size()
        );

        if (send(socket_, socket_buffer_.data.data(), read, 0) < 0) {
            LOGE(LOG_TAG, "Failed to send audio frame");
            break;
        }

        last_presentation_time_us_ = frame_buffer.presentation_time_us;
        last_rtp_ts_ = rtp_ts;
        seq = (seq + 1) % 65536;
        perf_monitor_.onFrameSend(last_presentation_time_us_);
    }

    markStopped();
    LOGD(LOG_TAG, "Processing thread finished");
}


uint32_t AudioStream::calculateRtpTimestamp(int64_t next_frame_timestamp_us) const {
    auto delta_frame_timestamp_us = next_frame_timestamp_us - last_presentation_time_us_;
    return last_rtp_ts_ + delta_frame_timestamp_us * AUDIO_SAMPLE_RATE / 1000000;
}

void AudioStream::markStopped() {
    if (!running_.exchange(false)) {
        return;
    }

    if (audio_source_ != nullptr) {
        audio_source_->removeListener(this);
    }
}





