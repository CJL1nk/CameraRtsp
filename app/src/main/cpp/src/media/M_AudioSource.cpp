#include "media/M_AudioSource.h"
#include "media/M_AFrameQueue.h"
#include "media/M_Platform.h"
#include "utils/CircularDeque.h"

#define LOG_TAG "AudioSource"

// Forward declarations
static bool InitStream(M_AudioSource &source);
static bool InitEncoder(M_AudioSource &source);
static void EncodingLoop(M_AudioSource &source);
static void HandleEncoded(M_AudioSource &source,
                   const byte_t *data,
                   sz_t size,
                   tm_t presentation_time_us,
                   int_t flags);
static void OnEncodedAvailable(void *context, const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame);
static void MarkStopped(M_AudioSource &source);
static void CleanUp(M_AudioSource &source);
static void OnRawAvailable(M_AudioSource &source, M_AStream *stream, void *audioData, int_t numSamples);


static void StartEncoding(M_AudioSource &source) {
    SetThreadName("AudioSource");

    if (!InitStream(source)) {
        LOGE(LOG_TAG, "Failed to initialize AAudio");
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    if (!InitEncoder(source)) {
        LOGE(LOG_TAG, "Failed to initialize encoder");
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    M_Start(source.queue);

    if (!source.stream) {
        LOGE(LOG_TAG, "Failed to create audio stream");
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    auto result = M_RequestStart(source.stream);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to start audio stream: %d", result);
        MarkStopped(source);
        CleanUp(source);
        return;
    }

    LOGI(LOG_TAG, "Audio source started successfully");
    EncodingLoop(source);

    // Stop the queue
    M_Stop(source.queue);

    CleanUp(source);
}

static void *StartEncodingThread(void *arg) {
    auto *source = static_cast<M_AudioSource *>(arg);
    if (source) {
        StartEncoding(*source);
    }
    return nullptr;
}

static int_t AudioDataCallback(
    AAudioStream *stream,
    void *userData,
    void *audioData,
    int_t numSamples) {
    
    auto *source = static_cast<M_AudioSource *>(userData);
    if (!source) {
        return M_AUDIO_CALLBACK_RESULT_STOP;
    }

    if (Load(&source->is_stopping)) {
        return M_AUDIO_CALLBACK_RESULT_STOP;
    }

    OnRawAvailable(*source, stream, audioData, numSamples);
    return M_AUDIO_CALLBACK_RESULT_CONTINUE;
}

// We mustn't reset listeners here
// Listener should add and remove itself manually.
static void Reset(M_AudioSource &source) {
    Reset(source.buffer[0]);
    Reset(source.buffer[1]);
    Reset(&source.read_idx);
    Reset(&source.write_idx);
}

void M_Init(M_AudioSource &source) {
    for (auto & listener : source.listeners) {
        listener.callback = nullptr;
        listener.context = nullptr;
    }

    // Initialize frame queue
    source.queue_cb.context = &source;
    source.queue_cb.fnc = OnEncodedAvailable;
    M_Init(source.queue, &source.queue_cb);
    
    // Initialize synchronization primitives
    Init(&source.listener_lock);
    Init(&source.condition);
    Init(&source.buffer_lock);

    // Initialize thread
    Init(&source.thread);
    Init(&source.is_recording);
    Init(&source.is_stopping);

    // Initialize pointers
    source.stream = nullptr;
    source.encoder = nullptr;
    source.format = nullptr;
}

void M_Start(M_AudioSource &source) {
    if (Load(&source.is_stopping)) {
        return;
    }

    if (GetAndSet(&source.is_recording, true)) {
        return; // Already running
    }

    Reset(source);
    Start(&source.thread, StartEncodingThread, &source);
}

void M_Stop(M_AudioSource &source) {
    if (!Load(&source.is_recording)) {
        return;
    }
    if (GetAndSet(&source.is_stopping, true)) {
        return;
    }
    Signal(&source.condition);

    Join(&source.thread);
    MarkStopped(source);
}

bool M_AddListener(M_AudioSource &source, M_AEncodedCallback callback, void *ctx) {
    Lock(&source.listener_lock);
    for (auto & listener : source.listeners) {
        if (listener.callback == nullptr &&
            listener.context == nullptr) {
            listener.callback = callback;
            listener.context = ctx;
            Unlock(&source.listener_lock);
            return true;
        }
    }
    Unlock(&source.listener_lock);
    return false;
}

bool M_RemoveListener(M_AudioSource &source, void *ctx) {
    Lock(&source.listener_lock);
    for (auto & listener : source.listeners) {
        if (listener.context == ctx) {
            listener.callback = nullptr;
            listener.context = nullptr;
            Unlock(&source.listener_lock);
            return true;
        }
    }
    Unlock(&source.listener_lock);
    return false;
}

static bool InitStream(M_AudioSource &source) {
    M_ABuilder *builder;
    result_t result;

    result = M_CreateBuilder(&builder);
    if (result != M_RESULT_OK) {
        return false;
    }

    M_SetDirection(builder, M_AUDIO_DIRECTION_INPUT);
    M_SetSampleRate(builder, AUDIO_SAMPLE_RATE);
    M_SetChannelCount(builder, AUDIO_CHANNEL_COUNT);
    M_SetFormat(builder, M_AUDIO_FORMAT_PCM_I16);
    M_SetPerformanceMode(builder, M_AUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    M_SetDataCallback(builder, AudioDataCallback, &source);

    result = M_OpenStream(builder, &source.stream);
    M_Delete(builder);
    if (result != M_RESULT_OK || source.stream == nullptr) {
        return false;
    }
    return true;
}

static bool InitEncoder(M_AudioSource &source) {
    result_t result;

    // Create AAC encoder
    source.encoder = M_CreateEncoder("audio/mp4a-latm");
    if (!source.encoder) {
        LOGE(LOG_TAG, "Failed to create AAC encoder");
        return false;
    }

    // Create media format
    source.format = M_NewFormat();
    M_SetString(source.format, M_KEY_MIME, "audio/mp4a-latm");
    M_SetInt32(source.format, M_KEY_SAMPLE_RATE, AUDIO_SAMPLE_RATE);
    M_SetInt32(source.format, M_KEY_CHANNEL_COUNT, AUDIO_CHANNEL_COUNT);
    M_SetInt32(source.format, M_KEY_BIT_RATE, AUDIO_BIT_RATE);
    M_SetInt32(source.format, M_KEY_AAC_PROFILE, 2); // AAC-LC
    M_SetInt32(source.format, M_KEY_MAX_INPUT_SIZE, MAX_AUDIO_RECORD_SIZE);

    // Configure encoder
    result = M_Configure(
            source.encoder,
            source.format,
            nullptr,
            nullptr,
            M_CODEC_CONFIGURE_ENCODE);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to configure encoder: %d", result);
        return false;
    }

    // Start encoder
    result = M_Start(source.encoder);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to start encoder: %d", result);
        return false;
    }
    return true;
}

// Wait for frames or stop signal
static bool_t Wait(M_AudioSource &source) {
    bool_t stopping = false;
    int_t write_old;
    int_t write_new;
    RecordBuffer* buffer;

    // Condition variable needs lock
    Lock(&source.buffer_lock);

    // First loop, wait for new frames
    while (true) {
        // Read all data happens before write_idx is set (OnRawAvailable)
        write_old = SyncAndGet(&source.write_idx);
        buffer = &source.buffer[write_old];
        stopping = Load(&source.is_stopping);

        // Break if read enough data or stopped
        if (buffer->size >= MAX_AUDIO_RECORD_SIZE || stopping) {
            break;
        }

        Wait(&source.condition, &source.buffer_lock);
    }

    // Tell OnRawAvailable we are reading on write_old buffer,
    // It should write on the other buffer.
    if (!stopping) {
        SetAndSync(&source.read_idx, write_old);
    }

    // Second loop, wait for buffer to switch to write_new
    while (true) {
        write_new = SyncAndGet(&source.write_idx);
        stopping = Load(&source.is_stopping);

        if (write_old != write_new || stopping) {
            break;
        }

        Wait(&source.condition, &source.buffer_lock);
    }

    Unlock(&source.buffer_lock);
    return stopping;
}

static bool_t EnqueueData(M_AudioSource &source, bool_t stopping) {
    ssz_t input_idx;
    sz_t input_size;
    sz_t buffer_size;
    result_t result;
    int_t buffer_idx;
    bool_t wait_record = false;
    byte_t *input_buffer;

    // Get input buffer from encoder
    input_idx = M_DequeueInput(source.encoder, 0);
    if (input_idx < 0) {
        return wait_record;
    }

    input_buffer = M_InputBuffer(
            source.encoder,
            (sz_t)input_idx,
            &input_size);
    if (input_buffer == nullptr || input_size == 0) {
        return wait_record;
    }

    if (stopping) {
        M_QueueInput(
                source.encoder,
                (sz_t) input_idx,
                0,
                0,
                -1,
                M_INFO_FLAG_END_OF_STREAM);
        return wait_record;
    }

    // Read all data happens before read_idx is set (after OnRawAvailable)
    buffer_idx = SyncAndGet(&source.read_idx);
    buffer_size = PopFront(
            source.buffer[buffer_idx],
            input_buffer,
            input_size);

    // Only wait if buffer is empty
    wait_record = Empty(source.buffer[buffer_idx]);

    result = M_QueueInput(
            source.encoder,
            (sz_t) input_idx,
            0,
            buffer_size,
            NowMicros(),
            0);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to queue input buffer: %d", result);
    }

    return wait_record;
}

