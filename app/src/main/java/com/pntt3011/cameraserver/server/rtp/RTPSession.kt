package com.pntt3011.cameraserver.server.rtp

import android.os.Handler
import android.util.Log
import com.pntt3011.cameraserver.server.packetizer.PacketizerPool
import com.pntt3011.cameraserver.server.packetizer.RTPPacketizer
import com.pntt3011.cameraserver.server.rtsp.RTSPServer
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketException
import java.util.concurrent.ExecutorService
import kotlin.random.Random

class RTPSession(
    private val clientAddress: InetAddress,
    private val trackInfos: Array<RTSPServer.TrackInfo>,
    private val packetizerPool: PacketizerPool,
    private val connectionThreadPool: ExecutorService,
    private val handler: Handler,
    private val onClosed: (RTPSession) -> Unit
) {
    private val socket by lazy {
        Array(trackInfos.size) {
            DatagramSocket(trackInfos[it].serverRtpPort)
        }
    }
    private val seq = Array(trackInfos.size) {
        ((Math.random() * 65536) % 65536).toInt()
    }
    private val buffer = Array(trackInfos.size) {
        RTPPacketizer.FramePacket()
    }
    private val ssrc = Array(trackInfos.size) {
        Random(123 + it).nextInt()
    }

    fun start() {
        trackInfos.forEachIndexed { index, trackInfo ->
            connectionThreadPool.submit {
                startServer(index, trackInfo)
            }
        }
    }

    private fun startServer(index: Int, trackInfo: RTSPServer.TrackInfo) {
        val clientPort = trackInfo.clientRtpPort
        val clientAddress = clientAddress
        val trackName = if (trackInfo.isVideo) "video" else "audio"
        Log.d("RTPSession", "Streaming $trackName at ${clientAddress}:${clientPort}")
        while (!socket[index].isClosed) {
            try {
                trySendData(index, trackInfo)
            } catch (e: SocketException) {
                break
            }
        }
        Log.d("CleanUp", "gracefully clean up $trackName stream ${clientAddress}:${clientPort}")
        checkClose()
    }

    private fun trySendData(index: Int, trackInfo: RTSPServer.TrackInfo) {
        buffer[index].apply {
            packetizerPool.get(trackInfo.isVideo).awaitFramePacket(this)
            for (i in 0 until numPacket) {
                inject(seq[index], ssrc[index])
                val packet = DatagramPacket(
                    rtpPacket[i].data,
                    rtpPacket[i].size,
                    clientAddress,
                    trackInfo.clientRtpPort,
                )
                socket[index].send(packet)
                seq[index] = (seq[index] + 1) % 65536
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