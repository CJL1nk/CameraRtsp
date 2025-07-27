package com.pntt3011.cameraserver.server.rtp

import android.os.Handler
import android.util.Log
import com.pntt3011.cameraserver.server.packetizer.RTPPacketizer
import com.pntt3011.cameraserver.server.rtsp.RTSPServer.Companion.VIDEO_TRACK_ID
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketException
import java.util.concurrent.ExecutorService
import kotlin.random.Random

class RTPSession(
    private val serverRtpPort: IntArray,
    private val clientAddress: InetAddress,
    private val clientRtpPort: IntArray,
    private val packetizers: Array<RTPPacketizer>,
    private val connectionThreadPool: ExecutorService,
    private val handler: Handler,
    private val onClosed: (RTPSession) -> Unit
) {
    init {
        assert(serverRtpPort.size == clientRtpPort.size)
    }
    private val socket by lazy {
        Array(serverRtpPort.size) {
            DatagramSocket(serverRtpPort[it])
        }
    }
    private val seq = Array(serverRtpPort.size) {
        ((Math.random() * 65536) % 65536).toInt()
    }
    private val buffer = Array(serverRtpPort.size) {
        RTPPacketizer.FramePacket()
    }
    private val ssrc = Array(serverRtpPort.size) {
        Random(123 + it).nextInt()
    }

    fun start() {
        serverRtpPort.indices.forEach {
            connectionThreadPool.submit {
                startServer(it)
            }
        }
    }

    private fun startServer(trackId: Int) {
        val clientPort = clientRtpPort[trackId]
        val clientAddress = clientAddress
        val trackName = if (trackId == VIDEO_TRACK_ID) "video" else "audio"
        Log.d("RTPSession", "Streaming $trackName at ${clientAddress}:${clientPort}")
        while (!socket[trackId].isClosed) {
            try {
                trySendData(trackId)
            } catch (e: SocketException) {
                break
            }
        }
        Log.d("CleanUp", "gracefully clean up $trackName stream ${clientAddress}:${clientPort}")
        checkClose()
    }

    private fun trySendData(trackId: Int) {
        buffer[trackId].apply {
            packetizers[trackId].awaitFramePacket(this)
            for (i in 0 until numPacket) {
                inject(seq[trackId], ssrc[trackId])
                val packet = DatagramPacket(
                    rtpPacket[i].data,
                    rtpPacket[i].size,
                    clientAddress,
                    clientRtpPort[trackId]
                )
                socket[trackId].send(packet)
                seq[trackId] = (seq[trackId] + 1) % 65536
            }
        }
    }

    fun stop() {
        socket.forEach { it.close() }
    }

    private fun checkClose() {
        handler.post {
            if (socket.all { it.isClosed }) {
                onClosed(this)
            }
        }
    }
}