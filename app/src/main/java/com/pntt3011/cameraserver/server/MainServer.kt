package com.pntt3011.cameraserver.server

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Handler
import android.util.Log
import com.pntt3011.cameraserver.server.packetizer.AACLATMPacketizer
import com.pntt3011.cameraserver.server.rtp.RTPSession
import com.pntt3011.cameraserver.server.rtsp.RTSPServer
import java.net.InetAddress
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class MainServer(
    private val rtspPort: Int,
    private val rtpPort: Int,
    private val workerHandler: Handler,
    private val onClosed: () -> Unit,
) {
    private val threadNumber = AtomicInteger(1)
    private val connectionThreadPool by lazy {
        Executors.newFixedThreadPool(4) {
            Thread({
                try {
                    it.run()
                } catch (e: Exception) {
                    Log.e("MainServer", "Thread ${Thread.currentThread().name} threw: ${e.message}", e)
                }
            },"ConnectionThread-${threadNumber.getAndIncrement()}")
        }
    }
    private val rtpSessions = mutableMapOf<String, RTPSession>()
    private val audioPacketizer by lazy {
        AACLATMPacketizer(rtpPort)
    }
    private val rtspServer by lazy {
        RTSPServer(rtspPort, connectionThreadPool, sessionHandler) {
            isHttpStopped = true
            checkStop()
        }
    }

    private val sessionHandler by lazy {
        object : RTSPServer.SessionHandler {
            override val handler: Handler
                get() = workerHandler

            override fun onNewSession(address: InetAddress, rtpPort: Int) {
                startNewRtpSessionIfNeeded(address, rtpPort)
            }

            override fun onDestroySession(address: InetAddress, rtpPort: Int) {
                val key = getRtpKey(address, rtpPort)
                rtpSessions[key]?.stop()
            }
        }
    }
    fun start() {
        rtspServer.start()
    }

    fun onAudioPrepared(mediaFormat: MediaFormat) {
        audioPacketizer.onPrepared(mediaFormat)
        rtspServer.onAudioPrepared(audioPacketizer.sdp)
    }

    fun onAudioFrameReceived(byteBuffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo) {
        audioPacketizer.onAACFrameReceived(byteBuffer, bufferInfo)
    }

    fun stop() {
        rtspServer.stop()
        rtpSessions.values.forEach { it.stop() }
    }

    private fun startNewRtpSessionIfNeeded(address: InetAddress, rtpPort: Int) {
        val key = getRtpKey(address, rtpPort)
        if (rtpSessions.containsKey(key)) {
            return
        }

        rtpSessions[key] = RTPSession(
            address,
            rtpPort,
            audioPacketizer,
            connectionThreadPool,
        ) {
            workerHandler.post {
                rtpSessions.remove(key)
                checkStop()
            }
        }.also { rtpSession ->
            rtpSession.start()
        }
    }

    private var isHttpStopped = false

    private fun checkStop() {
        if (isHttpStopped && rtpSessions.isEmpty()) {
            connectionThreadPool.awaitTermination(1, TimeUnit.SECONDS)
            Log.d("CleanUp", "gracefully clean up main server")
            onClosed.invoke()
        }
    }

    private fun getRtpKey(address: InetAddress, rtpPort: Int): String {
        return "${address.hostAddress}:$rtpPort"
    }
}