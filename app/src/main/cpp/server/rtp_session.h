#pragma once

#include "jni.h"
#include "video_stream.h"
#include "audio_stream.h"
#include "utils/server_utils.h"

class RtpSession {
public:
    explicit RtpSession(NativeVideoSource* video_source, NativeAudioSource* audio_source)
            : video_stream_(VideoStream {video_source}), audio_stream_(AudioStream {audio_source}) {};
    ~RtpSession() = default;

    void start(int32_t socket, int8_t video_interleave, int8_t audio_interleave) {
        if (video_interleave >= 0) {
            video_stream_.start(socket, video_interleave, genSSRC());
        }
        if (audio_interleave >= 0) {
            audio_stream_.start(socket, audio_interleave, genSSRC());
        }
    }
    void stop() {
        video_stream_.stop();
        audio_stream_.stop();
    };
    bool isRunning() const {
        return audio_stream_.isRunning() || video_stream_.isRunning();
    }

private:
    VideoStream video_stream_;
    AudioStream audio_stream_;
};
