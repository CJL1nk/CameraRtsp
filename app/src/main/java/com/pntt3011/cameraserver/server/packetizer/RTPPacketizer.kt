package com.pntt3011.cameraserver.server.packetizer

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import com.pntt3011.cameraserver.monitor.TrackPerfMonitor
import java.nio.ByteBuffer
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.random.Random
import kotlin.random.nextUInt

abstract class RTPPacketizer {

    private val framePacket = FramePacket()
    private var frameWaitThreadCount = 0
    private val frameLock = Any()

    var sdp = ""
        protected set
    protected var sampleRate = 0
    private var lastFrameTimeUs = 0L

    open fun onPrepared(mediaFormat: MediaFormat) {}

    open fun onPrepared(mediaBuffer: ByteBuffer) {}

    fun onFrameReceived(byteBuffer: ByteBuffer, bufferInfo: BufferInfo) {
        perfMonitors.forEach { it.onFrameAvailable(bufferInfo.presentationTimeUs) }
        synchronized(frameLock) {
            framePacket.frameTimestampUs = bufferInfo.presentationTimeUs
            framePacket.rtpTimestamp = increaseTimestamp(framePacket.rtpTimestamp, bufferInfo.presentationTimeUs)
            framePacket.numPacket = packetizeFrame(framePacket, byteBuffer, bufferInfo)
            if (frameWaitThreadCount > 0) {
                frameWaitThreadCount = 0
                (frameLock as Object).notifyAll()
            }
        }
    }

    private fun increaseTimestamp(oldTimestamp: UInt, frameTimestampUs: Long): UInt {
        if (lastFrameTimeUs == 0L) {
            lastFrameTimeUs = frameTimestampUs
            return Random.nextUInt()
        }
        val deltaUs = frameTimestampUs - lastFrameTimeUs
        lastFrameTimeUs = frameTimestampUs
        // (sample / frame) = (sample / second) * (second / frame)
        // => delta RTP time = clock rate * delta frame time
        val deltaRtpTime = (deltaUs * sampleRate / 1_000_000).toUInt()
        return oldTimestamp + deltaRtpTime
    }

    protected abstract fun packetizeFrame(framePacket: FramePacket, byteBuffer: ByteBuffer, bufferInfo: BufferInfo): Int

    fun awaitFramePacket(current: FramePacket): FramePacket {
        synchronized(frameLock) {
            if (current.rtpTimestamp >= framePacket.rtpTimestamp) {
                frameWaitThreadCount += 1
            }
            while (current.rtpTimestamp >= framePacket.rtpTimestamp) {
                (frameLock as Object).wait()
            }
            framePacket.clone(current)
        }
        return current
    }

    class FramePacket(
        var rtpTimestamp: UInt = 0u,
        val rtpPacket: MutableList<RtpPacket> = mutableListOf(),
        var numPacket: Int = 0,
        var frameTimestampUs: Long = 0L,
    ) {
        fun inject(seq: Int, ssrc: Int) {
            for (i in 0 until numPacket) {
                rtpPacket[i].inject(rtpTimestamp, seq, ssrc)
            }
        }
        fun clone(dest: FramePacket) {
            dest.rtpTimestamp = rtpTimestamp
            dest.numPacket = numPacket
            dest.frameTimestampUs = frameTimestampUs
            for (i in dest.rtpPacket.size until numPacket) {
                dest.rtpPacket.add(RtpPacket())
            }
            for (i in 0 until numPacket) {
                dest.rtpPacket[i].size = rtpPacket[i].size
                if (dest.rtpPacket[i].data.size < rtpPacket[i].size) {
                    dest.rtpPacket[i].data = ByteArray(rtpPacket[i].size)
                }
                System.arraycopy(rtpPacket[i].data, 0, dest.rtpPacket[i].data, 0, rtpPacket[i].size)
            }
        }
    }

    class RtpPacket(
        var size: Int = 0,
        var data: ByteArray = ByteArray(0)
    ) {
        fun inject(timestamp: UInt, seq: Int, ssrc: Int) {
            data[2] = ((seq shr 8) and 0xFF).toByte()
            data[3] = (seq and 0xFF).toByte()

            data[4] = ((timestamp shr 24) and 255u).toByte()
            data[5] = ((timestamp shr 16) and 255u).toByte()
            data[6] = ((timestamp shr 8) and 255u).toByte()
            data[7] = (timestamp and 255u).toByte()

            data[8] = ((ssrc shr 24) and 0xFF).toByte()
            data[9] = ((ssrc shr 16) and 0xFF).toByte()
            data[10] = ((ssrc shr 8) and 0xFF).toByte()
            data[11] = (ssrc and 0xFF).toByte()
        }
    }

    private val perfMonitors = CopyOnWriteArrayList<TrackPerfMonitor>()

    fun addTrackPerf(trackPerfMonitor: TrackPerfMonitor) {
        perfMonitors.add(trackPerfMonitor)
    }

    fun removeTrackPef(trackPerfMonitor: TrackPerfMonitor) {
        perfMonitors.remove(trackPerfMonitor)
    }

    companion object {
        const val HEADER_SIZE = 12
        const val MAX_PAYLOAD_SIZE = 1200 // Limit by Maximum Transmission Unit
    }
}