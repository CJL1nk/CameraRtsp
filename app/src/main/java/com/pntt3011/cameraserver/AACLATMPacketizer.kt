package com.pntt3011.cameraserver

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import java.nio.ByteBuffer
import kotlin.random.Random

class AACLATMPacketizer(
    private val port: Int,
) {
    companion object {
        private const val HEADER_SIZE = 12
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

    class Buffer(
        var data: ByteArray,
        var seq: Int,
        var length: Int,
    )
    private val producedBuffer = Buffer(ByteArray(0), (Math.random() * 65536).toInt(), 0)
    private val consumedBuffer = Buffer(ByteArray(0), (Math.random() * 65536).toInt(), 0)
    private var isFrameAvailable = false
    private val frameLock = Any()

    private val ssrc = Random(123).nextInt()
    var sdp = ""
        private set

    fun onPrepared(mediaFormat: MediaFormat) {
        val profile = 2 // AAC LC
        val channelCount = mediaFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        val sampleRate = mediaFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        val samplingRateIndex = SAMPLING_RATE_INDEX.indexOf(sampleRate)
        val config = (profile and 0x1F) shl 11 or
                ((samplingRateIndex and 0x0F) shl 7) or
                ((channelCount and 0x0F) shl 3)
        sdp = "m=audio $port RTP/AVP $PAYLOAD_TYPE\r\n" +
                "a=rtpmap:$PAYLOAD_TYPE MPEG4-GENERIC/$sampleRate/$channelCount\r\n" +
                "a=fmtp:$PAYLOAD_TYPE streamtype=5; profile-level-id=15; mode=AAC-hbr; config="+Integer.toHexString(config)+"; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
    }

    fun onAACFrameReceived(byteBuffer: ByteBuffer, bufferInfo: BufferInfo) {
        synchronized(frameLock) {
            // Increase seqNumber first so that data has matched seqNumber
            producedBuffer.seq = (producedBuffer.seq + 1) % 65536

            producedBuffer.length = bufferInfo.size + HEADER_SIZE
            if (producedBuffer.length > producedBuffer.data.size) {
                producedBuffer.data = ByteArray(producedBuffer.length)
            }

            producedBuffer.data[0] = 0x80.toByte() // Version 2
            producedBuffer.data[1] = (0x80 or PAYLOAD_TYPE).toByte()
            producedBuffer.data[2] = (producedBuffer.seq shr 8).toByte()
            producedBuffer.data[3] = (producedBuffer.seq and 0xFF).toByte()

            val rtpTimestamp = bufferInfo.presentationTimeUs * 1000
            producedBuffer.data[4] = ((rtpTimestamp shr 24) and 0xFF).toByte()
            producedBuffer.data[5] = ((rtpTimestamp shr 16) and 0xFF).toByte()
            producedBuffer.data[6] = ((rtpTimestamp shr 8) and 0xFF).toByte()
            producedBuffer.data[7] = (rtpTimestamp and 0xFF).toByte()

            producedBuffer.data[8] = ((ssrc shr 24) and 0xFF).toByte()
            producedBuffer.data[9] = ((ssrc shr 16) and 0xFF).toByte()
            producedBuffer.data[10] = ((ssrc shr 8) and 0xFF).toByte()
            producedBuffer.data[11] = (ssrc and 0xFF).toByte()

            val dup = byteBuffer.duplicate()
            dup.position(bufferInfo.offset)
            dup.limit(bufferInfo.offset + bufferInfo.size)
            dup.get(producedBuffer.data, HEADER_SIZE, bufferInfo.size)

            isFrameAvailable = true
            (frameLock as Object).notifyAll()
        }
    }

    fun getLatestBuffer(seq: Int): Buffer {
        // Call from late consumers
        if (isFrameAvailable && seq != consumedBuffer.seq) {
            return consumedBuffer
        }
        synchronized(frameLock) {
            // Double check late consumers
            if (isFrameAvailable && seq != consumedBuffer.seq) {
                return consumedBuffer
            }
            // Only the earliest consumer will enter this loop
            // Use "while" instead of "if"
            // to avoid spurious wakeup
            while (!isFrameAvailable || seq == producedBuffer.seq) {
                (frameLock as Object).wait()
            }
            if (producedBuffer.length > consumedBuffer.length) {
                consumedBuffer.data = ByteArray(producedBuffer.length)
            }
            System.arraycopy(producedBuffer.data, 0, consumedBuffer.data, 0, producedBuffer.length)
            consumedBuffer.length = producedBuffer.length
            consumedBuffer.seq = producedBuffer.seq
            isFrameAvailable = false
        }
        return consumedBuffer
    }
}