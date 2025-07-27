package com.pntt3011.cameraserver.server

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Handler
import android.util.Log
import com.pntt3011.cameraserver.server.packetizer.AACLATMPacketizer
import com.pntt3011.cameraserver.server.packetizer.H265Packetizer
import com.pntt3011.cameraserver.server.rtp.RTPSession
import com.pntt3011.cameraserver.server.rtsp.RTSPServer
import com.pntt3011.cameraserver.server.rtsp.RTSPServer.Companion.AUDIO_TRACK_ID
import com.pntt3011.cameraserver.server.rtsp.RTSPServer.Companion.VIDEO_TRACK_ID
import java.net.InetAddress
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class MainServer(
    private val rtspPort: Int,
    private val rtpPort: IntArray,
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
    private val packetizers by lazy {
        Array(2) {
            if (it == VIDEO_TRACK_ID) {
                H265Packetizer()
            } else {
                AACLATMPacketizer()
            }
        }
    }
    private val rtspServer by lazy {
        RTSPServer(
            rtspPort,
            rtpPort,
            connectionThreadPool,
            sessionHandler
        ) {
            isHttpStopped = true
            checkStop()
        }
    }

    private val sessionHandler by lazy {
        object : RTSPServer.SessionHandler {
            override val handler: Handler
                get() = workerHandler

            override fun onNewSession(address: InetAddress, rtpPort: IntArray) {
                startNewRtpSessionIfNeeded(address, rtpPort)
            }

            override fun onDestroySession(address: InetAddress, rtpPort: IntArray) {
                val key = getRtpKey(address, rtpPort)
                rtpSessions[key]?.stop()
            }
        }
    }
    fun start() {
        rtspServer.start()
    }

    fun onMediaPrepared(mediaFormat: MediaFormat, trackId: Int) {
        packetizers.getOrNull(trackId)?.let {
            it.onPrepared(mediaFormat)
            rtspServer.onMediaPrepared(it.sdp, trackId)
        }
    }

    fun onMediaPrepared(mediaBuffer: ByteBuffer, trackId: Int) {
        packetizers.getOrNull(trackId)?.let {
            it.onPrepared(mediaBuffer)
            rtspServer.onMediaPrepared(it.sdp, trackId)
        }
    }

    fun onFrameReceived(byteBuffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo, trackId: Int) {
        packetizers.getOrNull(trackId)?.onFrameReceived(byteBuffer, bufferInfo)
    }

    fun stop() {
        rtspServer.stop()
        rtpSessions.values.forEach { it.stop() }
    }

    private fun startNewRtpSessionIfNeeded(clientAddress: InetAddress, clientRtpPort: IntArray) {
        val key = getRtpKey(clientAddress, clientRtpPort)
        if (rtpSessions.containsKey(key)) {
            return
        }

        rtpSessions[key] = RTPSession(
            rtpPort,
            clientAddress,
            clientRtpPort,
            packetizers,
            connectionThreadPool,
            workerHandler,
        ) {
            rtpSessions.remove(key)
            checkStop()
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

    private fun getRtpKey(address: InetAddress, rtpPort: IntArray): String {
        return "${address.hostAddress}:" +
                "${rtpPort.getOrElse(VIDEO_TRACK_ID) { 0 }}:" +
                "${rtpPort.getOrElse(AUDIO_TRACK_ID) { 0 }}"
    }
}