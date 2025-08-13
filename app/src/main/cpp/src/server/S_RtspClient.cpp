#include "server/S_RtspClient.h"

#define MAX_BUFFER_LEN 2048
#define CLIENT_ID_LEN 10
#define MAX_SDP_LEN 1024

#define SEQ_KEYWORD "CSeq:"
#define TRACK_ID_KEYWORD "trackID="
#define LOG_TAG "RTSPClient"

void S_Init(S_RtspClient& client,
            S_RtspMedia* media,
            E_H265* video_encoder,
            E_AAC* audio_encoder) {
    static int_t i = 0;
    S_Init(client.rtp_session, video_encoder, audio_encoder);

    client.media = media;
    client.id = i++;

    Init(&client.thread);
    Init(&client.thread_start);
}

int_t S_Accept(S_RtspClient& client, const CancellableSocket& server_socket) {
    return Accept(client.socket, server_socket);
}

static int_t HandleRequest(S_RtspClient &client,
                           char_t *res_buf,
                           sz_t res_size,
                           char_t *recv_buf,
                           sz_t recv_size,
                           char_t *client_ip,
                           char_t *client_id_str);
static int_t FindTrackId(const char_t* request);
static int_t FindCSeq(const char_t* request);
static sz_t PrepareSdp(const S_RtspMedia* media,
                       const char_t* client_ip,
                       char_t* sdp,
                       sz_t size);

static void StartListen(S_RtspClient& client) {
    char_t recv_buf[MAX_BUFFER_LEN];
    char_t res_buf[MAX_BUFFER_LEN];
    char_t client_ip[SOCKET_ADDR_LEN];
    char_t client_id_str[CLIENT_ID_LEN];

    GetSocketAddr(client.socket, client_ip, SOCKET_ADDR_LEN);
    WriteStream(client_id_str, CLIENT_ID_LEN, "client_%d", client.id);

    LOGI(LOG_TAG, "Client %s connected", client_ip);

    // This function will prepare the metadata for sdp
    S_Prepare(client.rtp_session,
              client.media->video_idx >= 0,
              client.media->audio_idx >= 0);

    while (true) {
        if (Wait(client.socket) < 0) {
            break;
        }

        if (HandleRequest(
                client,
                res_buf,
                MAX_BUFFER_LEN,
                recv_buf,
                MAX_BUFFER_LEN,
                client_ip,
                client_id_str) < 0) {
            break;
        }
    }

    S_Stop(client.rtp_session);
    Destroy(client.socket);
    LOGI(LOG_TAG, "Client %s disconnected, exiting listening loop.", client_ip);
}

static void* StartClientThread(void* arg) {
    S_RtspClient* client = static_cast<S_RtspClient*>(arg);
    if (client) {
        StartListen(*client);
    }
    return nullptr;
}

void S_Start(S_RtspClient& client) {
    if (GetAndSet(&client.thread_start, true)) {
        Join(&client.thread); // Wait for previous thread to exit (if any)
    }
    Start(&client.thread, StartClientThread, &client);
}

bool_t S_IsConnected(const S_RtspClient& client) {
    return IsConnected(client.socket) && S_IsRunning(client.rtp_session);
}

void S_Stop(S_RtspClient& client) {
    if (S_IsConnected(client)) {
        Interrupt(client.socket);
    }
    if (GetAndSet(&client.thread_start, false)) {
        Join(&client.thread);
    }
}

