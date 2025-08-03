#pragma once

#include "media/video_source.h"
#include "media/audio_source.h"
#include "server/rtsp_server.h"

class MainController {
public:
    MainController() = default;
    ~MainController() = default;

    void start(bool start_video, bool start_audio);
    void stop();
    void onFrameAvailable(bool is_video, const uint8_t* data, size_t size, size_t offset, int64_t time, int32_t flags);

private:
    NativeVideoSource video_source_ {};
    NativeAudioSource audio_source_ {};
    RTSPServer rtsp_server_ { &video_source_, &audio_source_ };
};