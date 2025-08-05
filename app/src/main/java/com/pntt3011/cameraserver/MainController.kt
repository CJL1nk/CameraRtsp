package com.pntt3011.cameraserver

import android.content.Context
import androidx.annotation.WorkerThread
import com.pntt3011.cameraserver.MainApplication.Companion.workerHandler
import com.pntt3011.cameraserver.monitor.TemperatureMonitor

class MainController(context: Context) {

    private val temperatureMonitor by lazy {
        TemperatureMonitor(context, workerHandler)
    }

    @WorkerThread
    fun start(video: Boolean, audio: Boolean) {
        startNative(video, audio)
        temperatureMonitor.start()
    }

    @WorkerThread
    fun stop() {
        stopNative()
        temperatureMonitor.stop()
    }

    @WorkerThread
    private external fun startNative(video: Boolean, audio: Boolean)

    @WorkerThread
    private external fun stopNative()
}