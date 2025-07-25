package com.pntt3011.cameraserver

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import com.pntt3011.cameraserver.RTPSession.Companion.UNINITIALIZED_SEQ_VALUE
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


    private val syncDataLock = Any()
    private var data = ByteArray(0)
    private var dataSize = 0
    private var seqNumber = (Math.random() * 65536).toInt()

    @Volatile
    private var hasData = false

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
        synchronized(syncDataLock) {
            // Increase seqNumber first so that data has matched seqNumber
            seqNumber = (seqNumber + 1) % 65536

            dataSize = bufferInfo.size + HEADER_SIZE
            if (dataSize > data.size) {
                data = ByteArray(dataSize)
            }

            data[0] = 0x80.toByte() // Version 2
            data[1] = (0x80 or PAYLOAD_TYPE).toByte()
            data[2] = (seqNumber shr 8).toByte()
            data[3] = (seqNumber and 0xFF).toByte()

            val rtpTimestamp = bufferInfo.presentationTimeUs * 1000
            data[4] = ((rtpTimestamp shr 24) and 0xFF).toByte()
            data[5] = ((rtpTimestamp shr 16) and 0xFF).toByte()
            data[6] = ((rtpTimestamp shr 8) and 0xFF).toByte()
            data[7] = (rtpTimestamp and 0xFF).toByte()

            data[8] = ((ssrc shr 24) and 0xFF).toByte()
            data[9] = ((ssrc shr 16) and 0xFF).toByte()
            data[10] = ((ssrc shr 8) and 0xFF).toByte()
            data[11] = (ssrc and 0xFF).toByte()

            val dup = byteBuffer.duplicate()
            dup.position(bufferInfo.offset)
            dup.limit(bufferInfo.offset + bufferInfo.size)
            dup.get(data, HEADER_SIZE, bufferInfo.size)
        }

        hasData = true
    }

    class Buffer(
        var data: ByteArray,
        var seq: Int,
        var length: Int,
    ) {
        fun isInitialized() = seq != UNINITIALIZED_SEQ_VALUE
    }
    private val buffer = Buffer(ByteArray(0), UNINITIALIZED_SEQ_VALUE, 0)

    fun getLatestBuffer(seq: Int): Buffer? {
        if (!hasData) {
            return null
        }

        // The buffer is not outdated
        if (buffer.isInitialized() && seq != buffer.seq) {
            return buffer
        }

        // Read from backed data if new data has arrived
        synchronized(syncDataLock) {
            if (seqNumber != buffer.seq) {
                if (dataSize > buffer.length) {
                    buffer.data = ByteArray(dataSize)
                }
                System.arraycopy(data, 0, buffer.data, 0, dataSize)
                buffer.length = dataSize
                buffer.seq = seqNumber
            }
        }
        return buffer
    }
}