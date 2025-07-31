#pragma once

#include <jni.h>
#include <array>
#include "source/video_source.h"
#include "utils/frame_buffer.h"
#include "utils/h265_nal_unit.h"
#include "utils/constant.h"

class H265Packetizer {
public:
    using Frame = FrameBuffer<MAX_VIDEO_FRAME_SIZE>;
    explicit H265Packetizer(uint8_t itl, int32_t ssrc) : interleave_(itl), ssrc_(ssrc) {};
    ~H265Packetizer() = default;
    int32_t packetizeFrame(
        uint16_t seq, uint32_t timestamp,
        const Frame& src, size_t &src_offset, const NalUnit& current_nal,
        uint8_t* dst, size_t dst_size
    ) const;

private:
    static constexpr size_t PAYLOAD_HEADER_SIZE = 2;
    static constexpr size_t FU_HEADER_SIZE = 1;
    static constexpr size_t FRAGMENT_UNIT_PAYLOAD_TYPE = 49;

    uint8_t interleave_ = 0;
    int32_t ssrc_ = 0;
};