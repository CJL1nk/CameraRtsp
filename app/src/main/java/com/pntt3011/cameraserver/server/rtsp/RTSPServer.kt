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
    private val port: Int,
    private val connectionThreadPool: ExecutorService,
    private val sessionHandler: SessionHandler,
    private val onClosed: () -> Unit,
) {
    private val origin = "127.0.0.1"
    private var audioSdp = ""
    @Volatile
    private var isAudioReady = false

    private val rtspThread by lazy {
        Executors.newSingleThreadExecutor {
            Thread(it, "RTSPServerThread")
        }
    }
    private val runningSockets = ConcurrentHashMap<String, Socket>()

    interface SessionHandler {
        val handler: Handler
        fun onNewSession(address: InetAddress, rtpPort: Int)
        fun onDestroySession(address: InetAddress, rtpPort: Int)
    }

    fun onAudioPrepared(audioSdp: String) {
        this.audioSdp = audioSdp
        isAudioReady = true
    }

    private val server by lazy {
        ServerSocket(port)
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

        var clientRtpPort = port
        var clientRtcpPort: Int
        val clientAddress = socket.inetAddress
        val clientIp = clientAddress.hostAddress ?: ""

        val input = socket.getInputStream()
        val output = socket.getOutputStream()

        val requestBuffer = ByteArray(4096)
        var isClientStopped = false

        while (!socket.isClosed && !isClientStopped) {
            try {
                val bytesRead = input.read(requestBuffer)
                if (bytesRead <= 0) break

                val requestText = String(requestBuffer, 0, bytesRead)
                val requestLines = requestText.lines().filter { it.isNotBlank() }
                if (requestLines.isEmpty()) continue

                val requestLine = requestLines[0]
                val cseqLine = requestLines.find { it.startsWith("CSeq:", ignoreCase = true) }
                val cseq = cseqLine?.split(" ")?.getOrNull(1) ?: "1"

                val response = when {
                    !isResourceReady() -> {
                        "RTSP/1.0 501 Not Implemented\r\n" +
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
                        val clientPorts = Regex("client_port=(\\d+)-(\\d+)").find(transportLine ?: "")
                        clientRtpPort = clientPorts?.groupValues?.getOrNull(1)?.toIntOrNull() ?: 5004
                        clientRtcpPort = clientPorts?.groupValues?.getOrNull(2)?.toIntOrNull() ?: 5005
                        "RTSP/1.0 200 OK\r\n" +
                                "CSeq: $cseq\r\n" +
                                "Transport: RTP/AVP;unicast;client_port=$clientRtpPort-$clientRtcpPort;server_port=${port}-${port+1}\r\n" +
                                "Session: $sessionId\r\n" +
                                "\r\n"
                    }

                    requestLine.startsWith("PLAY", ignoreCase = true) -> {
                        sessionHandler.handler.post {
                            sessionHandler.onNewSession(clientAddress, clientRtpPort)
                        }
                        "RTSP/1.0 200 OK\r\n" +
                                "CSeq: $cseq\r\n" +
                                "Session: $sessionId\r\n" +
                                "\r\n"
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
                output.write(response.toByteArray())
                output.flush()

            } catch (e: SocketException) {
                break
            }
        }

        if (!socket.isClosed) {
            socket.close()
        }

        Log.d("RTSPServer", "Client $clientIp:$clientRtpPort disconnected, exiting listening loop.")
        runningSockets.remove(sessionId)
    }

    private fun isResourceReady(): Boolean {
        return isAudioReady
    }

    private fun prepareSdp(ip: String): String {
        return "v=0\r\n" +
                "o=- 0 0 IN IP4 $origin\r\n" +
                "s=Camera Stream\r\n" +
                "c=IN IP4 $ip\r\n" +
                "t=0 0\r\n" +
                audioSdp
    }
}