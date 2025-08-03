
#include "server/stream/video_stream.h"
#include "media/video_source.h"
#include "utils/android_log.h"
#include "utils/h265_nal_unit.h"
#include "utils/packetizer.h"
#include "utils/server_utils.h"

#define LOG_TAG "VideoStream"

void VideoStream::start(int32_t socket, uint8_t itl, int32_t ssrc) {
    if (running_.exchange(true)) {
        return; // Already running
    }
    if (video_source_ != nullptr) {
        video_source_->addListener(onVideoFrameAvailable, this);
    }
    socket_ = socket;
    interleave_ = itl;
    ssrc_ = ssrc;
    last_rtp_ts_ = genRtpTimestamp();
    pthread_create(&processing_thread_, nullptr, runStreamingThread, this);
}

void VideoStream::stop() {
    bool was_running = running_.load();
    markStopped();
    if (was_running) {
        frame_condition_.notify_one();
        pthread_join(processing_thread_, nullptr);
    }
    LOGD("CleanUp", "gracefully clean up video stream");
}

void VideoStream::processFrame(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame) {
    int index = 1 - frame_read_idx_.load(std::memory_order_acquire);
    if (frame.flags & BUFFER_FLAG_KEY_FRAME) {
        keyframe_buffer_[index] = frame;
        frame_buffer_ready_[index] = IFRAME;

    } else if (frame.size <= NORMAL_VIDEO_FRAME_SIZE) {
        auto& non_keyframe = frame_buffer_[index];
        memcpy(non_keyframe.data.data(), frame.data.data(), frame.size);
        non_keyframe.presentation_time_us = frame.presentation_time_us;
        non_keyframe.size = frame.size;
        non_keyframe.flags = frame.flags;
        frame_buffer_ready_[index] = NON_IFRAME;
    }

    frame_write_idx_.store(index, std::memory_order_release);
    frame_condition_.notify_one();
    perf_monitor_.onFrameAvailable(frame.presentation_time_us);
}

void VideoStream::streaming() {
    pthread_setname_np(pthread_self(), "VideoStream");

    auto seq = genSequenceNumber();

    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(pending_mutex_);

            // Wait for frames or stop signal
            frame_condition_.wait(lock, [this]{
                auto idx = frame_write_idx_.load(std::memory_order_acquire);
                auto buffer_timestamp_us =
                        frame_buffer_ready_[idx] == IFRAME ? keyframe_buffer_[idx].presentation_time_us :
                        frame_buffer_ready_[idx] == NON_IFRAME ? frame_buffer_[idx].presentation_time_us :
                        0;
                return (last_presentation_time_us_ < buffer_timestamp_us &&
                        keyframe_buffer_[idx].size > 0) ||
                       !running_.load();
            });
        }

        if (!running_.load()) {
            break;
        }

        auto idx = frame_write_idx_.load(std::memory_order_acquire);
        frame_read_idx_.store(idx, std::memory_order_release);
        auto &frame_buffer = frame_buffer_[idx];
        auto &keyframe_buffer = keyframe_buffer_[idx];
        auto type = frame_buffer_ready_[idx];
        frame_buffer_ready_[idx] = false;
        auto buffer_timestamp_us =
                type == IFRAME ? keyframe_buffer.presentation_time_us :
                type == NON_IFRAME ? frame_buffer.presentation_time_us :
                0;

        if (last_presentation_time_us_ >= buffer_timestamp_us) {
            continue;
        }

        if (type == IFRAME) {
            if (sendFrameAndAdvance(seq, keyframe_buffer) < 0) {
                break;
            }
            continue;
        }

        // type == NON_IFRAME
        bool key_frame_missed = last_presentation_time_us_ < keyframe_buffer.presentation_time_us;
        if (key_frame_missed) {
            if (sendFrameAndAdvance(seq, keyframe_buffer) < 0) {
                break;
            }
        }

        if (sendFrameAndAdvance(seq, frame_buffer) < 0) {
            break;
        }
    }
    markStopped();
    LOGD(LOG_TAG, "Processing thread finished");
}

template<size_t BufferCapacity>
int32_t
VideoStream::sendFrameAndAdvance(uint16_t &seq, const FrameBuffer<BufferCapacity> &frame) {
    auto rtp_ts = calculateRtpTimestamp(frame.presentation_time_us);
    if (sendFrame(seq, rtp_ts, frame) < 0) {
        return -1;
    }
    last_presentation_time_us_ = frame.presentation_time_us;
    last_rtp_ts_ = rtp_ts;
    perf_monitor_.onFrameSend(last_presentation_time_us_);
    return 0;
}

uint32_t VideoStream::calculateRtpTimestamp(int64_t next_frame_timestamp_us) const {
    auto delta_frame_timestamp_us = next_frame_timestamp_us - last_presentation_time_us_;
    return last_rtp_ts_ + delta_frame_timestamp_us * VIDEO_SAMPLE_RATE / 1000000;
}

template<size_t BufferCapacity>
int32_t VideoStream::sendFrame(uint16_t &seq, uint32_t rtp_ts, const FrameBuffer<BufferCapacity>&frame) {
    auto nalUnits = extractNalUnits<16>(frame.data.data(), 0, frame.size);
    for (auto nal : nalUnits) {
        if (!nal.isValid()) {
            continue;
        }
        size_t offset = nal.start;
        while (running_.load() && offset < nal.end) {
            auto read = packetizeH265Frame(
                    interleave_, ssrc_,
                    seq, rtp_ts,
                    frame.data.data(), frame.size,
                    offset, nal,
                    socket_buffer_.data.data(), socket_buffer_.data.size()
            );
            if (read < 0) {
                LOGE(LOG_TAG, "Failed to packetize video frame");
                return -1;
            }
            if (send(socket_, socket_buffer_.data.data(), read, 0) < 0) {
                LOGE(LOG_TAG, "Failed to send video frame");
                return -1;
            }
            seq = (seq + 1) % 65536;
        }
    }
    return 0;
}

void VideoStream::markStopped() {
    if (!running_.exchange(false)) {
        return;
    }

    if (video_source_ != nullptr) {
        video_source_->removeListener(this);
    }
}
