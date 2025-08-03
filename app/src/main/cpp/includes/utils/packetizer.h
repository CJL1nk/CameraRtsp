#pragma once

#include <jni.h>
#include "frame_buffer.h"
#include "h265_nal_unit.h"
#include "constant.h"

#define FU_TYPE(data, nal) ((data[nal.start + nal.codeSize] & 0x81) | ((H265_FU_PAYLOAD_TYPE << 1) & 0x7E))


static int32_t packetizeH265Frame(uint8_t interleave, int32_t ssrc,
                                  uint16_t seq, uint32_t timestamp,
                                  const uint8_t *data, size_t data_size,
                                  size_t &src_offset, const NalUnit& current_nal,
                                  uint8_t *dst, size_t dst_size) {
    size_t header_size = RTP_HEADER_SIZE + H265_PAYLOAD_HEADER_SIZE;

    if (src_offset < current_nal.start ||
        src_offset >= current_nal.end ||
        current_nal.end > data_size ||
        TCP_PREFIX_SIZE + header_size >= dst_size
            ) {
        return -1;
    }

    bool is_segment_start = src_offset == current_nal.start;
    bool is_single_mode = is_segment_start &&
                          (TCP_PREFIX_SIZE + header_size + (current_nal.end - current_nal.start - current_nal.codeSize) <= dst_size);
    bool is_segment_end = TCP_PREFIX_SIZE + header_size + H265_FU_HEADER_SIZE + (current_nal.end - src_offset) <= dst_size;
    size_t packet_size =
            is_single_mode ? header_size + (current_nal.end - current_nal.start - current_nal.codeSize) :
            is_segment_end ? header_size + H265_FU_HEADER_SIZE + (current_nal.end - src_offset) :
            dst_size - TCP_PREFIX_SIZE;

    size_t i = 0;
    dst[i++] = '$';
    dst[i++] = interleave;
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

    dst[i++] = (ssrc >> 24) & 0xFF;
    dst[i++] = (ssrc >> 16) & 0xFF;
    dst[i++] = (ssrc >> 8) & 0xFF;
    dst[i++] = ssrc & 0xFF;

    if (is_single_mode) {
        // Payload header
        src_offset += current_nal.codeSize;
        dst[i++] = data[src_offset];
        dst[i++] = data[src_offset + 1];

        // Payload
        std::memcpy(dst + i, data + src_offset, current_nal.end - src_offset);
        src_offset = current_nal.end;
        return static_cast<int32_t>(TCP_PREFIX_SIZE + packet_size);
    }

    // Payload header
    dst[i++] = FU_TYPE(data, current_nal);
    dst[i++] = data[current_nal.start + current_nal.codeSize + 1];

    // Fu header
    uint8_t fu_header = NAL_TYPE(data, current_nal);
    if (is_segment_start) {
        fu_header |= 0x80;
    } else if (is_segment_end) {
        fu_header |= 0x40;
    }
    dst[i++] = fu_header;

    // Payload
    if (is_segment_start) {
        src_offset += current_nal.codeSize + H265_PAYLOAD_HEADER_SIZE; // The FU Header already contains the Payload Header
    }
    std::memcpy(dst + i, data + src_offset, packet_size - header_size - H265_FU_HEADER_SIZE);
    src_offset = src_offset + packet_size - header_size - H265_FU_HEADER_SIZE;
    return static_cast<int32_t>(TCP_PREFIX_SIZE + packet_size);
}

static int32_t packetizeAACFrame(uint8_t interleave, int32_t ssrc,
                                 uint16_t seq, uint32_t timestamp,
                                 const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &src,
                                 uint8_t *dst, size_t dst_size) {

    size_t packet_size = RTP_HEADER_SIZE + AAC_AU_HEADER_SIZE + AAC_AU_SIZE + src.size;
    if (TCP_PREFIX_SIZE + packet_size > dst_size) {
        return -1;
    }

    // TCP prefix
    size_t i = 0;
    dst[i++] = '$';
    dst[i++] = interleave;
    dst[i++] = (packet_size >> 8) & 0xFF;
    dst[i++] = (packet_size & 0xFF);

    // RTP Header
    dst[i++] = RTP_VERSION;
    dst[i++] = 0x80 | AAC_PAYLOAD_TYPE;
    dst[i++] = (seq >> 8) & 0xFF;
    dst[i++] = (seq & 0xFF);

    dst[i++] = (timestamp >> 24) & 0xFF;
    dst[i++] = (timestamp >> 16) & 0xFF;
    dst[i++] = (timestamp >> 8) & 0xFF;
    dst[i++] = timestamp & 0xFF;

    dst[i++] = (ssrc >> 24) & 0xFF;
    dst[i++] = (ssrc >> 16) & 0xFF;
    dst[i++] = (ssrc >> 8) & 0xFF;
    dst[i++] = ssrc & 0xFF;

    // AU header: AU length (2 bytes)
    dst[i++] = 0x00;
    dst[i++] = 0x10;

    // AU: 13 bits for frame size, 3 bits for AU index (= 0)
    dst[i++] = src.size >> 5;
    dst[i++] = (src.size << 3) & 0xF8;

    // Payload
    std::memcpy(dst + i, src.data.data(), src.size);
    return static_cast<int32_t>(TCP_PREFIX_SIZE + packet_size);
}
