#include "server/S_Platform.h"
#include "server/S_VideoStream.h"
#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/Packetizer.h"

#define LOG_TAG "S_VideoStream"

// Frame types
#define NO_FRAME 0
#define IFRAME 1
#define NON_IFRAME 2

static void* StartStreamingThread(void* arg);
static void FrameCallback(void* ctx, const FrameBuffer<MAX_VIDEO_FRAME_SIZE>& frame);
static bool_t SendAndAdvance(
        S_VideoStream &stream,
        ushort_t &seq,
        tm_t frame_time_us,
        const byte_t *data,
        sz_t size);
static int_t PacketizeAndSend(S_VideoStream& stream,
                              ushort_t& seq,
                              uint_t rtp_ts,
                              const byte_t *data,
                              sz_t size);
static uint_t RtpTimestamp(const S_VideoStream& stream, tm_t time_us);

// Attributes that are initialized every new session.
static void Reset(S_VideoStream& stream) {
    Reset(stream.frame_buffer[0]);
    Reset(stream.frame_buffer[1]);
    Reset(stream.keyframe_buffer[0]);
    Reset(stream.keyframe_buffer[1]);
    Reset(stream.socket_buffer);

    stream.last_time_us = 0;
    stream.ssrc = 0;
    stream.socket = nullptr;
    stream.last_rtp_ts = 0;
    stream.interleave = 0;

    stream.last_report_sec = 0;
    stream.packet_count = 0;
    stream.octet_count = 0;

    stream.frame_ready[0] = NO_FRAME;
    stream.frame_ready[1] = NO_FRAME;
    Reset(&stream.write_idx);
    Reset(&stream.read_idx);
}

// Attributes that are only initialized once per app cycle.
void S_Init(S_VideoStream& stream, M_VideoSource* video_source) {
    if (!video_source) {
        return;
    }

    Init(&stream.frame_mutex);
    Init(&stream.frame_condition);
    Init(&stream.thread);
    Init(&stream.is_stopping);
    Init(&stream.is_running);

    stream.video_source = video_source;
    Init(stream.stats, true);
}

