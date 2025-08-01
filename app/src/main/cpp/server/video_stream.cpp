
#include "video_stream.h"
#include "source/video_source.h"
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
        video_source_->addListener(this);
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
        pending_condition_.notify_one();
        pthread_join(processing_thread_, nullptr);
    }
    LOGD("CleanUp", "gracefully clean up video stream");
}

void VideoStream::onFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &info) {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_buffer_ = info;
        if (info.flags & BUFFER_FLAG_KEY_FRAME) {
            pending_key_frame_buffer_ = info;
        }
        pending_condition_.notify_one();
    }
    perf_monitor_.onFrameAvailable(info.presentation_time_us);
}

void VideoStream::streaming() {
    pthread_setname_np(pthread_self(), "VideoStream");

    auto seq = genSequenceNumber();

    while (running_.load()) {
        std::unique_lock<std::mutex> lock(pending_mutex_);

        // Wait for frames or stop signal
        pending_condition_.wait(lock, [this]{
            return (last_presentation_time_us_ < pending_buffer_.presentation_time_us &&
                    pending_key_frame_buffer_.size > 0) ||
                    !running_.load();
        });

        if (!running_.load()) {
            break;
        }

        if (last_presentation_time_us_ < pending_buffer_.presentation_time_us) {
            bool key_frame_missed =
                    latest_buffer_ < pending_buffer_ &&
                    latest_keyframe_buffer_ < pending_key_frame_buffer_ &&
                    latest_buffer_ != latest_keyframe_buffer_;

            latest_buffer_ = pending_buffer_;
            if (latest_keyframe_buffer_ != pending_key_frame_buffer_) {
                latest_keyframe_buffer_ = pending_key_frame_buffer_;
            }
            lock.unlock();

            if (key_frame_missed) {
                if (trySendAndAdvance(seq, latest_keyframe_buffer_)) {
                    LOGE(LOG_TAG, "Failed to send video frame");
                    break;
                }
            }

            if (trySendAndAdvance(seq, latest_buffer_)) {
                LOGE(LOG_TAG, "Failed to send video frame");
                break;
            }
        }
    }
    markStopped();
    LOGD(LOG_TAG, "Processing thread finished");
}

int32_t
VideoStream::trySendAndAdvance(uint16_t &seq, const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame) {
    auto rtp_ts = calculateRtpTimestamp(frame.presentation_time_us);
    if (sendFrame(seq, rtp_ts, frame) < 0) {
        return -1;
    }
    last_presentation_time_us_ = frame.presentation_time_us;
    last_rtp_ts_ = rtp_ts;
    seq = (seq + 1) % 65536;
    perf_monitor_.onFrameSend(last_presentation_time_us_);
    return 0;
}

uint32_t VideoStream::calculateRtpTimestamp(int64_t next_frame_timestamp_us) const {
    auto delta_frame_timestamp_us = next_frame_timestamp_us - last_presentation_time_us_;
    return last_rtp_ts_ + delta_frame_timestamp_us * VIDEO_SAMPLE_RATE / 1000000;
}

int32_t VideoStream::sendFrame(uint16_t seq, uint32_t rtp_ts, const FrameBuffer<MAX_VIDEO_FRAME_SIZE>&frame) {
    auto nalUnits = extractNalUnits<16>(frame.data.data(), 0, frame.size);
    for (auto nal : nalUnits) {
        if (!nal.isValid()) {
            continue;
        }
        size_t offset = nal.start;
        while (offset < nal.end) {
            auto read = packetizeH265Frame(
                    interleave_, ssrc_,
                    seq, rtp_ts,
                    frame, offset, nal,
                    socket_buffer_.data.data(), socket_buffer_.data.size()
            );
            if (send(socket_, socket_buffer_.data.data(), read, 0) < 0) {
                return -1;
            }
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
