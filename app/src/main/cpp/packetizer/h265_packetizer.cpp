
#include "h265_packetizer.h"
#include <random>

#define NAL_TYPE(data, nal) ((data[nal.start + nal.codeSize] >> 1) & 0x3F)
#define FU_TYPE(data, nal) ((data[nal.start + nal.codeSize] & 0x81) | ((FRAGMENT_UNIT_PAYLOAD_TYPE << 1) & 0x7E))


int32_t
H265Packetizer::packetizeFrame(uint16_t seq, uint32_t timestamp,
                               const H265Packetizer::Frame &src, size_t &src_offset, const NalUnit& current_nal,
                               uint8_t *dst, size_t dst_size) const {
    size_t header_size = RTP_HEADER_SIZE + PAYLOAD_HEADER_SIZE;

    if (src_offset < current_nal.start ||
        src_offset >= current_nal.end ||
        current_nal.end > src.size ||
        TCP_PREFIX_SIZE + header_size >= dst_size
    ) {
        return -1;
    }

    bool is_segment_start = src_offset == current_nal.start;
    bool is_single_mode = is_segment_start &&
            (TCP_PREFIX_SIZE + header_size + (current_nal.end - current_nal.start - current_nal.codeSize) <= dst_size);
    bool is_segment_end = TCP_PREFIX_SIZE + header_size + FU_HEADER_SIZE + (current_nal.end - src_offset) <= dst_size;
    size_t packet_size =
            is_single_mode ? header_size + (current_nal.end - current_nal.start - current_nal.codeSize) :
            is_segment_end ? header_size + FU_HEADER_SIZE + (current_nal.end - src_offset) :
            dst_size - TCP_PREFIX_SIZE;

    size_t i = 0;
    dst[i++] = '$';
    dst[i++] = interleave_;
    dst[i++] = (packet_size >> 8) & 0xFF;
    dst[i++] = (packet_size & 0xFF);

    // RTP Header
    dst[i++] = RTP_VERSION;
    dst[i++] = ((is_single_mode || is_segment_end) ? 0x80 : 0x00) | H265_PAYLOAD_TYPE;
    dst[i++] = (seq >> 8) & 0xFF;
    dst[i++] = (seq & 0xFF);

    dst[i++] = (timestamp >> 24) & 0xFF;
    dst[i++] = (timestamp >> 16) & 0xFF;
    dst[i++] = (timestamp >> 8) & 0xFF;
    dst[i++] = timestamp & 0xFF;

    dst[i++] = (ssrc_ >> 24) & 0xFF;
    dst[i++] = (ssrc_ >> 16) & 0xFF;
    dst[i++] = (ssrc_ >> 8) & 0xFF;
    dst[i++] = ssrc_ & 0xFF;

    if (is_single_mode) {
        // Payload header
        src_offset += current_nal.codeSize;
        dst[i++] = src.data[src_offset];
        dst[i++] = src.data[src_offset + 1];

        // Payload
        std::memcpy(dst + i, src.data.data() + src_offset, current_nal.end - src_offset);
        src_offset = current_nal.end;
        return static_cast<int32_t>(TCP_PREFIX_SIZE + packet_size);
    }

    // Payload header
    dst[i++] = FU_TYPE(src.data, current_nal);
    dst[i++] = src.data[current_nal.start + 1];

    // Fu header
    uint8_t fu_header = NAL_TYPE(src.data, current_nal);
    if (is_segment_start) {
        fu_header |= 0x80;
    } else if (is_segment_end) {
        fu_header |= 0x40;
    }
    dst[i++] = fu_header;

    // Payload
    if (is_segment_start) {
        src_offset += current_nal.codeSize;
    }
    std::memcpy(dst + i, src.data.data() + src_offset, packet_size - header_size - FU_HEADER_SIZE);
    src_offset = src_offset + packet_size - header_size - FU_HEADER_SIZE;
    return static_cast<int32_t>(TCP_PREFIX_SIZE + packet_size);
}
