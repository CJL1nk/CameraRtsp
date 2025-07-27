package com.pntt3011.cameraserver

import android.content.Context
import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.Message
import android.util.Log
import com.pntt3011.cameraserver.server.MainServer
import com.pntt3011.cameraserver.source.AudioSource
import com.pntt3011.cameraserver.source.CameraSource
import com.pntt3011.cameraserver.source.SourceCallback
import java.nio.ByteBuffer

class MainController(context: Context) {
    private val mainHandler by lazy {
        Handler(Looper.getMainLooper())
    }
    private val workerThread by lazy {
        HandlerThread("WorkerThread").apply { start() }
    }
    private val workerHandler by lazy {
        object : Handler(workerThread.looper) {
            override fun handleMessage(msg: Message) {
                try {
                    super.handleMessage(msg)  // default behavior (if any)
                } catch (e: Exception) {
                    Log.e("WorkerHandler", "Exception in handleMessage: ${e.message}", e)
                }
            }
        }
    }
    private val server by lazy {
        MainServer(8554, 5004, workerHandler) {
            stoppedServer = true
            checkStop()
        }
    }
    private val cameraSource by lazy {
        CameraSource(context, object : SourceCallback {
            override val handler: Handler
                get() = workerHandler

            override fun onPrepared(format: MediaFormat) {
            }

            override fun onFrameAvailable(buffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo) {
            }

            override fun onClosed() {
                stoppedVideo = true
                checkStop()
            }
        })
    }
    private val audioSource by lazy {
        AudioSource(object : SourceCallback {
            override val handler: Handler
                get() = workerHandler

            override fun onPrepared(format: MediaFormat) {
                server.onAudioPrepared(format)
            }

            override fun onFrameAvailable(buffer: ByteBuffer, bufferInfo: MediaCodec.BufferInfo) {
                server.onAudioFrameReceived(buffer, bufferInfo)
            }

            override fun onClosed() {
                stoppedAudio = true
                checkStop()
            }
        })
    }

    fun start() {
        server.start()
        audioSource.start()
        cameraSource.start()
    }

    fun stop() {
        server.stop()
        audioSource.stop()
        cameraSource.stop()
    }

    private var stoppedVideo = false
    private var stoppedAudio = false
    private var stoppedServer = false

    private fun checkStop() {
        if (stoppedVideo && stoppedAudio && stoppedServer) {
            cleanUp()
        }
    }

    private fun cleanUp() {
        mainHandler.post {
            workerThread.quitSafely()
            workerThread.join()
            Log.d("CleanUp", "gracefully clean up all")
        }
    }
}