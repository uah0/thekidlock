package com.blelock.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        // MainActivity owns service startup. Boot-time service recovery is
        // intentionally disabled because some vendor Android builds deliver
        // boot/package broadcasts while the Activity is being restored,
        // causing a duplicate foreground-service start and process death.
        return
        /*
        // Package replacement is handled by MainActivity on the next launch.
        // Starting the service here races with Activity.onResume(), which
        // deliberately restarts the service and can trigger Android's
        // foreground-service startup timeout.
        val regularBoot = intent.action == Intent.ACTION_BOOT_COMPLETED
        val lockedBoot = Build.VERSION.SDK_INT >= Build.VERSION_CODES.N &&
            intent.action == Intent.ACTION_LOCKED_BOOT_COMPLETED
        if (!regularBoot && !lockedBoot) return
        if (!LockSettings(context).autoOpenEnabled) {
            return
        }
        val service = Intent(context, BleLockService::class.java)
            .setAction(BleLockService.ACTION_START)
            .setPackage(context.packageName)
        // Start while the BOOT_COMPLETED receiver is alive. Delaying with a bare
        // Handler after onReceive returns allows Android to kill the process.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(service)
        } else {
            context.startService(service)
        }
        */
    }
}
