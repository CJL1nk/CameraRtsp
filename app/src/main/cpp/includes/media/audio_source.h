#pragma once

#include <jni.h>
#include <aaudio/AAudio.h>
#include <array>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <mutex>
#include "media/helper/audio_frame_queue.h"
#include "utils/circular_deque.h"
#include "utils/constant.h"


class NativeAudioSource {
public:
    using FrameAvailableFunction = void (*)(const FrameBuffer<MAX_AUDIO_FRAME_SIZE>&, void* context);

    NativeAudioSource() = default;
    ~NativeAudioSource() = default;
    void start();
    void stop();
    bool addListener(FrameAvailableFunction callback, void* ctx);
    bool removeListener(void* ctx);

private:
    struct FrameListener {
        FrameAvailableFunction callback;
        void* context;
    };

private:
    static constexpr const char* LOG_TAG = "AudioSource";
    static constexpr size_t MAX_AUDIO_LISTENER = 2;
    static constexpr size_t MAX_AUDIO_RECORD_SAMPLE = 4096;
    static constexpr size_t SIZE_PER_SAMPLE = sizeof(int16_t) * AUDIO_CHANNEL_COUNT;
    static constexpr size_t MAX_AUDIO_RECORD_SIZE = MAX_AUDIO_RECORD_SAMPLE * SIZE_PER_SAMPLE;

    // Frame queue
    NativeAudioFrameQueue frame_queue_;

    // Frame available listeners
    std::mutex listener_mutex_;
    FrameListener listeners_[MAX_AUDIO_LISTENER] {};

    // Aaudio stream
    AAudioStream *audio_stream_ = nullptr;

    // Double buffering
    CircularDeque<uint8_t, MAX_AUDIO_RECORD_SIZE * 2> record_buffer_[2];
    std::atomic<int> record_read_idx_ {0 };
    std::atomic<int> record_write_idx_ {0 };
    std::condition_variable record_condition_;
    std::mutex record_mutex_;

    // Media codec for AAC encoding
    AMediaCodec* encoder_ = nullptr;
    AMediaFormat* format_ = nullptr;

    // Threading
    std::atomic<bool> is_recording_ {};
    pthread_t encoding_thread_ {};

    // Method
    void startEncoding();
    bool initializeAAudio();
    bool initializeEncoder();
    void runEncodingLoop();
    void handleEncodedFrame(const uint8_t* data, size_t size, int64_t presentation_time_us, int32_t flags);
    void onEncodedFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame);
    void cleanup();
    void onRecordDataAvailable(AAudioStream *stream, void *audioData, int32_t numSamples);

    static void frameQueueAvailableCallback(const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame, void* context) {
        static_cast<NativeAudioSource *>(context)->onEncodedFrameAvailable(frame);
    }

    static void* startEncodingThread(void* arg) {
        auto* self = static_cast<NativeAudioSource*>(arg);
        self->startEncoding();  // call the member function
        return nullptr;
    }

    static aaudio_data_callback_result_t audioDataCallback(
            AAudioStream *stream,
            void *userData,
            void *audioData,
            int32_t numSamples
    ) {
        auto source = static_cast<NativeAudioSource*>(userData);
        if (!source) return AAUDIO_CALLBACK_RESULT_STOP;

        if (!source->is_recording_.load()) {
            return AAUDIO_CALLBACK_RESULT_STOP;
        }

        source->onRecordDataAvailable(stream, audioData, numSamples);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
};