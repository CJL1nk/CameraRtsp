#pragma once

#include "jni.h"
#include "video_stream.h"
#include "audio_stream.h"

class RtpSession {
public:
    RtpSession() = default;
    ~RtpSession() = default;

    void start(int32_t socket, int8_t video_interleave, int8_t audio_interleave) {
        if (video_interleave >= 0) {
            video_stream_.start(socket, video_interleave);
        }
        if (audio_interleave >= 0) {
            audio_stream_.start(socket, audio_interleave);
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
    VideoStream video_stream_ {};
    AudioStream audio_stream_ {};
};
