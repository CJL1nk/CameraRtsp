package com.pntt3011.cameraserver.source

import android.media.MediaCodec
import android.util.Log
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.LinkedBlockingDeque
import java.util.concurrent.TimeUnit

class AudioFrameQueue(
    private val callback: SourceCallback
) {
    private val executor = Executors.newSingleThreadExecutor {
        Thread(it, "AudioFrameQueue")
    }
    private val queue by lazy { LinkedBlockingDeque<Pair<ByteBuffer, MediaCodec.BufferInfo>>() }
    private val pool by lazy { ByteBufferPool() }
    private var started = false
    @Volatile
    private var stopped = false
    private var startTimeMs = 0L
    private var firstFrameUs = 0L

    fun onFrameAvailable(buffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo) {
        queue.putLast(cloneBuffer(buffer, bufferInfo))
        if (!started) {
            started = true
            startQueue()
        }
    }

    private fun startQueue() {
        executor.submit {
            try {
                while (!stopped) {
                    try {
                        dequeueOrWait()
                    } catch (e: InterruptedException) {
                        stopped = true
                        Log.d("AudioFrameQueue", "Thread interrupted, exiting.")
                        break
                    }
                }
            } catch (e: Exception) {
                Log.e("AudioFrameQueue", e.message, e)
            }
        }
    }

    private fun dequeueOrWait() {
        val (buffer, info) = queue.takeFirst()

        if (info.presentationTimeUs <= 0) {
            callback.onFrameAvailable(buffer, info)
            return
        }

        if (startTimeMs == 0L || firstFrameUs == 0L) {
            startTimeMs = System.currentTimeMillis()
            firstFrameUs = info.presentationTimeUs
        }

        val frameDelta = (info.presentationTimeUs - firstFrameUs) / 1000
        val elapsed = System.currentTimeMillis() - startTimeMs

        if (elapsed >= frameDelta) {
            callback.onFrameAvailable(buffer, info)
            pool.release(buffer, info)
        } else {
            queue.putFirst(Pair(buffer, info))
            Thread.sleep(frameDelta - elapsed)
        }
    }

    private fun cloneBuffer(
        sourceBuffer: ByteBuffer,
        sourceInfo: MediaCodec.BufferInfo
    ): Pair<ByteBuffer, MediaCodec.BufferInfo> {
        val (buffer, info) = pool.acquire(sourceInfo.size)

        val oldPosition = sourceBuffer.position()
        val oldLimit = sourceBuffer.limit()

        sourceBuffer.position(sourceInfo.offset)
        sourceBuffer.limit(sourceInfo.offset + sourceInfo.size)

        buffer.clear()
        buffer.put(sourceBuffer)
        buffer.flip()

        sourceBuffer.position(oldPosition)
        sourceBuffer.limit(oldLimit)

        info.set(
            0, // offset is always 0 in the pooled buffer
            sourceInfo.size,
            sourceInfo.presentationTimeUs,
            sourceInfo.flags
        )

        return Pair(buffer, info)
    }

    fun stop() {
        stopped = true
        queue.clear()
        executor.awaitTermination(1, TimeUnit.SECONDS)
        Log.d("CleanUp", "gracefully clean up audio frame queue")
    }

    // There are about 15 encoded frames (check log) each time I enqueue input from audio
    // So double that to be safe
    class ByteBufferPool(private val maxPoolSize: Int = 30) {

        private val pool = ArrayDeque<Pair<ByteBuffer, MediaCodec.BufferInfo>>()

        fun acquire(minCapacity: Int): Pair<ByteBuffer, MediaCodec.BufferInfo> {
            synchronized(this) {
                val iterator = pool.iterator()
                while (iterator.hasNext()) {
                    val (buffer, info) = iterator.next()
                    if (buffer.capacity() >= minCapacity) {
                        iterator.remove()
                        buffer.clear()
                        info.set(0, 0, 0, 0)
                        return Pair(buffer, info)
                    }
                }

                // No suitable buffer found, create new
                val newBuffer = ByteBuffer.allocate(minCapacity)
                val newInfo = MediaCodec.BufferInfo()
                return Pair(newBuffer, newInfo)
            }
        }

        /**
         * Return a buffer-info pair to the pool for reuse.
         */
        fun release(buffer: ByteBuffer, info: MediaCodec.BufferInfo) {
            synchronized(this) {
                if (pool.size < maxPoolSize) {
                    pool.add(Pair(buffer, info))
                    return
                }

                // Drop buffer with smallest capacity
                var smallestPair: Pair<ByteBuffer, MediaCodec.BufferInfo>? = null
                var smallestCapacity = Int.MAX_VALUE

                for (pair in pool) {
                    val cap = pair.first.capacity()
                    if (cap < smallestCapacity) {
                        smallestCapacity = cap
                        smallestPair = pair
                    }
                }

                if (buffer.capacity() > smallestCapacity && smallestPair != null) {
                    pool.remove(smallestPair)
                    pool.add(Pair(buffer, info))
                }
            }
        }
    }
}