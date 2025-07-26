package com.pntt3011.cameraserver.server.rtp

import android.util.Log
import com.pntt3011.cameraserver.server.packetizer.AACLATMPacketizer
import com.pntt3011.cameraserver.server.packetizer.RTPPacketizer
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress

class RTPSession(
    private val ip: String,
    private val port: Int,
    private val audioPacketizer: AACLATMPacketizer,
    private val rtcpValidation: RTCPValidation,
    private val onClosed: (RTPSession) -> Unit
) {
    @Volatile
    private var isStopped = false

    private val socket by lazy {
        DatagramSocket()
    }

    fun start() {
        Log.d("RTPSession", "Opening new session at ${ip}:${port}")
        while (!isStopped) {
            trySendAudio()
            checkDisconnect()
        }
        socket.close()
        Log.d("CleanUp", "gracefully clean up rtp session ${ip}:${port}")
        onClosed(this)
    }

    private var audioSeq = ((Math.random() * 65536) % 65536).toInt()
    private val audioBuffer = RTPPacketizer.Buffer(ByteArray(0), 0, 0L)

    private fun trySendAudio() {
        audioPacketizer.getLastBuffer(audioBuffer)
        audioBuffer.injectSeqNumber(audioSeq)
        audioSeq = (audioSeq + 1) % 65536
        val packet = DatagramPacket(audioBuffer.data, audioBuffer.length, InetAddress.getByName(ip), port)
        Log.d("RTPSession", "Sending ${audioBuffer.length} audio bytes to ${ip}:${port}")
        socket.send(packet)
    }

    private fun checkDisconnect() {
        val isDisconnected = !rtcpValidation.checkRtcpReport(ip)
        if (isDisconnected) {
            stop()
            rtcpValidation.disconnect(ip)
        }
    }

    fun stop() {
        isStopped = true
    }
}