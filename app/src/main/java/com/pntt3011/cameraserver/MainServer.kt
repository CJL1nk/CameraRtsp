package com.pntt3011.cameraserver

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Handler
import android.util.Log
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

class MainServer(
    private val httpPort: Int,
    private val rtpPort: Int,
    private val workerHandler: Handler,
    private val onClosed: () -> Unit,
) {
    private val threadNumber = AtomicInteger(1)
    private val connectionThreadPool by lazy {
        Executors.newFixedThreadPool(2) {
            Thread({
                try {
                    it.run()
                } catch (e: Exception) {
                    Log.e("MainServer", "Thread ${Thread.currentThread().name} threw: ${e.message}", e)
                }
            },"ConnectionThread-${threadNumber.getAndIncrement()}")
        }
    }
    private val sessions = mutableListOf<RTPSession>()
    private val audioPacketizer by lazy {
        AACLATMPacketizer(rtpPort)
    }
    private val httpServer by lazy {
        HTTPServer(httpPort, connectionThreadPool, sessionHandler) {
            isHttpStopped = true
            checkStop()
        }
    }

    private val sessionHandler by lazy {
        object : HTTPServer.SessionHandler {
            override val handler: Handler
                get() = workerHandler

            override fun onNewSession(destination: String) {
                val rtpSession = createNewRtpSession(destination)
                connectionThreadPool.submit {
                    rtpSession.start()
                }
            }
        }
    }
    fun start() {
        httpServer.start()
    }

    fun onAudioPrepared(mediaFormat: MediaFormat) {
        audioPacketizer.onPrepared(mediaFormat)
        httpServer.onAudioPrepared(audioPacketizer.sdp)
    }

    fun onAudioFrameReceived(byteBuffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo) {
        audioPacketizer.onAACFrameReceived(byteBuffer, bufferInfo)
    }

    fun stop() {
        httpServer.stop()
        sessions.forEach { it.stop() }
    }

    private fun createNewRtpSession(destination: String): RTPSession {
        val rtpSession = RTPSession(
            destination,
            rtpPort,
            audioPacketizer
        ) {
            workerHandler.post {
                sessions.remove(it)
                checkStop()
            }
        }
        sessions.add(rtpSession)
        return rtpSession
    }

    private var isHttpStopped = false

    private fun checkStop() {
        if (isHttpStopped && sessions.isEmpty()) {
            Log.d("CleanUp", "gracefully clean up main server")
            onClosed.invoke()
        }
    }
}