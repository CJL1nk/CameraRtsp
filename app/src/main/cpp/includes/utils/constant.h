#pragma once

#define BUFFER_FLAG_KEY_FRAME 0x01 // MediaCodec
#define BUFFER_FLAG_CODEC_CONFIG 0x02 // MediaCodec

#define AUDIO_SAMPLE_RATE 44100 // Config
#define AUDIO_CHANNEL_COUNT 1 // Config
#define AUDIO_BIT_RATE 64000 // Config
#define MAX_AUDIO_FRAME_SIZE 512 // NORMAL_AUDIO_FRAME_SIZE x 2
#define NORMAL_AUDIO_FRAME_SIZE 256 // 64kbps / (44100Hz / 1024 samples per frame) frames / 8 bits per byte
#define AAC_PAYLOAD_TYPE 96

#define VIDEO_SAMPLE_RATE 90000 // H264/H265 standard
#define MAX_VIDEO_FRAME_SIZE 102400 // Keyframe: normal frame x 5)
#define NORMAL_VIDEO_FRAME_SIZE 20480 // Normal frame: 2Mbps / 24 frames per second / 8 bits per byte
#define H265_PAYLOAD_TYPE 97

#define RTP_MAX_PACKET_SIZE 1024
#define RTP_HEADER_SIZE 12
#define TCP_PREFIX_SIZE 4
#define RTP_VERSION 0x80

#define AAC_AU_HEADER_SIZE 2
#define AAC_AU_SIZE 2

#define H265_PAYLOAD_HEADER_SIZE 2
#define H265_FU_HEADER_SIZE 1
#define H265_FU_PAYLOAD_TYPE 49
