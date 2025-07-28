package com.pntt3011.cameraserver.server.rtp

import com.pntt3011.cameraserver.server.packetizer.PacketizerPool
import com.pntt3011.cameraserver.server.packetizer.RTPPacketizer
import com.pntt3011.cameraserver.server.rtsp.RTSPServer
import java.io.OutputStream
import java.net.SocketException
import java.util.concurrent.ExecutorService
import kotlin.random.Random

class RTPTCPSession(
    private val outputStream: OutputStream,
    private val trackInfos: Array<RTSPServer.TrackInfo>,
    private val packetizerPool: PacketizerPool,
    private val connectionThreadPool: ExecutorService,
) {
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
        packetizerPool.get(trackInfo.isVideo).addTrackPerf(trackInfo.perfMonitor)
        while (true) {
            try {
                trySendData(index, trackInfo)
            } catch (e: SocketException) {
                break
            }
        }
        packetizerPool.get(trackInfo.isVideo).removeTrackPef(trackInfo.perfMonitor)
    }

    private fun trySendData(index: Int, trackInfo: RTSPServer.TrackInfo) {
        buffer[index].apply {
            packetizerPool.get(trackInfo.isVideo).awaitFramePacket(this)
            for (i in 0 until numPacket) {
                inject(seq[index], ssrc[index])
                val header = ByteArray(4)
                header[0] = '$'.code.toByte()
                header[1] = trackInfo.interleave.toByte()
                header[2] = ((rtpPacket[i].size shr 8) and 0xFF).toByte()
                header[3] = (rtpPacket[i].size and 0xFF).toByte()

                synchronized(outputStream) {
                    outputStream.write(header + rtpPacket[i].data.copyOfRange(0, rtpPacket[i].size))
                    outputStream.flush()
                }
                seq[index] = (seq[index] + 1) % 65536
            }
            trackInfo.perfMonitor.onFrameSend(frameTimestampUs)
        }
    }
}