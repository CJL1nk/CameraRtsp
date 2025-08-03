#include "media/audio_source.h"
#include "utils/android_log.h"
#include "utils/time_utils.h"

void NativeAudioSource::start() {
    if (is_recording_.exchange(true)) {
        return;
    }
    pthread_create(&encoding_thread_, nullptr, startEncodingThread, this);
}

void NativeAudioSource::stop() {
    if (!is_recording_.exchange(false)) {
        return;
    }
    record_condition_.notify_one();
    pthread_join(encoding_thread_, nullptr);
}

bool NativeAudioSource::addListener(FrameAvailableFunction callback, void* ctx) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.callback == nullptr && i.context == nullptr) {
            i.callback = callback;
            i.context = ctx;
            return true;
        }
    }
    return false;
}

bool NativeAudioSource::removeListener(void* ctx) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.context == ctx) {
            i.callback = nullptr;
            i.context = nullptr;
            return true;
        }
    }
    return false;
}

void NativeAudioSource::startEncoding() {
    pthread_setname_np(pthread_self(), "AudioSource");

    if (!initializeAAudio()) {
        LOGE(LOG_TAG, "Failed to initialize Oboe");
        cleanup();
        return;
    }

    if (!initializeEncoder()) {
        LOGE(LOG_TAG, "Failed to initialize encoder");
        cleanup();
        return;
    }

    frame_queue_.addFrameCallback(frameQueueAvailableCallback, this);
    frame_queue_.start();

    if (audio_stream_) {
        auto result = AAudioStream_requestStart(audio_stream_);
        if (result != AAUDIO_OK) {
            LOGE(LOG_TAG, "Failed to start audio stream: %d", result);
            // DEADLOCK
            stop();
        }
    }

    LOGD(LOG_TAG, "Audio source started successfully");

    runEncodingLoop();

    frame_queue_.stop();
    frame_queue_.removeFrameCallback(this);

    cleanup();
    LOGD("CleanUp", "gracefully clean up audio source");
}

bool NativeAudioSource::initializeAAudio() {
    AAudioStreamBuilder *builder;
    aaudio_result_t result;

    result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) return false;

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, AUDIO_SAMPLE_RATE);
    AAudioStreamBuilder_setChannelCount(builder, AUDIO_CHANNEL_COUNT); // mono
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, audioDataCallback, this);

    result = AAudioStreamBuilder_openStream(builder, &audio_stream_);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK || audio_stream_ == nullptr) return false;
    return true;
}

bool NativeAudioSource::initializeEncoder() {
    // Create AAC encoder
    encoder_ = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (!encoder_) {
        LOGE(LOG_TAG, "Failed to create AAC encoder");
        return false;
    }

    // Create media format
    format_ = AMediaFormat_new();
    AMediaFormat_setString(format_, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_SAMPLE_RATE, AUDIO_SAMPLE_RATE);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_CHANNEL_COUNT, AUDIO_CHANNEL_COUNT);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_BIT_RATE, AUDIO_BIT_RATE);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_AAC_PROFILE, 2); // AAC-LC
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, MAX_AUDIO_RECORD_SIZE);

    // Configure encoder
    media_status_t status = AMediaCodec_configure(encoder_, format_, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to configure encoder: %d", status);
        return false;
    }

    // Start encoder
    status = AMediaCodec_start(encoder_);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to start encoder: %d", status);
        return false;
    }
    return true;
}

