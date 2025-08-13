#include "server/S_AudioStream.h"
#include "server/S_Platform.h"
#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/Packetizer.h"

#define LOG_TAG "S_AudioStream"

static void* StartStreamingThread(void* arg);
static void FrameCallback(void* ctx, const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame);
static uint_t RtpTimestamp(const S_AACStream& stream, tm_t time_us);

static void Reset(S_AACStream& stream) {
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

void S_Init(S_AACStream& stream, E_AAC* encoder) {
    if (!encoder) {
        return;
    }
    Init(&stream.frame_mutex);
    Init(&stream.frame_condition);

    Init(&stream.thread);

    Store(&stream.state, IDLE);

    stream.encoder = encoder;

    Init(stream.stats, false);
}

void S_Prepare(S_AACStream& stream) {
    if (!CompareAndSet(&stream.state, IDLE, PREPARED))  {
        return;
    }
    // Add encoder listener
    E_AddListener(*stream.encoder, FrameCallback, &stream);
}

void S_Start(
        S_AACStream& stream,
        CancellableSocket* socket,
        byte_t interleave,
        int_t ssrc) {

    if (!CompareAndSet(&stream.state, PREPARED, RECORD))  {
        return;
    }

    Reset(stream);
    stream.socket = socket;
    stream.interleave = interleave;
    stream.ssrc = ssrc;
    stream.last_rtp_ts = RandomInt();

    Start(&stream.thread, StartStreamingThread, &stream);
}

static void MarkIdle(S_AACStream& stream) {
    Store(&stream.state, IDLE);
}

void S_Stop(S_AACStream& stream) {
    bool_t is_prepared = false;
    bool_t is_recording = false;

    if (CompareAndSet(&stream.state, RECORD, STOPPING)) {
        is_recording = true;
        is_prepared = true;
    }

    if (CompareAndSet(&stream.state, PREPARED, STOPPING)) {
        is_prepared = true;
    }

    if (is_recording) {
        // Wait for streaming thread to stop
        Signal(&stream.frame_condition);
        Join(&stream.thread);
    }

    if (is_prepared) {
        // Remove encoder listener
        E_RemoveListener(*stream.encoder, &stream);
    }

    if (is_recording || is_prepared) {
        LOGI("CleanUp", "gracefully clean up audio stream");
        MarkIdle(stream);
    }
}

// From outside perspective, PREPARED, RECORD and STOPPED is the same as RUNNING
bool_t S_IsRunning(const S_AACStream& stream) {
    return Load(&stream.state) != IDLE;
}

static bool_t WaitFrame(S_AACStream& stream, FrameBuffer<MAX_AUDIO_FRAME_SIZE>* &frame) {
    Lock(&stream.frame_mutex);
    int_t write_old;
    int_t write_new;
    bool_t new_frame;
    bool_t stopping;

    // First loop, wait for new frames or stopped
    while (true) {
        stopping = Load(&stream.state) == STOPPING;
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
        stopping = Load(&stream.state) == STOPPING;
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

static void SendReport(S_AACStream &stream) {
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

static void StartStreaming(S_AACStream& stream) {
    SetThreadName("AudioStream");

    FrameBuffer<MAX_AUDIO_FRAME_SIZE>* frame = nullptr;
    int_t read;
    uint_t rtp_ts;
    ushort_t seq = RandomShort();
    bool_t stopping = false;

    // Wait -> Packetize -> Send
    while (Load(&stream.state) == RECORD) {

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
}

static void ProcessFrame(S_AACStream& stream, const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame) {
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
    auto stream = static_cast<S_AACStream *>(arg);
    if (stream) {
        StartStreaming(*stream);
    }
    return nullptr;
}

static void FrameCallback(void* ctx, const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame) {
    auto stream = static_cast<S_AACStream*>(ctx);
    if (stream) {
        ProcessFrame(*stream, frame);
    }
}

static uint_t RtpTimestamp(const S_AACStream& stream, tm_t time_us) {
    tm_t delta = time_us - stream.last_time_us;
    return stream.last_rtp_ts + (uint_t)(delta * AUDIO_SAMPLE_RATE / 1'000'000);
}