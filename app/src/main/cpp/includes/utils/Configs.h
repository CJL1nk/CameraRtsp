#pragma once

// Audio frame queue config
#define MAX_AUDIO_FRAME_QUEUE_SIZE 30

// Audio record config
#define MAX_AUDIO_RECORD_SAMPLE 4096
#define SIZE_PER_SAMPLE (sizeof(int16_t) * AUDIO_CHANNEL_COUNT)
#define MAX_AUDIO_RECORD_SIZE (MAX_AUDIO_RECORD_SAMPLE * SIZE_PER_SAMPLE)
#define MAX_AUDIO_LISTENER 1

// Video record config
#define VIDEO_WIDTH 1280
#define VIDEO_HEIGHT 720
#define VIDEO_DEFAULT_FRAME_RATE 30 // Query camera_id supported frame rate
#define VIDEO_MIN_FRAME_RATE 15     // Query camera_id supported frame rate
#define CAMERA_ID "0"
#define MAX_VIDEO_LISTENER 1
#define IMAGE_READER_CACHE_SIZE 1

// Audio encoder config
#define AUDIO_SAMPLE_RATE 44100 // Config
#define AUDIO_CHANNEL_COUNT 1   // Config
#define AUDIO_BIT_RATE 64000    // Config

// Video encoder config
#define VIDEO_SAMPLE_RATE 90000 // H264/H265 standard
#define VIDEO_BIT_RATE 2000000
#define VIDEO_IFRAME_INTERVAL 1
#define VIDEO_CODEC_PROFILE 1
#define VIDEO_CODEC_LEVEL 2097152
#define H265_PARAMS_SIZE 64

// Buffer config
#define MAX_AUDIO_FRAME_SIZE 512      // NORMAL_AUDIO_FRAME_SIZE x 2
#define NORMAL_AUDIO_FRAME_SIZE 256   // 64kbps / (44100Hz / 1024 samples per frame) frames / 8 bits per byte
#define MAX_VIDEO_FRAME_SIZE 128000   // Keyframe: normal frame x 4)
#define NORMAL_VIDEO_FRAME_SIZE 32000 // Normal frame: 3Mbps / 15 frames per second / 8 bits per byte

// RTSP Config
#define RTSP_PORT 8554
#define RTSP_MAX_CONNECTIONS 1
#define RTSP_VIDEO_INTERLEAVE 0
#define RTSP_AUDIO_INTERLEAVE 2

// RTP config
#define RTP_MAX_PACKET_SIZE 1024
#define AAC_PAYLOAD_TYPE 96
#define H265_PAYLOAD_TYPE 97

// Stats config
#define STATS_LOG_INTERVAL 10000