package com.pntt3011.cameraserver

import android.content.Context

class CameraServer(context: Context) {
    private val cameraSource by lazy {
        CameraSource(context)
    }

    fun start() {
        cameraSource.start()
    }

    fun stop() {
        cameraSource.stop()
    }
}