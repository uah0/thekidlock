package com.blelock.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val regularBoot = intent.action == Intent.ACTION_BOOT_COMPLETED
        val lockedBoot = Build.VERSION.SDK_INT >= Build.VERSION_CODES.N &&
            intent.action == Intent.ACTION_LOCKED_BOOT_COMPLETED
        if (!regularBoot && !lockedBoot) return
        if (!LockSettings(context).autoOpenEnabled) {
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            (context.checkSelfPermission(android.Manifest.permission.BLUETOOTH_SCAN) !=
                android.content.pm.PackageManager.PERMISSION_GRANTED ||
             context.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) !=
                android.content.pm.PackageManager.PERMISSION_GRANTED)) {
            return
        }
        val service = Intent(context, BleLockService::class.java)
            .setAction(BleLockService.ACTION_START)
            .setPackage(context.packageName)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(service)
        } else {
            context.startService(service)
        }
    }
}
