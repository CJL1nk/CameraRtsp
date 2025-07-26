package com.pntt3011.cameraserver.server.rtp

import android.os.Handler
import android.util.Log
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketException
import java.net.SocketTimeoutException
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean


class RTCPValidation(
    rtpPort: Int,
    private val handler: Handler,
    private val onClosed: () -> Unit,
) {
    companion object {
        private const val TIME_TO_LIVE_MS = 10_000 // Actually 5s, but I double it
        private const val OK_SIGNAL = 1
        private const val LEAVING_SIGNAL = -1
    }

    private val rtcpPort = rtpPort + 1
    private val buffer = ByteArray(1500)
    private val socket by lazy { DatagramSocket(rtcpPort).apply { soTimeout = TIME_TO_LIVE_MS } }
    private val packet by lazy { DatagramPacket(buffer, buffer.size) }

    private class Signal(
        val lastPing: Long,
        val lastValue: Int,
    )
    private val signalMap = ConcurrentHashMap<String, Signal>()

    private val executor by lazy{
        Executors.newSingleThreadExecutor { runnable ->
            Thread(
                runnable,
                "RTCPListenerThread"
            )
        }
    }
    private var isStarted = AtomicBoolean(false)
    @Volatile
    private var isStopped = false

    fun checkRtcpReport(ip: String): Boolean {
        if (isStopped) {
            return false
        }
        checkStartListening()
        signalMap.getOrPut(ip) { Signal(System.currentTimeMillis(), OK_SIGNAL) }
        updateAllSignals()
        val signal = signalMap[ip]?.lastValue ?: OK_SIGNAL
        return signal == OK_SIGNAL
    }

    fun disconnect(ip: String) {
        signalMap.remove(ip)
    }

    fun stop() {
        isStopped = true
        handler.post {
            socket.close()
            executor.awaitTermination(1, TimeUnit.SECONDS)
            signalMap.clear()
            Log.d("CleanUp", "gracefully clean up rtp validation")
            onClosed.invoke()
        }
    }

    private fun updateAllSignals()  {
        for (signal in signalMap) {
            if (System.currentTimeMillis() - signal.value.lastPing > TIME_TO_LIVE_MS) {
                signalMap[signal.key] = Signal(System.currentTimeMillis(), LEAVING_SIGNAL)
            }
        }
    }

    private fun checkStartListening() {
        if (isStarted.compareAndSet(false, true)) {
            executor.submit {
                try {
                    startListening()
                } catch (e: Exception) {
                    Log.e("RTCP", e.message, e)
                }
            }
        }
    }

    private fun startListening() {
        while (!isStopped) {
            if (signalMap.isEmpty() && isStarted.compareAndSet(true, false)) {
                Log.w("RTCP", "No connection, stop listening")
                break
            }
            try {
                socket.receive(packet)
            } catch (e: SocketTimeoutException) {
                Log.d("RTCP", "Socket timeout")
                continue
            } catch (e: SocketException) {
                isStopped = true
                // This happens when socket is closed from another thread
                Log.d("RTCP", "Socket closed, exiting receive loop.")
            }
            val ip = packet.address?.hostAddress ?: continue
            val value = parseRTCP(ip, packet.data) ?: continue
            signalMap[ip] = Signal(System.currentTimeMillis(), value)
        }
    }

    private fun parseRTCP(ip: String, data: ByteArray): Int? {
        if (data.size < 8) return null

        val version = (data[0].toInt() shr 6) and 0x03
        if (version != 2) {
            Log.w("RTCP", "Unknown version from $ip: $version")
            return null
        }

        return when (val packetType = data[1].toInt() and 0xFF) {
            201, 202 -> {
                Log.d("RTCP", "OK packet type from $ip: $packetType")
                OK_SIGNAL
            }
            203 -> {
                Log.d("RTCP", "Leaving packet type from $ip: $packetType")
                LEAVING_SIGNAL
            }
            else -> {
                Log.d("RTCP", "Unknown packet type from $ip: $packetType")
                null
            }
        }
    }
}