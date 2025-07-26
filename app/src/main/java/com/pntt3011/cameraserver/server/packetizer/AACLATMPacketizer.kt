package com.pntt3011.cameraserver.server.packetizer

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import java.nio.ByteBuffer
import kotlin.random.Random

class AACLATMPacketizer(port: Int): RTPPacketizer(port) {
    companion object {
        private const val HEADER_SIZE = 12
        private const val AU_HEADER_SIZE = 4
        private const val PAYLOAD_TYPE = 96
        private const val SAMPLES_PER_FRAME = 1024 // Standard
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

    private var sampleRate = 0
    private var channelCount = 0

    override fun prepare(mediaFormat: MediaFormat): String {
        val profile = 2 // AAC LC
        channelCount = mediaFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        sampleRate = mediaFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        val samplingRateIndex = SAMPLING_RATE_INDEX.indexOf(sampleRate)
        val config = (profile and 0x1F) shl 11 or
                ((samplingRateIndex and 0x0F) shl 7) or
                ((channelCount and 0x0F) shl 3)
        return "m=audio $port RTP/AVP $PAYLOAD_TYPE\r\n" +
                "a=rtpmap:$PAYLOAD_TYPE MPEG4-GENERIC/$sampleRate/$channelCount\r\n" +
                "a=fmtp:$PAYLOAD_TYPE streamtype=5; profile-level-id=15; mode=AAC-hbr; config="+Integer.toHexString(config)+"; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
    }

    override fun increaseTimestamp(oldTimestamp: Long): Long {
        return oldTimestamp + SAMPLES_PER_FRAME
    }

    private val ssrc = Random(123).nextInt()

    override fun packetizeFrame(byteBuffer: ByteBuffer, bufferInfo: BufferInfo, timestamp: Long): Int {
        val length = bufferInfo.size + HEADER_SIZE + AU_HEADER_SIZE
        if (length > buffer.data.size) {
            buffer.data = ByteArray(length)
        }

        buffer.data[0] = 0x80.toByte() // Version 2
        buffer.data[1] = (0x80 or PAYLOAD_TYPE).toByte()
        buffer.data[2] = 0 // Injected by each consumer
        buffer.data[3] = 0 // Injected by each consumer

        buffer.data[4] = ((timestamp shr 24) and 0xFF).toByte()
        buffer.data[5] = ((timestamp shr 16) and 0xFF).toByte()
        buffer.data[6] = ((timestamp shr 8) and 0xFF).toByte()
        buffer.data[7] = (timestamp and 0xFF).toByte()

        buffer.data[8] = ((ssrc shr 24) and 0xFF).toByte()
        buffer.data[9] = ((ssrc shr 16) and 0xFF).toByte()
        buffer.data[10] = ((ssrc shr 8) and 0xFF).toByte()
        buffer.data[11] = (ssrc and 0xFF).toByte()

        // AU header length = 16 bits = 2 bytes (byte 14 + 15)
        buffer.data[12] = 0
        buffer.data[13] = 0x10

        // 13 bits for frame size, 3 bits for AU index (= 0)
        buffer.data[14] = (bufferInfo.size shr 5).toByte()
        buffer.data[15] = ((bufferInfo.size shl 3) and (0xF8)).toByte()

        val dup = byteBuffer.duplicate()
        dup.position(bufferInfo.offset)
        dup.limit(bufferInfo.offset + bufferInfo.size)
        dup.get(buffer.data, HEADER_SIZE + AU_HEADER_SIZE, bufferInfo.size)
        return length
    }
}