#include "mediasource/M_AudioSource.h"
#include "mediasource/M_Platform.h"

#define LOG_TAG "AudioSource"


static void CleanUp(M_AudioSource &source) {
    if (source.stream) {
        M_RequestStop(source.stream);
        M_Close(source.stream);
        source.stream = nullptr;
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
    for (auto & listener : source.listeners) {
        if (listener.context && listener.callback) {
            listener.callback(listener.context, data, data_size);
        }
    }
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

    if (!Load(&source->running)) {
        return M_AUDIO_CALLBACK_RESULT_STOP;
    }

    OnRawAvailable(*source, stream, audioData, numSamples);
    return M_AUDIO_CALLBACK_RESULT_CONTINUE;
}

static bool OpenStream(M_AudioSource &source) {
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

void M_Init(M_AudioSource &source) {
    for (auto & listener : source.listeners) {
        listener.callback = nullptr;
        listener.context = nullptr;
    }
    Init(&source.running);
}

void M_Start(M_AudioSource &source) {
    if (GetAndSet(&source.running, true)) {
        return; // Already running
    }

    if (!OpenStream(source)) {
        LOGE(LOG_TAG, "Failed to create audio stream");
        M_Stop(source);
        return;
    }

    auto result = M_RequestStart(source.stream);
    if (result != M_RESULT_OK) {
        LOGE(LOG_TAG, "Failed to start audio stream: %d", result);
        M_Stop(source);
        return;
    }

    LOGI(LOG_TAG, "Audio source started successfully");
}

void M_Stop(M_AudioSource &source) {
    if (!GetAndSet(&source.running, false)) {
        return; // Already stopped
    }
    CleanUp(source);
}

bool M_AddListener(M_AudioSource &source, M_AFrameCallback callback, void *ctx) {
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



