#include "encoder/E_AAC.h"

#define LOG_TAG "AACEncoder"

static bool_t StartCodec(E_AAC &encoder);
static void EncodingLoop(E_AAC &encoder);
static void HandleEncoded(
        E_AAC &encoder,
        const byte_t *data,
        sz_t size,
        tm_t presentation_time_us,
        int_t flags);
static void OnFrameAvailable(void *context, const byte_t* data, sz_t size);

static void OnEncodedAvailable(void *context, const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame) {
    auto *source = static_cast<E_AAC *>(context);

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

void E_Init(E_AAC &encoder, M_AudioSource *source){
    for (auto & listener : encoder.listeners) {
        listener.callback = nullptr;
        listener.context = nullptr;
    }

    // Initialize frame queue
    encoder.queue_cb.context = &encoder;
    encoder.queue_cb.fnc = OnEncodedAvailable;
    E_Init(encoder.queue, &encoder.queue_cb);

    // Initialize synchronization primitives
    Init(&encoder.listener_lock);
    Init(&encoder.condition);
    Init(&encoder.buffer_lock);

    // Initialize thread
    Init(&encoder.thread);
    Init(&encoder.is_recording);
    Init(&encoder.is_stopping);

    // Initialize pointers
    encoder.codec = nullptr;
    encoder.format = nullptr;

    // Initialize source
    encoder.source = source;
}

static void MarkStopped(E_AAC &encoder) {
    Store(&encoder.is_stopping, false);
    Store(&encoder.is_recording, false);
}

static void CleanUp(E_AAC &encoder) {
    if (encoder.codec) {
        E_Stop(encoder.codec);
        E_Delete(encoder.codec);
        encoder.codec = nullptr;
    }

    if (encoder.format) {
        E_Delete(encoder.format);
        encoder.format = nullptr;
    }

    LOGI("CleanUp", "gracefully clean up AAC encoder");
}

static void StartEncoding(E_AAC &encoder) {
    SetThreadName("AACEncoder");

    M_AddListener(*encoder.source, OnFrameAvailable, &encoder);
    E_Start(encoder.queue);

    LOGI(LOG_TAG, "Audio encoder started successfully");
    EncodingLoop(encoder);

    // Stop the queue
    E_Stop(encoder.queue);

    M_RemoveListener(*encoder.source, &encoder);
    CleanUp(encoder);
}

static void *StartEncodingThread(void *arg) {
    auto *source = static_cast<E_AAC *>(arg);
    if (source) {
        StartEncoding(*source);
    }
    return nullptr;
}

// We mustn't reset listeners here
// Listener should add and remov itself manually.
static void Reset(E_AAC &encoder) {
    Reset(encoder.buffer[0]);
    Reset(encoder.buffer[1]);
    Reset(&encoder.read_idx);
    Reset(&encoder.write_idx);
}

void E_Start(E_AAC &encoder) {
    if (Load(&encoder.is_stopping)) {
        return;
    }

    if (GetAndSet(&encoder.is_recording, true)) {
        return; // Slready running
    }

    Reset(encoder);

    if (!encoder.source) {
        LOGE(LOG_TAG, "Audio source is null");
        MarkStopped(encoder);
        return;
    }

    if (!StartCodec(encoder)) {
        LOGE(LOG_TAG, "Failed to initialize AAC codec");
        MarkStopped(encoder);
        CleanUp(encoder);
        return;
    }

    Start(&encoder.thread, StartEncodingThread, &encoder);
}

void E_Stop(E_AAC &encoder) {
    if (!Load(&encoder.is_recording)) {
        return;
    }
    if (GetAndSet(&encoder.is_stopping, true)) {
        return;
    }
    Signal(&encoder.condition);

    Join(&encoder.thread);
    MarkStopped(encoder);
}

bool E_AddListener(E_AAC &encoder,
                   E_AACFrameCallback callback,
                   void *ctx){
    Lock(&encoder.listener_lock);
    for (auto & listener : encoder.listeners) {
        if (listener.callback == nullptr &&
            listener.context == nullptr) {
            listener.callback = callback;
            listener.context = ctx;
            Unlock(&encoder.listener_lock);
            Signal(&encoder.condition);
            return true;
        }
    }
    Unlock(&encoder.listener_lock);
    return false;
}

bool E_RemoveListener(E_AAC &encoder, void *ctx) {
    Lock(&encoder.listener_lock);
    for (auto & listener : encoder.listeners) {
        if (listener.context == ctx) {
            listener.callback = nullptr;
            listener.context = nullptr;
            Unlock(&encoder.listener_lock);
            return true;
        }
    }
    Unlock(&encoder.listener_lock);
    return false;
}

static bool_t StartCodec(E_AAC &encoder) {
    result_t result;

    // Create AAC encoder
    encoder.codec = E_CreateEncoder("audio/mp4a-latm");
    if (!encoder.codec) {
        LOGE(LOG_TAG, "Failed to create AAC encoder");
        return false;
    }

    // Create media format
    encoder.format = E_NewFormat();
    E_SetString(encoder.format, E_KEY_MIME, "audio/mp4a-latm");
    E_SetInt32(encoder.format, E_KEY_SAMPLE_RATE, AUDIO_SAMPLE_RATE);
    E_SetInt32(encoder.format, E_KEY_CHANNEL_COUNT, AUDIO_CHANNEL_COUNT);
    E_SetInt32(encoder.format, E_KEY_BIT_RATE, AUDIO_BIT_RATE);
    E_SetInt32(encoder.format, E_KEY_AAC_PROFILE, 2); // AAC-LC
    E_SetInt32(encoder.format, E_KEY_MAX_INPUT_SIZE, MAX_AUDIO_RECORD_SIZE);

    // Configure encoder
    result = E_Configure(
            encoder.codec,
            encoder.format,
            nullptr,
            nullptr,
            E_CODEC_CONFIGURE_ENCODE);
    if (result != E_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to configure encoder: %d", result);
        return false;
    }

    // Start encoder
    result = E_Start(encoder.codec);
    if (result != E_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to start encoder: %d", result);
        return false;
    }
    return true;
}

static bool_t HaveListeners(E_AAC &encoder) {
    Lock(&encoder.listener_lock);
    for (auto & listener : encoder.listeners) {
        if (listener.callback != nullptr) {
            Unlock(&encoder.listener_lock);
            return true;
        }
    }
    Unlock(&encoder.listener_lock);
    return false;
}

// Wait for frames or stpo signal
static bool_t Wait(E_AAC &encoder) {
    bool_t stopping = false;
    bool_t have_listeners = false;
    int_t write_old;
    int_t write_new;
    RecordBuffer* buffer;

    // Condition variable needs lock
    Lock(&encoder.buffer_lock);

    // First loop, wait for new frames and listener available
    while (true) {

        stopping = Load(&encoder.is_stopping);
        if (stopping) {
            break;
        }

        // We only run if there are listeners
        have_listeners = HaveListeners(encoder);
        if (have_listeners) {

            // Read all data happens before write_idx is set (OnRawAvailable)
            write_old = SyncAndGet(&encoder.write_idx);
            buffer = &encoder.buffer[write_old];

            // Break if read enough data
            if (buffer->size >= MAX_AUDIO_RECORD_SIZE) {
                break;
            }
        }

        Wait(&encoder.condition, &encoder.buffer_lock);
    }

    // Tell OnRawAvailable we are reading on write_old buffer,
    // It should write on the other buffer.
    if (!stopping) {
        SetAndSync(&encoder.read_idx, write_old);
    }

    // Second loop, wait for buffer to switch to write_new
    while (true) {
        write_new = SyncAndGet(&encoder.write_idx);
        stopping = Load(&encoder.is_stopping);

        if (write_old != write_new || stopping) {
            break;
        }

        Wait(&encoder.condition, &encoder.buffer_lock);
    }

    Unlock(&encoder.buffer_lock);
    return stopping;
}

static bool_t EnqueueData(E_AAC &encoder, bool_t stopping) {
    ssz_t input_idx;
    sz_t input_size;
    sz_t buffer_size;
    result_t result;
    int_t buffer_idx;
    bool_t wait_record = false;
    byte_t *input_buffer;

    // Get input buffer from encoder
    input_idx = E_DequeueInput(encoder.codec, 0);
    if (input_idx < 0) {
        return wait_record;
    }

    input_buffer = E_InputBuffer(
            encoder.codec,
            (sz_t)input_idx,
            &input_size);
    if (input_buffer == nullptr || input_size == 0) {
        return wait_record;
    }

    if (stopping) {
        E_QueueInput(
                encoder.codec,
                (sz_t) input_idx,
                0,
                0,
                -1,
                E_INFO_FLAG_END_OF_STREAM);
        return wait_record;
    }

    // Read all data happens before read_idx is set (after OnRawAvailable)
    buffer_idx = SyncAndGet(&encoder.read_idx);
    buffer_size = PopFront(
            encoder.buffer[buffer_idx],
            input_buffer,
            input_size);

    // Only wait if buffer is empty
    wait_record = Empty(encoder.buffer[buffer_idx]);

    result = E_QueueInput(
            encoder.codec,
            (sz_t) input_idx,
            0,
            buffer_size,
            NowMicros(),
            0);
    if (result != E_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to queue input buffer: %d", result);
    }

    return wait_record;
}

static bool_t DequeueData(E_AAC &encoder, E_BufferInfo& buffer_info) {
    ssize_t output_idx = E_DequeueOutput(
            encoder.codec,
            &buffer_info,
            100000); // Wait max 100ms for encoder finish
    sz_t output_size;
    sz_t buffer_size;
    byte_t *output_buffer;
    bool_t is_config_frame;
    bool_t finish = false;

    while (output_idx >= 0) {
        // Conifg frame contains no helpful data
        is_config_frame = buffer_info.flags & E_INFO_FLAG_CODEC_CONFIG;
        if (!is_config_frame && buffer_info.size > 0) {

            output_buffer = E_OutputBuffer(
                    encoder.codec,
                    (sz_t)output_idx,
                    &output_size);
            buffer_size = buffer_info.size;

            if (output_buffer && output_size >= buffer_size) {
                HandleEncoded(
                        encoder,
                        output_buffer + buffer_info.offset,
                        buffer_info.size,
                        buffer_info.presentationTimeUs,
                        static_cast<int_t>(buffer_info.flags));
            }
        }

        // Remember to release buffer after finish
        E_ReleaseOutput(encoder.codec, (sz_t)output_idx, false);

        // Check EOS
        finish = buffer_info.flags & E_INFO_FLAG_END_OF_STREAM;
        if (finish) {
            break;
        }

        // Flush all decoded data
        // Don't need to wait for encoder to finish (timeout = 0)
        // If timeout > 0 here, the encoded data will be late.
        output_idx = E_DequeueOutput(
                encoder.codec,
                &buffer_info,
                0);
    }
    return finish;
}

static void EncodingLoop(E_AAC &encoder) {
    bool_t wait_record = true;
    bool_t finish = false;
    bool_t stopping = false;
    E_BufferInfo buffer_info;

    while (!finish) {
        if (wait_record) {
            stopping = Wait(encoder);
        }
        wait_record = EnqueueData(encoder, stopping);
        finish = DequeueData(encoder, buffer_info);
    }
}

static void HandleEncoded(
        E_AAC &encoder,
        const byte_t *data,
        sz_t size,
        tm_t presentation_time_us,
        int_t flags) {

    if (flags & E_INFO_FLAG_CODEC_CONFIG) {
        return;
    }

    if (size <= MAX_AUDIO_FRAME_SIZE)
        E_Enqueue(encoder.queue, data, size, presentation_time_us, flags);
    else
        LOGE(LOG_TAG, "Frame size is too large: %zu, skipped", size);
}

static void OnFrameAvailable(void *context, const byte_t* data, sz_t size) {
    auto *encoder = static_cast<E_AAC *>(context);
    if (!encoder) {
        return;
    }

    // Write to NON-readign buffer (Double buffer technique)
    int_t index = 1 - SyncAndGet(&encoder->read_idx);
    PushBack(encoder->buffer[index], data, size);

    // Sync all data
    SetAndSync(&encoder->write_idx, index);

    // Notify waiting condition
    Signal(&encoder->condition);
}
