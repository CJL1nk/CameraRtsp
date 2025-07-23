package com.pntt3011.cameraserver

import android.content.Context
import android.os.Handler
import android.os.HandlerThread

class CameraServer(context: Context) {

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
}