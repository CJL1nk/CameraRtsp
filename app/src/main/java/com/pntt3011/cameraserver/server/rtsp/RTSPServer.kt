package com.pntt3011.cameraserver.server.rtsp

import android.os.Handler
import android.util.Log
import com.pntt3011.cameraserver.monitor.TrackPerfMonitor
import java.io.OutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class RTSPServer(
    private val rtspPort: Int,
    private val trackInfos: Array<TrackInfo>,
    private val connectionThreadPool: ExecutorService,
    private val sessionHandler: SessionHandler,
    private val onClosed: () -> Unit,
) {
    private val origin = "127.0.0.1"
    @Volatile
    private var isResourceReady = false

    private val rtspThread by lazy {
        Executors.newSingleThreadExecutor {
            Thread(it, "RTSPServerThread")
        }
    }
    private val runningSockets = ConcurrentHashMap<String, Socket>()

    interface SessionHandler {
        val handler: Handler
        fun onNewSession(address: InetAddress, trackInfos: Array<TrackInfo>)
        fun onNewTcpSession(outputStream: OutputStream)
        fun onDestroySession(address: InetAddress, trackInfos: Array<TrackInfo>)
    }

    fun onMediaPrepared(sdp: String, isVideo: Boolean) {
        val trackName = if (isVideo) "video" else "audio"
        trackInfos.find { it.isVideo == isVideo }?.sdp = sdp
        Log.d("RTSPServer", "$trackName prepared: $sdp")
        isResourceReady = trackInfos.all { it.sdp.isNotEmpty() }
    }

    private val server by lazy {
        ServerSocket(rtspPort)
    }

    fun start() {
        rtspThread.submit {
            try {
                while (!server.isClosed) {
                    try {
                        val client = server.accept()
                        connectionThreadPool.submit { handleClient(client) }
                    } catch (e: SocketException) {
                        Log.d("RTSPServer", "Socket closed, exiting listening loop.")
                    }
                }
            } catch (e: Exception) {
                Log.e("RTSPServer", "Exception", e)
            }
        }
    }

    fun stop() {
        sessionHandler.handler.post {
            runningSockets.values.forEach { it.close() }
            server.close()
            rtspThread.awaitTermination(1, TimeUnit.SECONDS)
            Log.d("CleanUp", "gracefully clean up rtsp server")
            onClosed.invoke()
        }
    }

    private fun handleClient(socket: Socket) {
        val sessionId = UUID.randomUUID().toString()
        runningSockets[sessionId] = socket

        val clientAddress = socket.inetAddress
        val clientIp = clientAddress.hostAddress ?: ""

        val input = socket.getInputStream()
        val output = socket.getOutputStream()

        val requestBuffer = ByteArray(4096)
        var isClientStopped = false

        Log.d("RTSPServer", "handle client $clientIp")
        Log.d("RTSPServer", "start session $sessionId")

        while (!socket.isClosed && !isClientStopped) {
            try {
                val bytesRead = input.read(requestBuffer)
                if (bytesRead <= 0) break

                val requestText = String(requestBuffer, 0, bytesRead)
                val requestLines = requestText.lines().filter { it.isNotBlank() }
                if (requestLines.isEmpty()) continue

                Log.d("RTSPServer", "handle request from $sessionId: $requestLines")

                val requestLine = requestLines[0]
                val cseqLine = requestLines.find { it.startsWith("CSeq:", ignoreCase = true) }
                val cseq = cseqLine?.split(" ")?.getOrNull(1) ?: "1"
                val trackId = Regex("trackID=(\\d+)").find(requestLine)?.groupValues?.get(1)
                    ?.toIntOrNull()

                val response = when {
                    !isResourceReady -> {
                        "RTSP/1.0 400 Bad Request\r\n" +
                                "CSeq: $cseq\r\n" +
                                "\r\n"
                    }

                    requestLine.startsWith("OPTIONS", ignoreCase = true) -> {
                        "RTSP/1.0 200 OK\r\n" +
                                "CSeq: $cseq\r\n" +
                                "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n" +
                                "\r\n"
                    }

                    requestLine.startsWith("DESCRIBE", ignoreCase = true) -> {
                        val sdp = prepareSdp(clientIp)
                        "RTSP/1.0 200 OK\r\n" +
                                "CSeq: $cseq\r\n" +
                                "Content-Type: application/sdp\r\n" +
                                "Content-Length: ${sdp.toByteArray().size}\r\n" +
                                "\r\n" +
                                sdp
                    }

                    requestLine.startsWith("SETUP", ignoreCase = true) -> {
                        val interleave = trackInfos[trackId ?: 0].interleave
                        "RTSP/1.0 200 OK\r\n" +
                                "CSeq: $cseq\r\n" +
                                "Transport: Transport: RTP/AVP/TCP;interleaved=$interleave-${interleave+1};unicast\r\n" +
                                "Session: $sessionId\r\n" +
                                "\r\n"
                    }

                    requestLine.startsWith("PLAY", ignoreCase = true) -> {
                        sessionHandler.handler.post {
                            sessionHandler.onNewTcpSession(socket.getOutputStream())
                        }
                        "RTSP/1.0 200 OK\r\n" +
                                "CSeq: $cseq\r\n" +
                                "Session: $sessionId\r\n" +
                                "\r\n"
                    }

                    requestLine.startsWith("TEARDOWN", ignoreCase = true) -> {
                        isClientStopped = true
                        "RTSP/1.0 200 OK\r\n" +
                                "CSeq: $cseq\r\n" +
                                "Session: $sessionId\r\n" +
                                "\r\n"
                    }

                    else -> {
                        continue
                    }
                }
                Log.d("RTSPServer", "response to $sessionId: $response")

                output.write(response.toByteArray())
                output.flush()

            } catch (e: SocketException) {
                break
            }
        }

        if (!socket.isClosed) {
            socket.close()
        }

        Log.d("RTSPServer", "Client $clientIp disconnected, exiting listening loop.")
        runningSockets.remove(sessionId)
    }

    private fun prepareSdp(ip: String): String {
        val general = "v=0\r\n" +
                "o=- 0 0 IN IP4 $origin\r\n" +
                "s=Camera Stream\r\n" +
                "c=IN IP4 $ip\r\n" +
                "t=0 0\r\n" +
                "a=control:*\r\n"

        var trackSpecific =""
        trackInfos.forEachIndexed { index, trackInfo ->
            trackSpecific += "\r\n"
            trackSpecific += trackInfo.sdp
            trackSpecific += "a=control:trackID=$index\r\n"
        }

        return general + trackSpecific
    }

    class TrackInfo(val serverRtpPort: Int, val isVideo: Boolean, val interleave: Int, handler: Handler) {
        var clientRtpPort = 0
        var sdp = ""
        val perfMonitor = TrackPerfMonitor(isVideo, handler)
    }
}