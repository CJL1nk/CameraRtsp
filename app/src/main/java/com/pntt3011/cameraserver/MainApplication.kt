package com.pntt3011.cameraserver

import android.app.Application
import android.os.Handler
import android.os.HandlerThread

class MainApplication: Application() {
    companion object {
        lateinit var mainApplication: MainApplication
        lateinit var workerThread: HandlerThread
        lateinit var workerHandler: Handler
        lateinit var mainHandler: Handler
    }

    private val stateListeners = mutableListOf<ServiceStateListener>()

    // Simulate reactive programming
    var isServiceRunning = false
        set(value) {
            field = value
            stateListeners.forEach { it.onChanged(value) }
        }

    fun addServiceStateListener(listener: ServiceStateListener) {
        stateListeners.add(listener)
        listener.onChanged(isServiceRunning)
    }

    fun removeServiceStateListener(listener: ServiceStateListener) {
        stateListeners.remove(listener)
    }

    override fun onCreate() {
        super.onCreate()
        mainApplication = this
        mainHandler = Handler(mainLooper)
        workerThread = HandlerThread("WorkerThread").also { it.start() }
        workerHandler = Handler(workerThread.looper)
        workerHandler.post {
            System.loadLibrary("cameraserver")
        }
    }

    interface ServiceStateListener {
        fun onChanged(running: Boolean)
    }
}