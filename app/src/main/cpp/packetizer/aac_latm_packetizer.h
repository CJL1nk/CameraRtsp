#pragma once

#include <jni.h>
#include "packetizer/packetizer.h"
#include "source/audio_source.h"
#include "utils/frame_buffer.h"

#define AAC_AU_HEADER_SIZE 2
#define AAC_AU_SIZE 2
#define AAC_PAYLOAD_TYPE 96

class AacLatmPacketizer {
public:
    using Frame = FrameBuffer<MAX_AUDIO_FRAME_SIZE>;
    explicit AacLatmPacketizer(uint8_t itl, int32_t ssrc) : interleave_(itl), ssrc_(ssrc) {}
    ~AacLatmPacketizer() = default;
    int32_t packetizeFrame(
        int32_t seq, uint32_t timestamp,
        const Frame& src,
        uint8_t* dst, size_t dst_size
    ) const;

private:
    uint8_t interleave_ = 0;
    int32_t ssrc_ = 0;
};
