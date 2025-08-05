
#include "utils/Packetizer.h"

#define RTP_HEADER_SIZE 12
#define TCP_PREFIX_SIZE 4
#define RTP_VERSION 0x80

#define AAC_AU_HEADER_SIZE 2
#define AAC_AU_SIZE 2

#define H265_PAYLOAD_HEADER_SIZE 2
#define H265_FU_HEADER_SIZE 1
#define H265_FU_PAYLOAD_TYPE 49

#define NTP_UNIX_OFFSET 2208988800UL
#define RTCP_SR_TYPE 200

#define FU_TYPE(data, nal) ((data[nal.start + nal.codeSize] & 0x81) | ((H265_FU_PAYLOAD_TYPE << 1) & 0x7E))

int_t RtpPayloadStart() {
    return TCP_PREFIX_SIZE + RTP_HEADER_SIZE;
}

// Return the packet size
// Also move source offset to end of read position
int_t PacketizeH265(
        byte_t interleave,
        ushort_t seq,
        sz_t timestamp,
        sz_t ssrc,

        const byte_t *src,
        sz_t src_size,
        sz_t &src_offset,
        const NalUnit& src_nal,

        byte_t *dst,
        sz_t dst_size) {

    sz_t header_size;
    sz_t nal_size;
    sz_t nal_remain;
    sz_t packet_size;
    sz_t i;
    bool_t is_segment_start;
    bool_t is_segment_end;
    bool_t is_single_mode;
    byte_t fu_header;
    
    // Don't include TCP_PREFIX_SIZE in packet size
    header_size = RTP_HEADER_SIZE + H265_PAYLOAD_HEADER_SIZE;

    // Invalid input
    if (src_offset < src_nal.start ||
        src_offset >= src_nal.end ||
        src_nal.end > src_size ||
        TCP_PREFIX_SIZE + header_size >= dst_size) {
        return -1;
    }

    is_segment_start = src_offset == src_nal.start;

    // All data in one packet (Single mode)
    nal_size = src_nal.end - src_nal.start - src_nal.codeSize;
    is_single_mode =
            is_segment_start &&
            TCP_PREFIX_SIZE + header_size + nal_size <= dst_size;

    // H265_FU_HEADER_SIZE only appears in fragmented mode
    // If is_single_mode is true, is_segment_end is always false
    nal_remain = src_nal.end - src_offset;
    is_segment_end =
            TCP_PREFIX_SIZE + header_size + H265_FU_HEADER_SIZE + nal_remain <= dst_size;

    packet_size =
            is_single_mode ? header_size + nal_size :
            is_segment_end ? header_size + H265_FU_HEADER_SIZE + nal_remain :
            dst_size - TCP_PREFIX_SIZE; // Just use all available space

    i = 0;
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
        // Payload header: NAL header (2 bytes after 00 00 .. 01 code)
        src_offset += src_nal.codeSize;
        dst[i++] = src[src_offset];
        dst[i++] = src[src_offset + 1];

        // Payload: All NAL data (except 00 00 .. 01 code)
        // PS: Duplicate NAL header unit
        Copy(dst + i, src + src_offset, src_nal.end - src_offset);
        src_offset = src_nal.end;
        return TCP_PREFIX_SIZE + packet_size;
    }

    // Payload header: NAL header (2 bytes after 00 00 .. 01 code) but with FU type
    dst[i++] = FU_TYPE(src, src_nal);
    dst[i++] = src[src_nal.start + src_nal.codeSize + 1];

    // Fu header
    fu_header = NAL_TYPE(src, src_nal);
    if (is_segment_start) {
        fu_header |= 0x80;
    } else if (is_segment_end) {
        fu_header |= 0x40;
    }
    dst[i++] = fu_header;

    // Payload
    if (is_segment_start) {
        // Skip the NAL header
        // FU Header already contains the NAL Header
        src_offset += src_nal.codeSize + H265_PAYLOAD_HEADER_SIZE;
    }
    Copy(dst + i, src + src_offset, packet_size - header_size - H265_FU_HEADER_SIZE);
    src_offset = src_offset + packet_size - header_size - H265_FU_HEADER_SIZE;
    return TCP_PREFIX_SIZE + packet_size;
}

