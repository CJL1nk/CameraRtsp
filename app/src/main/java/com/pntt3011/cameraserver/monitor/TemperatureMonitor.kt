package com.pntt3011.cameraserver.monitor

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Handler
import android.util.Log

class TemperatureMonitor(
    private val context: Context,
    private val handler: Handler,
) {
    companion object {
        private const val MONITOR_INTERVAL = 60_000L
    }

    fun start() {
        handler.post(periodicLog)
    }

    fun stop() {
        handler.removeCallbacks(periodicLog)
    }

    private val periodicLog = object : Runnable {
        override fun run() {
            logBatteryTemperature()
            handler.postDelayed(this, MONITOR_INTERVAL)
        }
    }

    private fun logBatteryTemperature() {
        val intentFilter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        val batteryStatus = context.registerReceiver(null, intentFilter)
        val temp = batteryStatus?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1) ?: -1
        val bHealth = batteryStatus?.getIntExtra(BatteryManager.EXTRA_HEALTH, -1) ?: -1
        val batteryHealth = when (bHealth) {
            BatteryManager.BATTERY_HEALTH_COLD -> "Cold"
            BatteryManager.BATTERY_HEALTH_DEAD -> "Dead"
            BatteryManager.BATTERY_HEALTH_GOOD -> "Good"
            BatteryManager.BATTERY_HEALTH_OVER_VOLTAGE -> "Over-Voltage"
            BatteryManager.BATTERY_HEALTH_OVERHEAT -> "Overheat"
            BatteryManager.BATTERY_HEALTH_UNKNOWN -> "Unknown"
            BatteryManager.BATTERY_HEALTH_UNSPECIFIED_FAILURE -> "Unspecified Failure"
            else -> "No Data"
        }
        Log.d("TemperatureMonitor", "Battery temperature: ${temp / 10.0}, Battery Health: $batteryHealth")
    }
}