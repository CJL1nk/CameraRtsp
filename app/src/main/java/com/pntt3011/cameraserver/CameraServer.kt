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

    fun start() {
        cameraSource.start(workerHandler)
    }

    fun stop() {
        cameraSource.stop {
            workerThread.quitSafely()
            workerThread.join()
        }
    }
}