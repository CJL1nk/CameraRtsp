package com.pntt3011.cameraserver.server.rtsp

import android.os.Handler
import android.util.Log
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
    private val rtpPort: IntArray,
    private val connectionThreadPool: ExecutorService,
    private val sessionHandler: SessionHandler,
    private val onClosed: () -> Unit,
) {
    private val origin = "127.0.0.1"
    private val sdp = Array(2) { "" }
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
        fun onNewSession(address: InetAddress, rtpPort: IntArray)
        fun onDestroySession(address: InetAddress, rtpPort: IntArray)
    }

    fun onMediaPrepared(sdp: String, trackId: Int) {
        if (trackId == VIDEO_TRACK_ID || trackId == AUDIO_TRACK_ID) {
            this.sdp[trackId] = sdp
            val trackName = if (trackId == VIDEO_TRACK_ID) "video" else "audio"
            Log.d("RTSPServer", "$trackName prepared: $sdp")
            isResourceReady = this.sdp.all { it.isNotEmpty() }
        }
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

        val clientRtpPort = IntArray(2)
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
                    ?.takeIf { it == VIDEO_TRACK_ID || it == AUDIO_TRACK_ID }

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
                        val transportLine = requestLines.find { it.startsWith("Transport:") }
                        val ports = Regex("client_port=(\\d+)-(\\d+)").find(transportLine ?: "")
                        val rtpPort = ports?.groupValues?.getOrNull(1)?.toIntOrNull()
                        val rtcpPort = ports?.groupValues?.getOrNull(2)?.toIntOrNull()
                        if (rtpPort == null || rtcpPort == null || trackId == null) {
                            "RTSP/1.0 400 Bad Request\r\n" +
                                    "CSeq: $cseq\r\n" +
                                    "\r\n"
                        } else {
                            val serverPort = this.rtpPort[trackId]
                            clientRtpPort[trackId] = rtpPort
                            "RTSP/1.0 200 OK\r\n" +
                                    "CSeq: $cseq\r\n" +
                                    "Transport: RTP/AVP;unicast;client_port=$rtpPort-$rtcpPort;server_port=${serverPort}-${serverPort+1}\r\n" +
                                    "Session: $sessionId\r\n" +
                                    "\r\n"
                        }
                    }

                    requestLine.startsWith("PLAY", ignoreCase = true) -> {
                        if (clientRtpPort.any { it == 0 } ) {
                            "RTSP/1.0 400 Bad Request\r\n" +
                                    "CSeq: $cseq\r\n" +
                                    "\r\n"
                        } else {
                            sessionHandler.handler.post {
                                sessionHandler.onNewSession(clientAddress, clientRtpPort)
                            }
                            "RTSP/1.0 200 OK\r\n" +
                                    "CSeq: $cseq\r\n" +
                                    "Session: $sessionId\r\n" +
                                    "\r\n"
                        }
                    }

                    requestLine.startsWith("TEARDOWN", ignoreCase = true) -> {
                        sessionHandler.handler.post {
                            sessionHandler.onDestroySession(clientAddress, clientRtpPort)
                        }
                        isClientStopped = true
                        "RTSP/1.0 200 OK\r\n" +
                                "CSeq: $cseq\r\n" +
                                "Session: $sessionId\r\n" +
                                "\r\n"
                    }

                    else -> {
                        "RTSP/1.0 501 Not Implemented\r\n" +
                                "CSeq: $cseq\r\n" +
                                "\r\n"
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
        return "v=0\r\n" +
                "o=- 0 0 IN IP4 $origin\r\n" +
                "s=Camera Stream\r\n" +
                "c=IN IP4 $ip\r\n" +
                "t=0 0\r\n" +
                "a=control:*\r\n" +
                "\r\n" +
                sdp[VIDEO_TRACK_ID] +
                "a=control:trackID=$VIDEO_TRACK_ID\r\n" +
                "\r\n" +
                sdp[AUDIO_TRACK_ID] +
                "a=control:trackID=$AUDIO_TRACK_ID\r\n"
    }

    companion object {
        const val VIDEO_TRACK_ID = 0
        const val AUDIO_TRACK_ID = 1
    }
}