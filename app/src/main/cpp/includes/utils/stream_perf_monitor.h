#pragma once

#include "jni.h"
#include <array>
#include <mutex>
#include "circular_deque.h"
#include "android_log.h"

class StreamPerfMonitor {
public:
    explicit StreamPerfMonitor(bool is_video) : is_video_(is_video) {}
    ~StreamPerfMonitor() = default;

    static constexpr char LOG_TAG[] = "StreamPerfMonitor";

    void onFrameAvailable(int64_t presentation_time_us) {
        auto now = currentTimeNanos();
        std::lock_guard<std::mutex> lock(mutex_);
        frame_queue_.push_front(std::pair(presentation_time_us, now));
    }

    void onFrameSend(int64_t presentation_time_us) {
        auto now = currentTimeNanos();

        frame_count_++;

        {
            std::pair<int64_t, int64_t> current{0, 0};

            std::lock_guard<std::mutex> lock(mutex_);
            while (!frame_queue_.empty() &&
                   frame_queue_.pop_front(current) &&
                   current.first < presentation_time_us) {
                uint64_t overhead = now - current.second;
                size_t bin = getBin(overhead);
                frame_skipped_++;
                skipped_overhead_bin_[bin]++;
            }

            if (!frame_queue_.empty() && current.first == presentation_time_us) {
                uint64_t overhead = now - current.second;
                size_t bin = getBin(overhead);
                frame_sent_++;
                sent_overhead_bin_[bin]++;
            }
        }

        if (frame_count_ == LOG_INTERVAL) {
            const char* name = is_video_ ? "video" : "audio";
            char buffer[2048]; // make sure it's big enough
            int offset = snprintf(buffer, sizeof(buffer), "Track %s:\nSend stats: ", name);
            offset += printFilteredMapToBuffer(sent_overhead_bin_, buffer + offset, sizeof(buffer) - offset);
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\nSkip stats: ");
            offset += printFilteredMapToBuffer(skipped_overhead_bin_, buffer + offset, sizeof(buffer) - offset);
            LOGD(LOG_TAG, "%s", buffer);
            frame_count_ = 0;
        }
    }

private:
    static constexpr int BIN_COUNT = 30;
    static constexpr int FRAME_QUEUE_MAX_SIZE = 30;
    static constexpr int LOG_INTERVAL = 1000;

    static size_t getBin(uint64_t x) {
        auto bin = x / 1'000'000;
        return bin < BIN_COUNT ? bin : BIN_COUNT;
    }

    static int printFilteredMapToBuffer(const std::array<int, BIN_COUNT>& bin, char* buffer, size_t bufSize) {
        int written = 0;
        for (int i = 0; i < BIN_COUNT; ++i) {
            if (bin[i] != 0) {
                int n = snprintf(buffer + written, bufSize - written, "[%d] = %d, ", i, bin[i]);
                if (n < 0 || (size_t)n >= bufSize - written) break;
                written += n;
            }
        }
        return written;
    }

    static uint64_t currentTimeNanos() {
        timespec ts {};
        clock_gettime(CLOCK_REALTIME, &ts);
        return (uint64_t)(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
    }

    bool is_video_;
    size_t frame_count_ = 0;
    size_t frame_sent_ = 0;
    size_t frame_skipped_ = 0;
    std::mutex mutex_;
    CircularDeque<std::pair<int64_t, int64_t>, FRAME_QUEUE_MAX_SIZE> frame_queue_;
    std::array<int, BIN_COUNT> sent_overhead_bin_ {};
    std::array<int, BIN_COUNT> skipped_overhead_bin_ {};
};
