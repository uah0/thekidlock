package com.blelock.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log

class ServiceRestartReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val settings = LockSettings(context)
        if (intent.action != ACTION_RESTART_SERVICE || !settings.autoOpenEnabled) return

        val service = Intent(context, BleLockService::class.java)
            .setAction(BleLockService.ACTION_START)
            .setPackage(context.packageName)
        runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(service)
            } else {
                context.startService(service)
            }
        }.onFailure { error ->
            Log.e("BleLockRestart", "Не удалось восстановить фоновую службу", error)
        }
    }

    companion object {
        const val ACTION_RESTART_SERVICE = "com.blelock.app.action.RESTART_SERVICE"
    }
}