static int_t HandleRequest(S_RtspClient &client,
                           char_t *res_buf,
                           sz_t res_size,
                           char_t *recv_buf,
                           sz_t recv_size,
                           char_t *client_ip,
                           char_t *client_id_str) {
    ssz_t received;
    char_t sdp_buffer[MAX_SDP_LEN];
    sz_t sdp_length;
    int_t track_id;
    int_t interleave;
    int_t cseq;
    const char_t* transport;
    S_RtspMedia* media;

    // Read the request
    received = Receive(client.socket,
                       recv_buf,
                       recv_size - 1, // -1 is for "\0"
                       0);
    if (received <= 0) {
        return -1;
    }
    recv_buf[received] = '\0';

    // Parse request
    media = client.media;
    track_id = FindTrackId(recv_buf);

    cseq = FindCSeq(recv_buf);
    if (cseq < 0) {
        // Can be RTCP/TCP request while streaming
        // But we don't handle it for now
        LOGI(LOG_TAG, "Encounter non RTSP request");
        return 0;
    }

    if (FindSubString(recv_buf, "OPTIONS")) {
        WriteStream(res_buf,
                    res_size,
                    "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
                    "\r\n",
                    cseq);

    } else if (FindSubString(recv_buf, "DESCRIBE")) {
        sdp_length = PrepareSdp(
                client.media,
                client_ip,
                sdp_buffer,
                MAX_SDP_LEN);
        WriteStream(res_buf,
                    res_size,
                    "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Content-Type: application/sdp\r\n"
                    "Content-Length: %zu\r\n"
                    "\r\n"
                    "%s",
                    cseq,
                    sdp_length,
                    sdp_buffer);

    } else if (FindSubString(recv_buf, "SETUP") && track_id >= 0) {
        transport = FindSubString(recv_buf, "Transport: RTP/AVP/TCP");
        interleave = track_id == media->audio_idx ? media->audio_interleave :
                     track_id == media->video_idx ? media->video_interleave : -1;

        // Only support TCP
        if (transport) {
            WriteStream(res_buf,
                        res_size,
                        "RTSP/1.0 200 OK\r\n"
                        "CSeq: %d\r\n"
                        "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n"
                        "Session: %s\r\n"
                        "\r\n",
                        cseq,
                        interleave, interleave + 1,
                        client_id_str);

        } else {
            WriteStream(res_buf,
                        res_size,
                        "RTSP/1.0 461 Unsupported Transport\r\n"
                        "CSeq: %d\r\n"
                        "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
                        "Supported: Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n"
                        "\r\n",
                        cseq,
                        interleave, interleave + 1);
        }

    } else if (FindSubString(recv_buf, "PLAY")) {
        S_Start(client.rtp_session,
                &client.socket,
                media->video_interleave,
                media->audio_interleave);

        WriteStream(res_buf,
                    res_size,
                    "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Session: %s\r\n"
                    "\r\n",
                    cseq,
                    client_id_str);

    } else if (FindSubString(recv_buf, "TEARDOWN")) {
        S_Stop(client.rtp_session);

        WriteStream(res_buf,
                    res_size,
                    "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "\r\n",
                    cseq);

    } else {
        WriteStream(res_buf,
                    res_size,
                    "RTSP/1.0 501 Not Implemented\r\n"
                    "CSeq: %d\r\n"
                    "\r\n",
                    cseq);
    }

    return Send(
            client.socket,
            res_buf,
            Len(res_buf),
            0);
}

// Find first number after TRACK_ID_KEYWORD
static int_t FindTrackId(const char_t* request) {
    const char_t* pos = FindSubString(request, TRACK_ID_KEYWORD);
    if (!pos) {
        return -1;
    }

    pos += Len(TRACK_ID_KEYWORD);
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    return Int(pos);
}

// Find first number after SEQ_KEYWORD
static int_t FindCSeq(const char_t* request) {
    const char_t* pos = FindSubString(request, SEQ_KEYWORD);
    if (!pos) {
        return -1;
    }

    pos += Len(SEQ_KEYWORD);
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }

    return Int(pos);
}

static sz_t PrepareSdp(const S_RtspMedia* media,
                       const char_t* client_ip,
                       char_t* sdp,
                       sz_t size) {
    sz_t offset = 0;
    char_t vps[256], sps[256], pps[256];

    if (!media) {
        return 0;
    }

    offset += WriteStream(sdp + offset, size - offset,
                          "v=0\r\n"
                          "o=- 0 0 IN IP4 127.0.0.1\r\n"
                          "s=Camera Stream\r\n"
                          "c=IN IP4 %s\r\n"
                          "t=0 0\r\n"
                          "a=control:*\r\n",
                          client_ip);

    if (media->video_idx >= 0) {

        E_GetParams(*media->video_encoder, vps, sps, pps);
        offset += WriteStream(sdp + offset, size - offset,
                              "\r\n"
                              "m=video 0 RTP/AVP %d\r\n"
                              "a=rtpmap:%d H265/%d\r\n"
                              "a=fmtp:%d sprop-vps=%s;sprop-sps=%s;sprop-pps=%s\r\n"
                              "a=control:trackID=%d\r\n",
                              H265_PAYLOAD_TYPE,
                              H265_PAYLOAD_TYPE, VIDEO_SAMPLE_RATE,
                              H265_PAYLOAD_TYPE, vps, sps, pps,
                              media->video_idx);
    }

    if (media->audio_idx >= 0) {
        offset += WriteStream(sdp + offset, size - offset,
                              "\r\n"
                              "m=audio 0 RTP/AVP %d\r\n"
                              "a=rtpmap:%d MPEG4-GENERIC/%d/%d\r\n"
                              "a=fmtp:%d streamtype=5; profile-level-id=15; mode=AAC-hbr; config=1208; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
                              "a=control:trackID=%d\r\n",
                              AAC_PAYLOAD_TYPE,
                              AAC_PAYLOAD_TYPE, AUDIO_SAMPLE_RATE, AUDIO_CHANNEL_COUNT,
                              AAC_PAYLOAD_TYPE,
                              media->audio_idx);
    }

    sdp[offset] = '\0';
    return offset;
}