static bool_t DequeueData(M_AudioSource &source, M_BufferInfo& buffer_info) {
    ssize_t output_idx = M_DequeueOutput(
            source.encoder,
            &buffer_info,
            100000); // Wait max 100ms for encoder finish
    sz_t output_size;
    sz_t buffer_size;
    byte_t *output_buffer;
    bool_t is_config_frame;
    bool_t finish = false;

    while (output_idx >= 0) {
        // Config frame contains no helpful data
        is_config_frame = buffer_info.flags & M_INFO_FLAG_CODEC_CONFIG;
        if (!is_config_frame && buffer_info.size > 0) {

            output_buffer = M_OutputBuffer(
                    source.encoder,
                    (sz_t)output_idx,
                    &output_size);
            buffer_size = buffer_info.size;

            if (output_buffer && output_size >= buffer_size) {
                HandleEncoded(
                        source,
                        output_buffer + buffer_info.offset,
                        buffer_info.size,
                        buffer_info.presentationTimeUs,
                        static_cast<int_t>(buffer_info.flags));
            }
        }

        // Remember to release buffer after finish
        M_ReleaseOutput(source.encoder, (sz_t)output_idx, false);

        // Check EOS
        finish = buffer_info.flags & M_INFO_FLAG_END_OF_STREAM;
        if (finish) {
            break;
        }

        // Flush all decoded data
        // Don't need to wait for encoder to finish (timeout = 0)
        // If timeout > 0 here, the encoded data will be late.
        output_idx = M_DequeueOutput(
                source.encoder,
                &buffer_info,
                0);
    }
    return finish;
}

