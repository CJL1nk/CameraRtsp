package com.pntt3011.cameraserver

import android.os.Handler
import android.util.Log
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class HTTPServer(
    private val port: Int,
    private val connectionThreadPool: ExecutorService,
    private val sessionHandler: SessionHandler,
    private val onClosed: () -> Unit,
) {
    private val origin = "127.0.0.1"
    private var audioSdp = ""
    @Volatile
    private var isAudioReady = false
    @Volatile
    private var isStopped = false

    private val httpThread by lazy {
        Executors.newSingleThreadExecutor {
            Thread(it, "HTTPServerThread")
        }
    }


    interface SessionHandler {
        val handler: Handler
        fun onNewSession(destination: String)
    }

    fun onAudioPrepared(audioSdp: String) {
        this.audioSdp = audioSdp
        isAudioReady = true
    }

    private val server by lazy {
        ServerSocket(port)
    }

    fun start() {
        httpThread.submit {
            try {
                while (!isStopped) {
                    try {
                        val client = server.accept()
                        connectionThreadPool.submit { handleClient(client) }
                    } catch (e: SocketException) {
                        isStopped = true
                        // This happens when socket is closed from another thread
                        Log.d("HTTPServer", "Socket closed, exiting listening loop.")
                    }
                }
            } catch (e: Exception) {
                Log.e("HTTPServer", "Exception", e)
            }
        }
    }

    fun stop() {
        isStopped = true
        sessionHandler.handler.post {
            server.close()
            httpThread.awaitTermination(1, TimeUnit.SECONDS)
            Log.d("CleanUp", "gracefully clean up http server")
            onClosed.invoke()
        }
    }

    private fun handleClient(socket: Socket) {
        val ip = socket.inetAddress.hostAddress ?: ""
        val writer = socket.getOutputStream()
        val response = if (ip.isNotEmpty() && isResourceReady()) {
            Log.d("HTTPServer", "response for $ip: 200")
            sdpContent(ip)
        } else {
            Log.d("HTTPServer", "response for $ip: 404")
            errorContent
        }
        writer.write(response.toByteArray())
        writer.flush()
        sessionHandler.handler.post {
            sessionHandler.onNewSession(ip)
        }
    }

    private fun isResourceReady(): Boolean {
        return isAudioReady
    }

    private val errorContent by lazy {
        val text = "Resource not found.\r\n"
        "HTTP/1.1 404 Not Found\r\n" +
            "Content-Type: text/plain\r\n" +
            "Content-Length: ${text.toByteArray().size}\r\n" +
            "\r\n" +
            text
    }

    private fun sdpContent(destination: String): String {
        val sdp = prepareSdp(destination)
        return "HTTP/1.1 200 OK\r\n" +
                "Content-Type: application/sdp\r\n" +
                "Content-Length: ${sdp.toByteArray().size}\r\n" +
                "\r\n" +
                sdp
    }

    private fun prepareSdp(destination: String): String {
        return "v=0\r\n" +
                "o=- 0 0 IN IP4 $origin\r\n" +
                "s=Camera Stream\r\n" +
                "c=IN IP4 $destination\r\n" +
                "t=0 0\r\n" +
                audioSdp
    }
}