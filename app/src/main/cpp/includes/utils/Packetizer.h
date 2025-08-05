#pragma once

#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/Utils.h"

int_t RtpPayloadStart();

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
        sz_t dst_size);

int_t PacketizeAAC(
        byte_t interleave,
        ushort_t seq,
        sz_t timestamp,
        sz_t ssrc,
        const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &src,
        byte_t *dst,
        sz_t dst_size);

int_t PacketizeReport(byte_t interleave,
                      byte_t *buf,
                      uint_t ssrc,
                      uint_t rtp_timestamp,
                      uint_t pkt_count,
                      uint_t byte_count);