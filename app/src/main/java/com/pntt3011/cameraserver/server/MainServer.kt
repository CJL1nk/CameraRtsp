package com.pntt3011.cameraserver.server

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Handler
import android.util.Log
import com.pntt3011.cameraserver.server.packetizer.AACLATMPacketizer
import com.pntt3011.cameraserver.server.rtp.RTCPValidation
import com.pntt3011.cameraserver.server.rtp.RTPSession
import com.pntt3011.cameraserver.server.http.HTTPServer
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
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
    private val sessions = mutableMapOf<String, RTPSession>()
    private val audioPacketizer by lazy {
        AACLATMPacketizer(rtpPort)
    }
    private val httpServer by lazy {
        HTTPServer(httpPort, connectionThreadPool, sessionHandler) {
            isHttpStopped = true
            checkStop()
        }
    }
    private val rtcpValidation by lazy {
        RTCPValidation(rtpPort, workerHandler) {
            isRtcpStopped = true
            checkStop()
        }
    }

    private val sessionHandler by lazy {
        object : HTTPServer.SessionHandler {
            override val handler: Handler
                get() = workerHandler

            override fun onNewSession(destination: String) {
                startNewRtpSessionIfNeeded(destination)
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
        rtcpValidation.stop()
        sessions.values.forEach { it.stop() }
    }

    private fun startNewRtpSessionIfNeeded(destination: String) {
        if (sessions.containsKey(destination)) {
            return
        }

        sessions[destination] = RTPSession(
            destination,
            rtpPort,
            audioPacketizer,
            rtcpValidation,
        ) {
            workerHandler.post {
                sessions.remove(destination)
                checkStop()
            }
        }.also { rtpSession ->
            connectionThreadPool.submit {
                rtpSession.start()
            }
        }
    }

    private var isHttpStopped = false
    private var isRtcpStopped = false

    private fun checkStop() {
        if (isHttpStopped && isRtcpStopped && sessions.isEmpty()) {
            connectionThreadPool.awaitTermination(1, TimeUnit.SECONDS)
            Log.d("CleanUp", "gracefully clean up main server")
            onClosed.invoke()
        }
    }
}