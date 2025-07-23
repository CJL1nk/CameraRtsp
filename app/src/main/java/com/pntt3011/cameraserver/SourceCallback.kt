package com.pntt3011.cameraserver

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import android.os.Handler
import java.nio.ByteBuffer

abstract class SourceCallback(val handler: Handler) {
    fun prepare(format: MediaFormat) {
        handler.post { onPrepared(format) }
    }
    fun frameAvailable(buffer: ByteBuffer, bufferInfo: BufferInfo) {
        handler.removeCallbacks(frameCallbackRunnable)
        synchronized(cloneLock) {
            clone(buffer, bufferInfo, pendingBuffer, pendingBufferInfo)
        }
        handler.post { frameCallbackRunnable }
    }
    fun close() {
        handler.post { onClosed() }
    }
    private var pendingBuffer: ByteBuffer = ByteBuffer.allocateDirect(0)
    private var pendingBufferInfo: BufferInfo = BufferInfo()
    private val cloneLock = Any()
    private var cloneBuffer = ByteBuffer.allocateDirect(0)
    private val cloneBufferInfo: BufferInfo = BufferInfo()

    private val frameCallbackRunnable = Runnable {
        synchronized(cloneLock) {
            clone(pendingBuffer, pendingBufferInfo, cloneBuffer, cloneBufferInfo)
        }
        onFrameAvailable(cloneBuffer, cloneBufferInfo)
    }
    protected abstract fun onPrepared(format: MediaFormat)
    protected abstract fun onFrameAvailable(buffer: ByteBuffer, bufferInfo: BufferInfo)
    protected abstract fun onClosed()

    private fun clone(
        sourceBuffer: ByteBuffer,
        sourceBufferInfo: BufferInfo,
        destBuffer: ByteBuffer,
        destBufferInfo: BufferInfo,
    ): Pair<ByteBuffer, BufferInfo> {

        sourceBuffer.position(sourceBufferInfo.offset)
        sourceBuffer.limit(sourceBufferInfo.offset + sourceBufferInfo.size)

        var buffer = destBuffer
        if (buffer.capacity() < sourceBufferInfo.size) {
            buffer = ByteBuffer.allocateDirect(sourceBufferInfo.size)
        } else {
            buffer.clear()
        }
        buffer.put(sourceBuffer)
        buffer.flip()

        destBufferInfo.set(0, sourceBufferInfo.size, sourceBufferInfo.presentationTimeUs, sourceBufferInfo.flags)
        return Pair(buffer, destBufferInfo)
    }
}