static void EncodingLoop(M_AudioSource &source) {
    bool_t wait_record = true;
    bool_t finish = false;
    bool_t stopping = false;
    M_BufferInfo buffer_info;

    while (!finish) {
        if (wait_record) {
            stopping = Wait(source);
        }
        wait_record = EnqueueData(source, stopping);
        finish = DequeueData(source, buffer_info);
    }
}

static void HandleEncoded(
        M_AudioSource &source,
        const byte_t *data,
        sz_t size,
        tm_t presentation_time_us,
        int_t flags) {

    if (flags & M_INFO_FLAG_CODEC_CONFIG) {
        return;
    }

    if (size <= MAX_AUDIO_FRAME_SIZE)
        M_Enqueue(source.queue, data, size, presentation_time_us, flags);
    else
        LOGE(LOG_TAG, "Frame size is too large: %zu, skipped", size);
}

static void OnEncodedAvailable(void *context, const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame) {
    auto *source = static_cast<M_AudioSource *>(context);

    Lock(&source->listener_lock);
    for (auto & listener : source->listeners) {
        if (listener.callback != nullptr &&
            listener.context != nullptr) {
            listener.callback(
                    listener.context,
                    frame);
        }
    }
    Unlock(&source->listener_lock);
}


static void MarkStopped(M_AudioSource &source) {
    Store(&source.is_stopping, false);
    Store(&source.is_recording, false);
}

static void CleanUp(M_AudioSource &source) {
    if (source.stream) {
        M_RequestStop(source.stream);
        M_Close(source.stream);
        source.stream = nullptr; }

    if (source.encoder) {
        M_Stop(source.encoder);
        M_Delete(source.encoder);
        source.encoder = nullptr;
    }

    if (source.format) {
        M_Delete(source.format);
        source.format = nullptr;
    }

    LOGI("CleanUp", "gracefully clean up audio source");
}

static void OnRawAvailable(
        M_AudioSource &source,
        M_AStream *stream,
        void *audioData,
        int_t numSamples) {

    if (numSamples <= 0)
        return;

    auto *data = reinterpret_cast<byte_t *>(audioData);
    if (data == nullptr) {
        return;
    }

    sz_t data_size = numSamples * SIZE_PER_SAMPLE;

    // Write to NON-reading buffer (Double buffer technique)
    int_t index = 1 - SyncAndGet(&source.read_idx);
    PushBack(source.buffer[index], data, data_size);

    // Sync all data
    SetAndSync(&source.write_idx, index);

    // Notify waiting condition
    Signal(&source.condition);
}
