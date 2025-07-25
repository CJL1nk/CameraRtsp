package com.pntt3011.cameraserver

import android.util.Log
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress

class RTPSession(
    private val ip: String,
    private val port: Int,
    private val audioPacketizer: AACLATMPacketizer,
    private val onClosed: (RTPSession) -> Unit
) {
    @Volatile
    private var isStopped = false

    private val socket by lazy {
        DatagramSocket()
    }

    fun start() {
        while (!isStopped) {
            trySendAudio()
        }
        socket.close()
        Log.d("CleanUp", "gracefully clean up rtp session ${ip}:${port}")
        onClosed(this)
    }

    private var audioSeq = UNINITIALIZED_SEQ_VALUE

    private fun trySendAudio() {
        val buffer = audioPacketizer.getLatestBuffer(0)
        if (buffer == null || buffer.seq == audioSeq) {
            Thread.sleep(1)
            return
        }
        val packet = DatagramPacket(buffer.data, buffer.length, InetAddress.getByName(ip), port)
        audioSeq = buffer.seq
        socket.send(packet)
    }

    fun stop() {
        isStopped = true
    }

    companion object {
        const val UNINITIALIZED_SEQ_VALUE = -1
    }
}