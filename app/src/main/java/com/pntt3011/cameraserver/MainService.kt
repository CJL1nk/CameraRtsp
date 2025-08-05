package com.pntt3011.cameraserver

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import com.pntt3011.cameraserver.MainApplication.Companion.mainApplication
import com.pntt3011.cameraserver.MainApplication.Companion.mainHandler
import com.pntt3011.cameraserver.MainApplication.Companion.workerHandler

class MainService : Service() {
    private var preventHarass = false
    private val mainController: MainController by lazy {
        MainController(applicationContext)
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            NOTIFICATION_CHANNEL_ID,
            "Camera Server",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Running in the background"
            setShowBadge(false)
        }

        val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE)
                as NotificationManager
        notificationManager.createNotificationChannel(channel)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                if (!mainApplication.isServiceRunning) {
                    startForegroundService()
                    startCameraService()
                }
            }
            ACTION_STOP -> {
                stopCameraService()
                stopSelf()
            }
        }
        return START_STICKY // Restart service if killed by system
    }

    override fun onDestroy() {
        super.onDestroy()
        stopCameraService()
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null // This is a started service, not bound
    }

    private fun startForegroundService() {
        val notification = createNotification()
        when {
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.R -> {
                startForeground(
                    NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA or
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
                )
            }
            else -> {
                startForeground(NOTIFICATION_ID, notification)
            }
        }
    }

    private fun createNotification(): Notification {
        // Intent to open the main activity when notification is tapped
        val notificationIntent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this, 0, notificationIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return Notification.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle("Camera Server")
            .setContentText("Running in the background")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }

    private fun startCameraService() {
        if (!mainApplication.isServiceRunning && !preventHarass) {
            preventHarass = true

            workerHandler.post {
                mainController.start(video = true, audio = true)

                mainHandler.post {
                    preventHarass = false
                    mainApplication.isServiceRunning = true
                }
            }
        }
    }

    private fun stopCameraService() {
        if (mainApplication.isServiceRunning && !preventHarass) {
            preventHarass = true

            workerHandler.post {
                mainController.stop()

                mainHandler.post {
                    preventHarass = false
                    mainApplication.isServiceRunning = false
                }
            }
        }
    }

    companion object {
        private const val NOTIFICATION_CHANNEL_ID = "camera_server_channel"
        private const val NOTIFICATION_ID = 1001
        
        const val ACTION_START = "com.pntt3011.cameraserver.ACTION_START"
        const val ACTION_STOP = "com.pntt3011.cameraserver.ACTION_STOP"

        fun startService(context: Context) {
            val intent = Intent(context, MainService::class.java).apply {
                action = ACTION_START
            }
            context.startForegroundService(intent)
        }

        fun stopService(context: Context) {
            val intent = Intent(context, MainService::class.java).apply {
                action = ACTION_STOP
            }
            context.startService(intent)
        }
    }
}
