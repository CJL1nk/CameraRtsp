package com.pntt3011.cameraserver.server

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Handler
import android.util.Log
import com.pntt3011.cameraserver.server.packetizer.PacketizerPool
import com.pntt3011.cameraserver.server.rtp.RTPSession
import com.pntt3011.cameraserver.server.rtp.RTPTCPSession
import com.pntt3011.cameraserver.server.rtsp.RTSPServer
import java.io.OutputStream
import java.net.InetAddress
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class MainServer(
    private val rtspPort: Int,
    private val trackInfos: Array<RTSPServer.TrackInfo>,
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
    private val packetizers by lazy { PacketizerPool() }
    private val rtspServer by lazy {
        RTSPServer(
            rtspPort,
            trackInfos,
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

            override fun onNewSession(address: InetAddress, trackInfos: Array<RTSPServer.TrackInfo>) {
                startNewRtpSessionIfNeeded(address, trackInfos)
            }

            override fun onNewTcpSession(outputStream: OutputStream) {
                RTPTCPSession(
                    outputStream,
                    trackInfos,
                    packetizers,
                    connectionThreadPool,
                ).start()
            }

            override fun onDestroySession(address: InetAddress, trackInfos: Array<RTSPServer.TrackInfo>) {
                val key = getRtpKey(address, trackInfos)
                rtpSessions[key]?.stop()
            }
        }
    }
    fun start() {
        rtspServer.start()
    }

    fun onMediaPrepared(mediaFormat: MediaFormat, isVideo: Boolean) {
        packetizers.get(isVideo).let {
            it.onPrepared(mediaFormat)
            rtspServer.onMediaPrepared(it.sdp, isVideo)
        }
    }

    fun onMediaPrepared(mediaBuffer: ByteBuffer, isVideo: Boolean) {
        packetizers.get(isVideo).let {
            it.onPrepared(mediaBuffer)
            rtspServer.onMediaPrepared(it.sdp, isVideo)
        }
    }

    fun onFrameReceived(byteBuffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo, isVideo: Boolean) {
        packetizers.get(isVideo).onFrameReceived(byteBuffer, bufferInfo)
    }

    fun stop() {
        rtspServer.stop()
        rtpSessions.values.forEach { it.stop() }
    }

    private fun startNewRtpSessionIfNeeded(clientAddress: InetAddress, trackInfos: Array<RTSPServer.TrackInfo>) {
        val key = getRtpKey(clientAddress, trackInfos)
        if (rtpSessions.containsKey(key)) {
            return
        }

        rtpSessions[key] = RTPSession(
            clientAddress,
            trackInfos,
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

    private fun getRtpKey(address: InetAddress, rtpPort: Array<RTSPServer.TrackInfo>): String {
        return "${address.hostAddress}:" + rtpPort.joinToString(",") { "${it.clientRtpPort}" }
    }
}