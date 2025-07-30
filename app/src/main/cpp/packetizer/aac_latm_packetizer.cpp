#include "aac_latm_packetizer.h"
#include <random>


int32_t AacLatmPacketizer::packetizeFrame(int32_t seq, uint32_t timestamp, const AacLatmPacketizer::Frame &src,
                                          uint8_t *dst, size_t dst_size) const {

    size_t packet_size = RTP_HEADER_SIZE + AAC_AU_HEADER_SIZE + AAC_AU_SIZE + src.size;
    if (TCP_PREFIX_SIZE + packet_size > dst_size) {
        return -1;
    }

    // TCP prefix
    size_t i = 0;
    dst[i++] = '$';
    dst[i++] = interleave_;
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

    dst[i++] = (ssrc_ >> 24) & 0xFF;
    dst[i++] = (ssrc_ >> 16) & 0xFF;
    dst[i++] = (ssrc_ >> 8) & 0xFF;
    dst[i++] = ssrc_ & 0xFF;

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




