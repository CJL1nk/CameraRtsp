package com.pntt3011.cameraserver

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.Message
import android.util.Log
import com.pntt3011.cameraserver.monitor.TemperatureMonitor

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

    fun start() {
        loadNativeLib()
        workerHandler.post {
            startNative(true, true)
        }
        temperatureMonitor.start()
    }

    fun stop() {
        workerHandler.post {
            stopNative()
            mainHandler.post {
                workerThread.quitSafely()
                workerThread.join()
                Log.d("CleanUp", "gracefully clean up all")
            }
        }
        temperatureMonitor.stop()
    }

    private fun loadNativeLib() {
        workerHandler.post {
            System.loadLibrary("cameraserver")
        }
    }

    private val temperatureMonitor by lazy {
        TemperatureMonitor(context, workerHandler)
    }

    private external fun startNative(video: Boolean, audio: Boolean)
    private external fun stopNative()
}