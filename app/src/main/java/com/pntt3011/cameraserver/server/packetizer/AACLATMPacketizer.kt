package com.pntt3011.cameraserver.server.packetizer

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import java.nio.ByteBuffer

class AACLATMPacketizer : RTPPacketizer() {
    companion object {
        private const val AU_HEADER_SIZE = 4
        private const val PAYLOAD_TYPE = 96
        private val SAMPLING_RATE_INDEX = intArrayOf(
            96000,  // 0
            88200,  // 1
            64000,  // 2
            48000,  // 3
            44100,  // 4
            32000,  // 5
            24000,  // 6
            22050,  // 7
            16000,  // 8
            12000,  // 9
            11025,  // 10
            8000,  // 11
            7350,  // 12
            -1,  // 13
            -1,  // 14
            -1,  // 15
        )
    }


    override fun onPrepared(mediaFormat: MediaFormat) {
        val profile = 2 // AAC LC
        val channelCount = mediaFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        sampleRate = mediaFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        val samplingRateIndex = SAMPLING_RATE_INDEX.indexOf(sampleRate)
        val config = (profile and 0x1F) shl 11 or
                ((samplingRateIndex and 0x0F) shl 7) or
                ((channelCount and 0x0F) shl 3)
        sdp = "m=audio 0 RTP/AVP $PAYLOAD_TYPE\r\n" +
                "a=rtpmap:$PAYLOAD_TYPE MPEG4-GENERIC/$sampleRate/$channelCount\r\n" +
                "a=fmtp:$PAYLOAD_TYPE streamtype=5; profile-level-id=15; mode=AAC-hbr; config="+Integer.toHexString(config)+"; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
    }

    override fun packetizeFrame(
        framePacket: FramePacket,
        byteBuffer: ByteBuffer,
        bufferInfo: BufferInfo
    ): Int {
        if (framePacket.rtpPacket.size == 0) {
            framePacket.rtpPacket.add(RtpPacket())
        }
        val buffer = framePacket.rtpPacket[0]
        buffer.size = HEADER_SIZE + AU_HEADER_SIZE + bufferInfo.size
        if (buffer.data.size < buffer.size) {
            buffer.data = ByteArray(buffer.size)
        }

        buffer.data[0] = 0x80.toByte() // Version 2
        buffer.data[1] = (0x80 or PAYLOAD_TYPE).toByte()

        // Injected by consumers later
        buffer.data[2] = 0
        buffer.data[3] = 0
        buffer.data[4] = 0
        buffer.data[5] = 0
        buffer.data[6] = 0
        buffer.data[7] = 0
        buffer.data[8] = 0
        buffer.data[9] = 0
        buffer.data[10] = 0
        buffer.data[11] = 0

        // AU header length = 16 bits = 2 bytes (byte 14 + 15)
        buffer.data[12] = 0
        buffer.data[13] = 0x10

        // 13 bits for frame size, 3 bits for AU index (= 0)
        buffer.data[14] = (bufferInfo.size shr 5).toByte()
        buffer.data[15] = ((bufferInfo.size shl 3) and (0xF8)).toByte()

        val dup = byteBuffer.duplicate()
        dup.get(buffer.data, HEADER_SIZE + AU_HEADER_SIZE, bufferInfo.size)

        return 1
    }
}