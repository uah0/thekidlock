package com.blelock.app

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.UserManager
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.work.Constraints
import androidx.work.Data
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequest
import androidx.work.PeriodicWorkRequest
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import java.util.concurrent.TimeUnit

class AppUpdateWorker(context: Context, parameters: WorkerParameters) : Worker(context, parameters) {
    override fun doWork(): Result {
        val context = applicationContext
        if (context.getSystemService(UserManager::class.java)?.isUserUnlocked != true) return Result.retry()
        val connectivity = context.getSystemService(ConnectivityManager::class.java)
        val network = connectivity.getNetworkCapabilities(connectivity.activeNetwork)
        if (network?.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) != true) return Result.retry()
        return try {
            val metadata = AppUpdateClient.checkAndDownload(context, inputData.getBoolean("force", false)) { isStopped }
            if (metadata != null) notifyReady(context, metadata)
            context.sendBroadcast(Intent(ACTION_CHANGED).setPackage(context.packageName))
            Result.success()
        } catch (error: Exception) {
            AppUpdateClient.preferences(context).edit().putString("last_error", error.message).apply()
            context.sendBroadcast(Intent(ACTION_CHANGED).setPackage(context.packageName))
            if (runAttemptCount < 3) Result.retry() else Result.failure()
        }
    }

    companion object {
        const val ACTION_CHANGED = "com.blelock.app.APP_UPDATE_CHANGED"
        const val ACTION_SHOW = "com.blelock.app.SHOW_APP_UPDATE"
        const val NOTIFICATION_ID = 2002
        private val constraints = Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build()

        fun schedule(context: Context) {
            WorkManager.getInstance(context).enqueueUniquePeriodicWork("app-update-periodic",
                ExistingPeriodicWorkPolicy.KEEP,
                PeriodicWorkRequest.Builder(AppUpdateWorker::class.java, 6, TimeUnit.HOURS)
                    .setConstraints(constraints).build())
        }

        fun checkNow(context: Context, force: Boolean = false) {
            WorkManager.getInstance(context).enqueueUniqueWork("app-update-check",
                if (force) ExistingWorkPolicy.REPLACE else ExistingWorkPolicy.KEEP,
                OneTimeWorkRequest.Builder(AppUpdateWorker::class.java).setConstraints(constraints)
                    .setInputData(Data.Builder().putBoolean("force", force).build()).build())
        }

        private fun notifyReady(context: Context, metadata: AppUpdateMetadata) {
            val preferences = AppUpdateClient.preferences(context)
            if (preferences.getInt("notified_version", 0) == metadata.versionCode ||
                !NotificationManagerCompat.from(context).areNotificationsEnabled()) return
            val manager = context.getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(NotificationChannel("app-updates", "Обновления приложения",
                NotificationManager.IMPORTANCE_DEFAULT))
            val click = PendingIntent.getActivity(context, 2002,
                Intent(context, MainActivity::class.java).setAction(ACTION_SHOW),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
            try {
                manager.notify(NOTIFICATION_ID, NotificationCompat.Builder(context, "app-updates")
                    .setSmallIcon(R.drawable.ic_handoff_cloud).setContentTitle("Доступно обновление TheKidLock")
                    .setContentText("Версия ${metadata.versionName} скачана. Нажмите, чтобы установить.")
                    .setContentIntent(click).setAutoCancel(true).build())
                preferences.edit().putInt("notified_version", metadata.versionCode).apply()
            } catch (_: SecurityException) { /* The user may revoke notification permission. */ }
        }
    }
}
