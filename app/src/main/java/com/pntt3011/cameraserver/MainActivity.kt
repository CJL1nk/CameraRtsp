package com.pntt3011.cameraserver

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.WindowManager
import android.widget.Button

class MainActivity : Activity() {
    private var server: Server? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val button = findViewById<Button>(R.id.start_button)
        button.setOnClickListener {
            if (server == null) {
                if (allPermissionsGranted()) {
                    startServer()
                    button.text = "STOP"
                } else {
                    requestPermissions(REQUIRED_PERMISSIONS, PERMISSIONS_REQUEST_CODE)
                }
            } else {
                stopServer()
                button.text = "START"
            }
        }
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
    }

    private fun startServer() {
        if (server != null) {
            return
        }
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return
        }
        server = Server(baseContext)
        server?.start()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopServer()
        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    private fun stopServer() {
        server?.stop()
        server = null
    }

    companion object {
        private val REQUIRED_PERMISSIONS =
            listOf(
                Manifest.permission.CAMERA,
                Manifest.permission.RECORD_AUDIO
            ).toTypedArray()
        private const val PERMISSIONS_REQUEST_CODE = 123
    }
}