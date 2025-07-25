package com.pntt3011.cameraserver

import android.util.Log
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

    private var audioSeq = UNINITIALIZED_SEQ_VALUE

    private fun trySendAudio() {
        val buffer = audioPacketizer.getLatestBuffer(audioSeq)
        audioSeq = buffer.seq
        val packet = DatagramPacket(buffer.data, buffer.length, InetAddress.getByName(ip), port)
        Log.d("RTPSession", "Sending ${buffer.length} bytes to ${ip}:${port}")
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

    companion object {
        const val UNINITIALIZED_SEQ_VALUE = -1
    }
}