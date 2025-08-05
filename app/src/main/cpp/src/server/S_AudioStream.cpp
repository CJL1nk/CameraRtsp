#include "server/S_AudioStream.h"
#include "server/S_Platform.h"
#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/Packetizer.h"

#define LOG_TAG "S_AudioStream"

static void* StartStreamingThread(void* arg);
static void FrameCallback(void* ctx, const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame);
static uint_t RtpTimestamp(const S_AudioStream& stream, tm_t time_us);

static void Reset(S_AudioStream& stream) {
    Reset(stream.frame_buffer[0]);
    Reset(stream.frame_buffer[1]);
    Reset(stream.socket_buffer);

    stream.last_time_us = 0;
    stream.ssrc = 0;
    stream.socket = nullptr;
    stream.last_rtp_ts = 0;
    stream.interleave = 0;

    stream.last_report_sec = 0;
    stream.packet_count = 0;
    stream.octet_count = 0;

    stream.frame_ready[0] = false;
    stream.frame_ready[1] = false;
    Reset(&stream.write_idx);
    Reset(&stream.read_idx);
}

void S_Init(S_AudioStream& stream, M_AudioSource* audio_source) {
    if (!audio_source) {
        return;
    }
    Init(&stream.frame_mutex);
    Init(&stream.frame_condition);

    Init(&stream.thread);
    Init(&stream.is_running);
    Init(&stream.is_stopping);

    stream.audio_source = audio_source;
    Init(stream.stats, false);
}

void S_Start(
        S_AudioStream& stream,
        CancellableSocket* socket,
        byte_t interleave,
        int_t ssrc) {

    if (Load(&stream.is_stopping)) {
        return;
    }

    if (GetAndSet(&stream.is_running, true)) {
        return; // Already running
    }

    Reset(stream);
    stream.socket = socket;
    stream.interleave = interleave;
    stream.ssrc = ssrc;
    stream.last_rtp_ts = RandomInt();

    Start(&stream.thread, StartStreamingThread, &stream);
}

static void MarkStopped(S_AudioStream& stream) {
    Store(&stream.is_running, false);
    Store(&stream.is_stopping, false);
}

void S_Stop(S_AudioStream& stream) {
    if (!Load(&stream.is_running)) {
        return;
    }
    if (GetAndSet(&stream.is_stopping, true)) {
        return;
    }
    Signal(&stream.frame_condition);
    Join(&stream.thread);
    MarkStopped(stream);
}

bool_t S_IsRunning(const S_AudioStream& stream) {
    return Load(&stream.is_running);
}

static bool_t WaitFrame(S_AudioStream& stream, FrameBuffer<MAX_AUDIO_FRAME_SIZE>* &frame) {
    Lock(&stream.frame_mutex);
    int_t write_old;
    int_t write_new;
    bool_t new_frame;
    bool_t stopping;

    // First loop, wait for new frames or stopped
    while (true) {
        stopping = Load(&stream.is_stopping);
        write_old = SyncAndGet(&stream.write_idx);
        new_frame = stream.frame_ready[write_old] &&
                    stream.frame_buffer[write_old].timeUs > stream.last_time_us;
        if (new_frame || stopping) {
            break;
        }
        Wait(&stream.frame_condition, &stream.frame_mutex);
    }

    if (stopping) {
        Unlock(&stream.frame_mutex);
        return true;
    }

    // The writer we are reading write_old buffer,
    // it should write to the other buffer.
    SetAndSync(&stream.read_idx, write_old);

    // Second loop, wait for writer update write_idx
    while (true) {
        stopping = Load(&stream.is_stopping);
        write_new = SyncAndGet(&stream.write_idx);
        if (write_old != write_new || stopping) {
            break;
        }
        Wait(&stream.frame_condition, &stream.frame_mutex);
    }

    // Mark read
    frame = &stream.frame_buffer[write_old];
    stream.frame_ready[write_old] = false;

    Unlock(&stream.frame_mutex);
    return stopping;
}

static void SendReport(S_AudioStream &stream) {
    int_t read;
    tm_t now = NowSecs();

    if (stream.packet_count >= 50 && // Any number is OK, just don't too big
        stream.last_report_sec != now && now % 2 == 0) {
        stream.last_report_sec = now;

        // RTCP uses interleave + 1
        read = PacketizeReport(stream.interleave + 1,
                               stream.socket_buffer.data,
                               stream.ssrc,
                               stream.last_rtp_ts,
                               stream.packet_count,
                               stream.octet_count);
        Send(*stream.socket,
             stream.socket_buffer.data,
             read, 0);
        Send(*stream.socket,
             stream.socket_buffer.data,
             read, 0);
    }
}

static void StartStreaming(S_AudioStream& stream) {
    SetThreadName("AudioStream");

    FrameBuffer<MAX_AUDIO_FRAME_SIZE>* frame = nullptr;
    int_t read;
    uint_t rtp_ts;
    ushort_t seq = RandomShort();
    bool_t stopping = false;

    // Add source listener
    M_AddListener(*stream.audio_source, FrameCallback, &stream);

    // Wait -> Packetize -> Send
    while (!Load(&stream.is_stopping)) {

        stopping = WaitFrame(stream, frame);
        if (stopping) {
            break;
        }

        StartProcess(stream.stats);
        rtp_ts = RtpTimestamp(stream, frame->timeUs);
        read = PacketizeAAC(
            stream.interleave,
            seq,
            rtp_ts,
            stream.ssrc,
            *frame,
            stream.socket_buffer.data,
            RTP_MAX_PACKET_SIZE
        );
        EndProcess(stream.stats);

        if (read < 0) {
            LOGE(LOG_TAG, "Failed to packetize audio frame");
            break;
        }
        
        if (Send(*stream.socket,
                 stream.socket_buffer.data,
                 read, 0) < 0) {
            LOGE(LOG_TAG, "Failed to send audio frame");
            break;
        }
        
        stream.last_time_us = frame->timeUs;
        stream.last_rtp_ts = rtp_ts;
        stream.packet_count++;
        stream.octet_count += read - RtpPayloadStart();

        seq = (seq + 1) % 65536;

        // RTCP Sender Report
        SendReport(stream);

        // Stats
        SendFrame(stream.stats);
    }

    // Remove source listener
    M_RemoveListener(*stream.audio_source, &stream);
    LOGI("CleanUp", "gracefully clean up audio stream");
}

static void ProcessFrame(S_AudioStream& stream, const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame) {
    int index = 1 - SyncAndGet(&stream.read_idx);
    stream.frame_buffer[index] = frame;
    stream.frame_ready[index] = true;

    // Stats
    ReceiveFrame(stream.stats);

    // Ensure all data is synced when read write_idx
    SetAndSync(&stream.write_idx, index);
    Signal(&stream.frame_condition);
}

static void* StartStreamingThread(void* arg) {
    auto stream = static_cast<S_AudioStream *>(arg);
    if (stream) {
        StartStreaming(*stream);
    }
    return nullptr;
}

static void FrameCallback(void* ctx, const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame) {
    auto stream = static_cast<S_AudioStream*>(ctx);
    if (stream) {
        ProcessFrame(*stream, frame);
    }
}

static uint_t RtpTimestamp(const S_AudioStream& stream, tm_t time_us) {
    tm_t delta = time_us - stream.last_time_us;
    return stream.last_rtp_ts + (uint_t)(delta * AUDIO_SAMPLE_RATE / 1'000'000);
}