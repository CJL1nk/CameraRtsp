package com.pntt3011.cameraserver.server.packetizer

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import java.nio.ByteBuffer

abstract class RTPPacketizer(
    val port: Int,
) {
    class Buffer(
        var data: ByteArray,
        var length: Int,
        var timestamp: Long,
    ) {
        fun injectSeqNumber(seq: Int) {
            data[2] = ((seq shr 8) and 0xFF).toByte()
            data[3] = (seq and 0xFF).toByte()
        }
    }

    protected val buffer = Buffer(ByteArray(0), 0, 0L)
    private var frameWaitThreadCount = 0
    private val frameLock = Any()

    var sdp = ""
        private set

    fun onPrepared(mediaFormat: MediaFormat) {
        sdp = prepareSdp(mediaFormat)
    }

    protected abstract fun prepareSdp(mediaFormat: MediaFormat): String

    fun onAACFrameReceived(byteBuffer: ByteBuffer, bufferInfo: BufferInfo) {
        synchronized(frameLock) {
            buffer.timestamp = bufferInfo.presentationTimeUs * 1000
            buffer.length = packetizeFrame(byteBuffer, bufferInfo)
            if (frameWaitThreadCount > 0) {
                frameWaitThreadCount = 0
                (frameLock as Object).notifyAll()
            }
        }
    }

    protected abstract fun packetizeFrame(byteBuffer: ByteBuffer, bufferInfo: BufferInfo): Int

    fun getLastBuffer(currentBuffer: Buffer): Buffer {
        synchronized(frameLock) {
            if (currentBuffer.timestamp >= buffer.timestamp) {
                frameWaitThreadCount += 1
            }
            while (currentBuffer.timestamp >= buffer.timestamp) {
                (frameLock as Object).wait()
            }
            if (buffer.length > currentBuffer.length) {
                currentBuffer.data = ByteArray(buffer.length)
            }
            System.arraycopy(buffer.data, 0, currentBuffer.data, 0, buffer.length)
            currentBuffer.length = buffer.length
            currentBuffer.timestamp = buffer.timestamp
        }
        return currentBuffer
    }
}