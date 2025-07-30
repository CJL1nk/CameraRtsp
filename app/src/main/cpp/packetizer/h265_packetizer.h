#pragma once

#include <jni.h>
#include <array>
#include "packetizer/packetizer.h"
#include "source/video_source.h"
#include "utils/frame_buffer.h"
#include "utils/h265_nal_unit.h"

#define H265_PAYLOAD_TYPE 97
#define PAYLOAD_HEADER_SIZE 2
#define FU_HEADER_SIZE 1
#define FRAGMENT_UNIT_PAYLOAD_TYPE 49

class H265Packetizer {
public:
    using Frame = FrameBuffer<MAX_VIDEO_FRAME_SIZE>;
    explicit H265Packetizer(uint8_t itl, int32_t ssrc) : interleave_(itl), ssrc_(ssrc) {};
    ~H265Packetizer() = default;
    int32_t packetizeFrame(
        int32_t seq, uint32_t timestamp,
        const Frame& src, size_t &src_offset, const NalUnit& current_nal,
        uint8_t* dst, size_t dst_size
    ) const;

private:
    uint8_t interleave_ = 0;
    int32_t ssrc_ = 0;
};