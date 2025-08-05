package com.pntt3011.cameraserver

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.widget.Button
import com.pntt3011.cameraserver.MainApplication.Companion.mainApplication

class MainActivity : Activity() {
    private val serviceStateListener = object : MainApplication.ServiceStateListener {
        override fun onChanged(running: Boolean) {
            val button = findViewById<Button>(R.id.start_button)
            button.setText(if (running) R.string.text_stop else R.string.text_start)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        setupButton()
    }

    private fun setupButton() {
        val button = findViewById<Button>(R.id.start_button)
        button.setOnClickListener {
            onButtonClick()
        }
    }

    private fun onButtonClick() {
        if (!allPermissionsGranted()) {
            requestPermissions(REQUIRED_PERMISSIONS, PERMISSIONS_REQUEST_CODE)
            return
        }

        if (!checkIgnoreBatteryOptimization()) {
            return
        }

        if (!mainApplication.isServiceRunning) {
            startService()
        } else {
            stopService()
        }
    }

    @SuppressLint("BatteryLife")
    private fun checkIgnoreBatteryOptimization(): Boolean {
        val pm = getSystemService(PowerManager::class.java)
        val packageName = packageName
        if (!pm.isIgnoringBatteryOptimizations(packageName)) {
            val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS)
            intent.data = Uri.parse("package:$packageName")
            startActivity(intent)
            return false
        }
        return true
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
    }

    private fun startService() {
        MainService.startService(this)
    }

    private fun stopService() {
        MainService.stopService(this)
    }

    override fun onStart() {
        super.onStart()
        mainApplication.addServiceStateListener(serviceStateListener)
    }

    override fun onPause() {
        super.onPause()
        mainApplication.removeServiceStateListener(serviceStateListener)
    }

    companion object {
        private val REQUIRED_PERMISSIONS = mutableListOf(
            Manifest.permission.CAMERA,
            Manifest.permission.RECORD_AUDIO
        ).apply {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                add(Manifest.permission.POST_NOTIFICATIONS)
            }
        }.toTypedArray()
        
        private const val PERMISSIONS_REQUEST_CODE = 123
    }
}