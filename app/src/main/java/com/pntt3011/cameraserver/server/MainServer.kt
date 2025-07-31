package com.pntt3011.cameraserver.server

import android.os.Handler

class MainServer(
    private val handler: Handler,
    private val onClosed: () -> Unit,
) {
    fun start(startVideo: Boolean, startAudio: Boolean) {
        handler.post {
            startNative(startVideo, startAudio)
        }
    }

    fun stop() {
        handler.post {
            stopNative()
            onClosed()
        }
    }

    private external fun startNative(startVideo: Boolean, startAudio: Boolean)

    private external fun stopNative()
}