void NativeAudioSource::runEncodingLoop() {
    bool wait_record = true;
    bool finish = false;
    AMediaCodecBufferInfo buffer_info;

    while (!finish) {
        if (wait_record) {
            std::unique_lock<std::mutex> lock(record_mutex_);
            // Wait for frames or stop signal
            record_condition_.wait(lock, [this]{
                auto idx = record_write_idx_.load(std::memory_order_acquire);
                auto& buffer = record_buffer_[idx];
                return buffer.size() >= MAX_AUDIO_RECORD_SIZE ||
                       !is_recording_.load();
            });
            lock.unlock();

            if (is_recording_.load()) {
                auto idx = record_write_idx_.load(std::memory_order_acquire);
                record_read_idx_.store(idx, std::memory_order_release);
            }
        }

        // Get input buffer from encoder
        ssize_t input_idx = AMediaCodec_dequeueInputBuffer(encoder_, 0);
        if (input_idx >= 0) {
            size_t input_size;
            uint8_t* input_buffer = AMediaCodec_getInputBuffer(encoder_, input_idx, &input_size);

            if (input_buffer) {
                if (is_recording_.load()) {
                    auto buffer_idx = record_read_idx_.load(std::memory_order_acquire);
                    auto buffer_size = record_buffer_[buffer_idx].pop_front(input_buffer, input_size);
                    wait_record = record_buffer_[buffer_idx].empty();

                    auto status = AMediaCodec_queueInputBuffer(
                            encoder_, input_idx,
                            0, buffer_size, currentTimeMicros(), 0);
                    if (status != AMEDIA_OK) {
                        LOGE(LOG_TAG, "Failed to queue input buffer: %d", status);
                    }
                } else {
                    AMediaCodec_queueInputBuffer(
                            encoder_, input_idx,
                            0, 0, -1, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                }
            }
        }

        ssize_t output_idx = AMediaCodec_dequeueOutputBuffer(encoder_, &buffer_info, 100'000); // 100ms

        while (output_idx >= 0) {
            if (!(buffer_info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) && buffer_info.size > 0) {
                size_t output_size;
                uint8_t* output_buffer = AMediaCodec_getOutputBuffer(encoder_, output_idx, &output_size);
                if (output_buffer && static_cast<size_t>(buffer_info.size) <= output_size) {
                    handleEncodedFrame(output_buffer + buffer_info.offset,
                                       buffer_info.size,
                                       buffer_info.presentationTimeUs,
                                       static_cast<int32_t>(buffer_info.flags));
                }
            }
            AMediaCodec_releaseOutputBuffer(encoder_, output_idx, false);

            if (buffer_info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                finish = true;
                break;
            }

            output_idx = AMediaCodec_dequeueOutputBuffer(encoder_, &buffer_info, 0);
        }
    }
    LOGD(LOG_TAG, "Encoding thread finished");
}

void NativeAudioSource::handleEncodedFrame(const uint8_t *data, size_t size, int64_t presentation_time_us,
                                           int32_t flags) {

    if (flags & BUFFER_FLAG_CODEC_CONFIG) {
        return;
    }

    if (size <= MAX_AUDIO_FRAME_SIZE) {
        frame_queue_.enqueueFrame(data, size, presentation_time_us, flags);
    } else {
        LOGE(LOG_TAG, "Frame size is too large: %zu, skipped", size);
    }
}

void NativeAudioSource::onEncodedFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.callback != nullptr && i.context != nullptr) {
            i.callback(frame, i.context);
        }
    }
}

void NativeAudioSource::cleanup() {
    if (audio_stream_) {
        AAudioStream_requestStop(audio_stream_);
        AAudioStream_close(audio_stream_);
        audio_stream_ = nullptr;
    }

    if (encoder_) {
        AMediaCodec_stop(encoder_);
        AMediaCodec_delete(encoder_);
        encoder_ = nullptr;
    }

    if (format_) {
        AMediaFormat_delete(format_);
        format_ = nullptr;
    }
}

void
NativeAudioSource::onRecordDataAvailable(AAudioStream *stream, void *audioData, int32_t numSamples) {
    if (numSamples <=  0) return;

    auto *data = reinterpret_cast<uint8_t*>(audioData);
    if (data == nullptr) return;

    size_t data_size = numSamples * SIZE_PER_SAMPLE;
    int index = 1 - record_read_idx_.load(std::memory_order_acquire);
    record_buffer_[index].push_back(data, data_size);
    record_write_idx_.store(index, std::memory_order_release);
    record_condition_.notify_one();
}
