package com.pntt3011.cameraserver.server.packetizer

import android.media.MediaCodec
import android.util.Base64
import java.nio.ByteBuffer

class H265Packetizer : RTPPacketizer() {
    companion object {
        private const val PAYLOAD_TYPE = 97
        private const val CLOCK_RATE = 90000
        private const val SPROP_VPS = "sprop-vps"
        private const val SPROP_SPS = "sprop-sps"
        private const val SPROP_PPS = "sprop-pps"
        private const val NAL_HEADER_SIZE = 2
        private const val FU_PAYLOAD_TYPE = 49
    }

    override fun onPrepared(mediaBuffer: ByteBuffer) {
        val props = extractParamSets(mediaBuffer)
        sampleRate = CLOCK_RATE
        sdp = "m=video 0 RTP/AVP ${PAYLOAD_TYPE}\r\n" +
                "a=rtpmap:$PAYLOAD_TYPE H265/$CLOCK_RATE\r\n" +
                "a=fmtp:$PAYLOAD_TYPE sprop-vps=${props[SPROP_VPS]};sprop-sps=${props[SPROP_SPS]};sprop-pps=${props[SPROP_PPS]}\r\n"
    }

    override fun packetizeFrame(
        framePacket: FramePacket,
        byteBuffer: ByteBuffer,
        bufferInfo: MediaCodec.BufferInfo
    ): Int {
        var numPacket = 0
        val frame = byteBuffer.toByteArray()
        val nalUnits = extractNalUnits(frame)
        for (nal in nalUnits) {
            val packets = nal.segmentPayloads()
            packets.forEachIndexed { index, packet ->
                if (framePacket.rtpPacket.size <= numPacket) {
                    framePacket.rtpPacket.add(RtpPacket())
                }
                val rtpPacket = framePacket.rtpPacket[numPacket]
                val rtpSize = packet.size + HEADER_SIZE
                if (rtpPacket.data.size < rtpSize) {
                    rtpPacket.data = ByteArray(rtpSize)
                }
                rtpPacket.data[0] = 0x80.toByte() // Version 2
                if (index == packets.lastIndex) {
                    rtpPacket.data[1] = (0x80 or PAYLOAD_TYPE).toByte()
                } else {
                    rtpPacket.data[1] = (0x00 or PAYLOAD_TYPE).toByte()
                }
                // Injected by consumers later
                rtpPacket.data[2] = 0
                rtpPacket.data[3] = 0
                rtpPacket.data[4] = 0
                rtpPacket.data[5] = 0
                rtpPacket.data[6] = 0
                rtpPacket.data[7] = 0
                rtpPacket.data[8] = 0
                rtpPacket.data[9] = 0
                rtpPacket.data[10] = 0
                rtpPacket.data[11] = 0

                System.arraycopy(packet, 0, rtpPacket.data, HEADER_SIZE, packet.size)
                rtpPacket.size = rtpSize
                numPacket++
            }
        }
        return numPacket
    }


    private fun extractParamSets(encodedBuffer: ByteBuffer): Map<String, String> {
        val frame = encodedBuffer.toByteArray()
        val nalUnits = extractNalUnits(frame)
        val nalMap = mutableMapOf<String, String>()
        for (nal in nalUnits) {
            when (nal.type) {
                32 -> nalMap[SPROP_VPS] = nal.base64Data
                33 -> nalMap[SPROP_SPS] = nal.base64Data
                34 -> nalMap[SPROP_PPS] = nal.base64Data
            }
        }
        return nalMap
    }

    private fun extractNalUnits(data: ByteArray): List<NALUnit> {
        val nals = mutableListOf<NALUnit>()
        var offset = 0
        while (true) {
            val start = data.indexOfNalStart(offset) ?: break
            val nalHdrLen = if (data[start + 2] == 0x01.toByte()) 3 else 4
            val end = data.indexOfNalStart(start + nalHdrLen) ?: data.size
            NALUnit.from(data, start + nalHdrLen, end)?.let { nals.add(it) }
            offset = end
        }
        return nals
    }

    private fun ByteArray.indexOfNalStart(start: Int): Int? {
        for (i in start until size - 3) {
            if (this[i] == 0x00.toByte() &&
                this[i + 1] == 0x00.toByte() &&
                this[i + 2] == 0x01.toByte()) return i

            if (this[i] == 0x00.toByte() &&
                this[i + 1] == 0x00.toByte() &&
                this[i + 2] == 0x00.toByte() &&
                this[i + 3] == 0x01.toByte()) return i
        }
        return null
    }

    private class NALUnit(val data: ByteArray) {
        val base64Data: String get() = Base64.encodeToString(data, Base64.NO_WRAP)
        val type = (data[0].toInt() shr 1) and 0x3F
        private val header get() = data.copyOfRange(0, NAL_HEADER_SIZE)

        fun segmentPayloads(): List<ByteArray> {
            if (data.size <= MAX_PAYLOAD_SIZE) {
                // Duplicate the nal.header (nal.data also contains nal.header)
                val payloadHeader = header
                return listOf(payloadHeader + data)
            }

            val packets = mutableListOf<ByteArray>()
            val payloadHeader = createFuPayloadHeader()
            val fuHeaderBase = type
            val nalSize = data.size
            var offset = NAL_HEADER_SIZE // This is not included in FU payload
            while (offset < nalSize) {
                val end = offset + minOf(MAX_PAYLOAD_SIZE, nalSize - offset)

                val isStart = (offset == NAL_HEADER_SIZE)
                val isEnd = (end == nalSize)
                val fuHeader = when {
                    isStart -> (fuHeaderBase or 0x80).toByte() // Bit 7
                    isEnd -> (fuHeaderBase or 0x40).toByte() // Bit 6
                    else -> fuHeaderBase.toByte()
                }

                val payload = payloadHeader + byteArrayOf(fuHeader) + data.copyOfRange(offset, end)
                packets.add(payload)

                offset = end
            }
            return packets
        }

        // Replace bit 1-6 of first header byte
        private fun createFuPayloadHeader(): ByteArray {
            return header.also {
                it[0] = ((it[0].toInt() and 0x81)                        // Clear bit 1-6
                        or ((FU_PAYLOAD_TYPE shl 1) and 0x7E)).toByte()  // Set bit 1-6
            }
        }

        companion object {
            fun from(data: ByteArray, start: Int, end: Int): NALUnit? {
                if (end - start < NAL_HEADER_SIZE) return null
                return NALUnit(data.copyOfRange(start, end))
            }
        }
    }

    private fun ByteBuffer.toByteArray(): ByteArray {
        val bytes = ByteArray(remaining())
        get(bytes)
        return bytes
    }
}