void S_Start(
        S_VideoStream& stream,
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

static void MarkStopped(S_VideoStream& stream) {
    Store(&stream.is_running, false);
    Store(&stream.is_stopping, false);
}

void S_Stop(S_VideoStream& stream) {
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

bool_t S_IsRunning(const S_VideoStream& stream) {
    return Load(&stream.is_running);
}

static void ProcessFrame(S_VideoStream& stream, const FrameBuffer<MAX_VIDEO_FRAME_SIZE>& frame) {

    int index = 1 - SyncAndGet(&stream.read_idx);
    
    if (frame.flags & M_INFO_FLAG_KEY_FRAME) {
        stream.keyframe_buffer[index] = frame;
        stream.frame_ready[index] = IFRAME;

    } else if (frame.size <= NORMAL_VIDEO_FRAME_SIZE) {
        auto non_key = &stream.frame_buffer[index];
        Copy(non_key->data, frame.data, frame.size);
        non_key->timeUs = frame.timeUs;
        non_key->size = frame.size;
        non_key->flags = frame.flags;
        stream.frame_ready[index] = NON_IFRAME;

    } else {
        LOGE(LOG_TAG, "Invalid non-key frame size %zu", frame.size);
        return;
    }

    // Stats
    ReceiveFrame(stream.stats);

    // Ensure all data is synced when read write_idx
    SetAndSync(&stream.write_idx, index);
    Signal(&stream.frame_condition);
}

static bool_t Wait(
        S_VideoStream& stream,
        int_t &type,
        FrameBuffer<MAX_VIDEO_FRAME_SIZE>* &keyframe,
        FrameBuffer<NORMAL_VIDEO_FRAME_SIZE>* &frame) {
    Lock(&stream.frame_mutex);

    tm_t buffer_time;
    int_t write_old;
    int_t write_new;
    bool_t stopping;
    bool_t new_frame;

    // First loop: Wait for frames or stopped
    while (true) {
        write_old = SyncAndGet(&stream.write_idx);
        type = stream.frame_ready[write_old];
        buffer_time =
                type == IFRAME ? stream.keyframe_buffer[write_old].timeUs :
                type == NON_IFRAME ? stream.frame_buffer[write_old].timeUs :
                0;

        new_frame = stream.last_time_us < buffer_time &&
                    stream.keyframe_buffer[write_old].size > 0;
        stopping = Load(&stream.is_stopping);
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

    // Read data
    type = stream.frame_ready[write_old];
    keyframe = &stream.keyframe_buffer[write_old];
    frame = &stream.frame_buffer[write_old];

    // Mark as read
    stream.frame_ready[write_old] = NO_FRAME;
    Unlock(&stream.frame_mutex);
    return stopping;
}

static void StartStreaming(S_VideoStream& stream) {
    M_AddListener(*stream.video_source, FrameCallback, &stream);

    FrameBuffer<MAX_VIDEO_FRAME_SIZE>* keyframe;
    FrameBuffer<NORMAL_VIDEO_FRAME_SIZE>* frame;
    int_t type;
    ushort_t seq = RandomShort();
    bool_t stopping = false;
    bool_t success = true;

    // Wait -> SendAndAdvance -> PacketizeAndSend
    while (!Load(&stream.is_stopping)) {
        
        stopping = Wait(stream, type, keyframe, frame);
        if (stopping) {
            break;
        }

        if (type == IFRAME) {
            success = SendAndAdvance(
                    stream,
                    seq,
                    keyframe->timeUs,
                    keyframe->data,
                    keyframe->size);

        } else if (type == NON_IFRAME) {
            // Re-send keyframe if missed
            if (stream.last_time_us < keyframe->timeUs) {
                success = SendAndAdvance(
                        stream,
                        seq,
                        keyframe->timeUs,
                        keyframe->data,
                        keyframe->size);
            }

            if (success) {
                success = SendAndAdvance(
                        stream,
                        seq,
                        frame->timeUs,
                        frame->data,
                        frame->size);
            }
        }

        if (!success) {
            break;
        }
    }

    M_RemoveListener(*stream.video_source, &stream);
    LOGI("CleanUp", "gracefully clean up video stream");
}

static void* StartStreamingThread(void* arg) {
    auto stream = static_cast<S_VideoStream*>(arg);
    if (stream) {
        StartStreaming(*stream);
    }
    return nullptr;
}

static void FrameCallback(void* ctx, const FrameBuffer<MAX_VIDEO_FRAME_SIZE>& frame) {
    auto stream = static_cast<S_VideoStream*>(ctx);
    if (stream) {
        ProcessFrame(*stream, frame);
    }
}

static uint_t RtpTimestamp(const S_VideoStream& stream, tm_t time_us) {
    tm_t delta = time_us - stream.last_time_us;
    return stream.last_rtp_ts + (uint_t)(delta * VIDEO_SAMPLE_RATE / 1000000);
}

static void SendReport(S_VideoStream & stream) {
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
    }
}

static bool_t SendAndAdvance(
        S_VideoStream &stream,
        ushort_t &seq,
        tm_t frame_time_us,
        const byte_t *data,
        sz_t size) {

    uint_t key_rtp_ts = RtpTimestamp(stream, frame_time_us);
    if (PacketizeAndSend(stream,
                         seq,
                         key_rtp_ts,
                         data,
                         size) < 0) {
        return false;
    }
    stream.last_time_us = frame_time_us;
    stream.last_rtp_ts = key_rtp_ts;

    // RTCP Sender Report
    SendReport(stream);

    // Stats
    SendFrame(stream.stats);
    return true;
}

static int_t PacketizeAndSend(
        S_VideoStream& stream,
        ushort_t& seq,
        uint_t rtp_ts,
        const byte_t* data,
        sz_t size) {

    NalUnit nals[16];
    sz_t count;
    sz_t i;
    sz_t offset;
    int_t read;
    bool_t stopping;

    StartProcess(stream.stats);
    count = ExtractNal(data, 0, size, nals, 16);
    PauseProcess(stream.stats);

    for (i = 0; i < count; ++i) {
        const NalUnit& nal = nals[i];

        offset = nal.start;
        stopping = Load(&stream.is_stopping);
        while (!stopping && offset < nal.end) {

            ResumeProcess(stream.stats);
            // This function also updates offset
            read = PacketizeH265(
                stream.interleave,
                seq,
                rtp_ts,
                stream.ssrc,
                data,
                size,
                offset,
                nal,
                stream.socket_buffer.data,
                RTP_MAX_PACKET_SIZE
            );
            PauseProcess(stream.stats);
            
            if (read < 0) {
                LOGE(LOG_TAG, "Failed to packetize video frame");
                return -1;
            }
            
            if (Send(*stream.socket,
                     stream.socket_buffer.data, read,
                     0) < 0) {
                LOGE(LOG_TAG, "Failed to send video frame"); 
                return -1;
            }

            stream.packet_count++;
            stream.octet_count += read - RtpPayloadStart();
            
            seq = (seq + 1) % 65536;
        }
    }

    EndProcess(stream.stats);

    return 0;
}
