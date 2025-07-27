package com.pntt3011.cameraserver.server.rtp

import android.util.Log
import com.pntt3011.cameraserver.server.packetizer.AACLATMPacketizer
import com.pntt3011.cameraserver.server.packetizer.RTPPacketizer
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketException
import java.util.concurrent.ExecutorService
import kotlin.random.Random

class RTPSession(
    private val clientAddress: InetAddress,
    private val clientRtpPort: Int,
    private val audioPacketizer: AACLATMPacketizer,
    private val connectionThreadPool: ExecutorService,
    private val onClosed: (RTPSession) -> Unit
) {
    private val socket by lazy {
        DatagramSocket()
    }

    fun start() {
        connectionThreadPool.submit {
            Log.d("RTPSession", "Opening new session at ${clientAddress}:${clientRtpPort}")
            while (!socket.isClosed) {
                try {
                    trySendAudio()
                } catch (e: SocketException) {
                    break
                }
            }
            Log.d("CleanUp", "gracefully clean up rtp session ${clientAddress}:${clientRtpPort}")
            onClosed(this)
        }
    }

    private var audioSeq = ((Math.random() * 65536) % 65536).toInt()
    private val audioBuffer = RTPPacketizer.Buffer(ByteArray(0), 0, 0)
    private val audioSsrc = Random(123).nextInt()

    private fun trySendAudio() {
        audioPacketizer.getLastBuffer(audioBuffer)
        audioBuffer.injectSeqNumber(audioSeq)
        audioBuffer.injectSrccNumber(audioSsrc)
        audioSeq = (audioSeq + 1) % 65536
        val packet = DatagramPacket(audioBuffer.data, audioBuffer.length, clientAddress, clientRtpPort)
        socket.send(packet)
    }

    fun stop() {
        socket.close()
    }
}