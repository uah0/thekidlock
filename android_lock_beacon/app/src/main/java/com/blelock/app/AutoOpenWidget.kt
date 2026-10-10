package com.blelock.app

import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.widget.RemoteViews

class AutoOpenWidget : AppWidgetProvider() {
    override fun onUpdate(context: Context, manager: AppWidgetManager, ids: IntArray) {
        update(context, manager, ids, LockSettings(context).autoOpenEnabled)
    }

    companion object {
        fun refresh(context: Context, enabled: Boolean) {
            val manager = AppWidgetManager.getInstance(context)
            update(context, manager, manager.getAppWidgetIds(
                ComponentName(context, AutoOpenWidget::class.java)), enabled)
        }

        private fun update(context: Context, manager: AppWidgetManager, ids: IntArray, enabled: Boolean) {
            if (ids.isEmpty()) return
            val intent = Intent(context, BleLockService::class.java)
                .setAction(BleLockService.ACTION_AUTO_OPEN_TOGGLE)
            val click = PendingIntent.getForegroundService(context, 0, intent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
            val views = RemoteViews(context.packageName, R.layout.widget_auto_open)
            views.setImageViewResource(R.id.widget_icon,
                if (enabled) R.drawable.ic_launcher_open else R.drawable.ic_launcher)
            views.setContentDescription(R.id.widget_button, context.getString(
                if (enabled) R.string.widget_auto_enabled else R.string.widget_auto_enable))
            views.setOnClickPendingIntent(R.id.widget_button, click)
            ids.forEach { manager.updateAppWidget(it, views) }
        }
    }
}
