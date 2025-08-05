#include "server/S_RtpSession.h"
#include "utils/Platform.h"

void S_Init(S_RtpSession& session, M_AudioSource* audio_source, M_VideoSource* video_source) {
    S_Init(session.audio_stream, audio_source);
    S_Init(session.video_stream, video_source);
}

void S_Start(S_RtpSession& session,
             CancellableSocket* socket,
             int_t video_interleave,
             int_t audio_interleave) {

    if (video_interleave >= 0) {
        S_Start(session.video_stream,
                socket,
                video_interleave,
                RandomInt());
    }
    if (audio_interleave >= 0) {
        S_Start(session.audio_stream,
                socket,
                audio_interleave,
                RandomInt());
    }
}

void S_Stop(S_RtpSession& session) {
    S_Stop(session.video_stream);
    S_Stop(session.audio_stream);
}

bool_t S_IsRunning(const S_RtpSession& session) {
    bool_t video_running = S_IsRunning(session.video_stream);
    bool_t audio_running = S_IsRunning(session.audio_stream);
    return video_running && audio_running;
}