int_t PacketizeAAC(
        byte_t interleave,
        ushort_t seq,
        sz_t timestamp,
        sz_t ssrc,
        const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &src,
        byte_t *dst,
        sz_t dst_size) {

    size_t packet_size;
    size_t i;

    packet_size = RTP_HEADER_SIZE + AAC_AU_HEADER_SIZE + AAC_AU_SIZE + src.size;
    if (TCP_PREFIX_SIZE + packet_size > dst_size) {
        return -1;
    }

    // TCP prefix
    i = 0;
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
    Copy(dst + i, src.data, src.size);
    return static_cast<int32_t>(TCP_PREFIX_SIZE + packet_size);
}

static void NTP(uint_t *ntp_sec, uint_t *ntp_frac) {
    ts_t ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    *ntp_sec = (uint_t)(ts.tv_sec + NTP_UNIX_OFFSET);
    *ntp_frac = (uint_t)(((double)ts.tv_nsec / 1.0e9) * (double)(1ULL << 32));
}

int_t PacketizeReport(byte_t interleave,
                      byte_t *buf,
                      uint_t ssrc,
                      uint_t rtp_timestamp,
                      uint_t pkt_count,
                      uint_t byte_count) {
    uint_t ntp_sec, ntp_frac;
    NTP(&ntp_sec, &ntp_frac);
    int_t i = 0;

    // TCP prefix
    i = 0;
    buf[i++] = '$';
    buf[i++] = interleave;
    buf[i++] = (28 >> 8) & 0xFF;
    buf[i++] = (28 & 0xFF);

    // RTCP Header: V=2, P=0, RC=0
    buf[i++] = (2 << 6) | 0;
    buf[i++] = RTCP_SR_TYPE;

    // length in 32-bit words minus one:
    // Header + length: 1
    // SSRC: 1
    // NTP: 2
    // RTP timestamp: 1
    // Packet count: 1
    // Octet count: 1
    // Sum: 7 => Minus one = 6
    ushort_t length = 6;
    buf[i++] = (length >> 8) & 0xFF;
    buf[i++] = length & 0xFF;

    // Sender SSRC
    buf[i++] = (ssrc >> 24) & 0xFF;
    buf[i++] = (ssrc >> 16) & 0xFF;
    buf[i++] = (ssrc >> 8) & 0xFF;
    buf[i++] = ssrc & 0xFF;

    // NTP seconds
    buf[i++] = (ntp_sec >> 24) & 0xFF;
    buf[i++] = (ntp_sec >> 16) & 0xFF;
    buf[i++] = (ntp_sec >> 8) & 0xFF;
    buf[i++] = ntp_sec & 0xFF;

    // NTP fractional seconds
    buf[i++] = (ntp_frac >> 24) & 0xFF;
    buf[i++] = (ntp_frac >> 16) & 0xFF;
    buf[i++] = (ntp_frac >> 8) & 0xFF;
    buf[i++] = ntp_frac & 0xFF;

    // RTP timestamp
    buf[i++] = (rtp_timestamp >> 24) & 0xFF;
    buf[i++] = (rtp_timestamp >> 16) & 0xFF;
    buf[i++] = (rtp_timestamp >> 8) & 0xFF;
    buf[i++] = rtp_timestamp & 0xFF;

    // Packet count
    buf[i++] = (pkt_count >> 24) & 0xFF;
    buf[i++] = (pkt_count >> 16) & 0xFF;
    buf[i++] = (pkt_count >> 8) & 0xFF;
    buf[i++] = pkt_count & 0xFF;

    // Octet count
    buf[i++] = (byte_count >> 24) & 0xFF;
    buf[i++] = (byte_count >> 16) & 0xFF;
    buf[i++] = (byte_count >> 8) & 0xFF;
    buf[i++] = byte_count & 0xFF;

    return i;
}