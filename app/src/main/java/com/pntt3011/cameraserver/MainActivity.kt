package com.pntt3011.cameraserver

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper

class MainActivity : Activity() {
    private var server: CameraServer? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        if (allPermissionsGranted()) {
            startServer()
        } else {
            requestPermissions(REQUIRED_PERMISSIONS, PERMISSIONS_REQUEST_CODE)
        }
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
    }

    private fun startServer() {
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return
        }
        assert(server == null) { "Old server must be cleared before starting new server" }
        server = CameraServer(baseContext)
        server?.start()

        Handler(Looper.getMainLooper()).postDelayed({
            stopServer()
        }, 10_000L)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERMISSIONS_REQUEST_CODE) {
            if (allPermissionsGranted()) {
                startServer()
            } else {
                finish()
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopServer()
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