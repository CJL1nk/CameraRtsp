package com.pntt3011.cameraserver

import android.content.ContentValues
import android.content.Context
import android.media.MediaCodec
import android.media.MediaFormat
import android.media.MediaMuxer
import android.os.Handler
import android.os.HandlerThread
import android.provider.MediaStore
import java.io.IOException
import java.nio.ByteBuffer

class Server(context: Context) {

    private val workerThread by lazy {
        HandlerThread("WorkerThread").apply { start() }
    }
    private val workerHandler by lazy {
        Handler(workerThread.looper)
    }
    private val cameraSource by lazy {
        CameraSource(context)
    }
    private val audioSource by lazy {
        AudioSource()
    }

    fun start() {
        audioSource.start()
        cameraSource.start(workerHandler)
    }

    fun stop() {
        audioSource.stop(workerHandler) {
            checkStop(false)
        }
        cameraSource.stop(workerHandler) {
            checkStop(true)
        }
    }

    private var stoppedVideo = false
    private var stoppedAudio = false

    private fun checkStop(isVideo: Boolean) {
        stoppedVideo = stoppedVideo || isVideo
        stoppedAudio = stoppedAudio || !isVideo
        if (stoppedVideo && stoppedAudio) {
            cleanUp()
        }
    }

    private fun cleanUp() {
        workerThread.quitSafely()
        workerThread.join()
    }

    class MediaMuxerWrapper(val context: Context) {
        val resolver = context.contentResolver
        val videoCollection =
            MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)

        val videoDetails = ContentValues().apply {
            put(MediaStore.Video.Media.DISPLAY_NAME, "my_video_${System.currentTimeMillis()}.mp4")
            put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
            put(MediaStore.Video.Media.RELATIVE_PATH, "DCIM/Camera") // or "Movies/MyApp"
            put(MediaStore.Video.Media.IS_PENDING, 1) // mark as "being written"
        }

        val videoUri = resolver.insert(videoCollection, videoDetails)
            ?: throw IOException("Failed to create MediaStore entry")

        val fd = resolver.openFileDescriptor(videoUri, "w")?.fileDescriptor
            ?: throw IOException("Failed to get FileDescriptor")
        private val muxer = MediaMuxer(fd, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

        private var videoTrackIndex = -1
        private var audioTrackIndex = -1
        private var started = false
        private var videoReady = false
        private var audioReady = false

        fun addVideoTrack(format: MediaFormat) {
            videoTrackIndex = muxer.addTrack(format)
            videoReady = true
            tryStartMuxer()
        }

        fun addAudioTrack(format: MediaFormat) {
            audioTrackIndex = muxer.addTrack(format)
            audioReady = true
            tryStartMuxer()
        }

        private fun tryStartMuxer() {
            if (!started && videoReady && audioReady) {
                muxer.start()
                started = true
            }
        }

        fun writeSampleData(
            isVideo: Boolean,
            byteBuffer: ByteBuffer,
            bufferInfo: MediaCodec.BufferInfo
        ) {
            if (!started) return
            val trackIndex = if (isVideo) videoTrackIndex else audioTrackIndex
            muxer.writeSampleData(trackIndex, byteBuffer, bufferInfo)
        }

        fun stopMuxer() {
            if (started) {
                try {
                    muxer.stop()
                } catch (_: Exception) {
                }
                muxer.release()
                started = false

                val values = ContentValues().apply {
                    put(MediaStore.Video.Media.IS_PENDING, 0)
                }
                context.contentResolver.update(videoUri, values, null, null)
            }
        }
    }
}