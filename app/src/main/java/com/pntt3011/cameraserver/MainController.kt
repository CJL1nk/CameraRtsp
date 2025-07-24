package com.pntt3011.cameraserver

import android.content.Context
import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Log
import java.nio.ByteBuffer

class MainController(context: Context) {
    private val mainHandler by lazy {
        Handler(Looper.getMainLooper())
    }
    private val workerThread by lazy {
        HandlerThread("WorkerThread").apply { start() }
    }
    private val workerHandler by lazy {
        Handler(workerThread.looper)
    }
    private val cameraSource by lazy {
        CameraSource(context, object : SourceCallback {
            override val handler: Handler
                get() = workerHandler

            override fun onPrepared(format: MediaFormat) {
            }

            override fun onFrameAvailable(buffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo) {
            }

            override fun onClosed() {
                checkStop(true)
            }
        })
    }
    private val audioSource by lazy {
        AudioSource(object : SourceCallback {
            override val handler: Handler
                get() = workerHandler

            override fun onPrepared(format: MediaFormat) {
            }

            override fun onFrameAvailable(buffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo) {
            }

            override fun onClosed() {
                checkStop(false)
            }
        })
    }

    fun start() {
        audioSource.start()
        cameraSource.start()
    }

    fun stop() {
        audioSource.stop()
        cameraSource.stop()
    }

    private var stoppedVideo = false
    private var stoppedAudio = false

    private fun checkStop(isVideo: Boolean) {
        stoppedVideo = stoppedVideo || isVideo
        stoppedAudio = stoppedAudio || !isVideo
        if (stoppedVideo && stoppedAudio) {
            cleanUp()
        }
        cleanUp()
    }

    private fun cleanUp() {
        mainHandler.post {
            workerThread.quitSafely()
            workerThread.join()
            Log.d("CleanUp", "gracefully clean up server")
        }
    }
}