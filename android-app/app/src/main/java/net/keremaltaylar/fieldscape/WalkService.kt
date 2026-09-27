package net.keremaltaylar.fieldscape

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Build
import android.os.IBinder

/**
 * 5.8: keeps the walk going with the screen locked. Android stops a backgrounded app's location
 * updates (and may stop the process) unless a foreground service with a visible notification is
 * running; this one holds location + media playback while the walk is on. The engine and the GPS
 * listener themselves stay where they are (Core, Walk) - the service only keeps the process awake.
 */
class WalkService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    /* A media session makes the walk a player the system knows: its play / pause shows on the lock
       screen and in the shade even collapsed (a plain silent notification hides its buttons until
       expanded, and may not show on the lock screen at all), and headphone buttons reach it too. */
    private var session: MediaSession? = null
    private fun toggle() { sendBroadcast(Intent(ACTION_TOGGLE).setPackage(packageName)) }
    override fun onDestroy() { session?.release(); session = null; super.onDestroy() }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Walk", NotificationManager.IMPORTANCE_LOW).apply {
            description = "Shown while Fieldscape plays your walk"
        })
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        /* Screen-off walking (Kerem, 2026-09-27): the notification's Pause / Play is the Sound / Stop
           button, so a walk is stopped and started from the lock screen without unlocking */
        val playing = intent?.getBooleanExtra(EXTRA_PLAYING, true) ?: true
        val ms = session ?: MediaSession(this, "Fieldscape").also {
            it.setCallback(object : MediaSession.Callback() {
                override fun onPlay() { toggle() }
                override fun onPause() { toggle() }
            })
            it.isActive = true
            session = it
        }
        ms.setPlaybackState(PlaybackState.Builder()
            .setActions(PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or PlaybackState.ACTION_PLAY_PAUSE)
            .setState(if (playing) PlaybackState.STATE_PLAYING else PlaybackState.STATE_PAUSED, PlaybackState.PLAYBACK_POSITION_UNKNOWN, 1f)
            .build())
        val toggleIntent = PendingIntent.getBroadcast(this, 1, Intent(ACTION_TOGGLE).setPackage(packageName), PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CHANNEL)
            .setContentTitle("Fieldscape")
            .setContentText(if (playing) "Playing your walk" else "Your walk is paused")
            .setSmallIcon(if (playing) android.R.drawable.ic_media_play else android.R.drawable.ic_media_pause)
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, if (playing) "Pause" else "Play", toggleIntent).build())
            .setStyle(Notification.MediaStyle().setMediaSession(ms.sessionToken).setShowActionsInCompactView(0))
            .setOngoing(true)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .build()
        if (Build.VERSION.SDK_INT >= 29)
            startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION or ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        else startForeground(1, n)
        return START_NOT_STICKY
    }

    companion object {
        private const val CHANNEL = "walk"
        const val ACTION_TOGGLE = "net.keremaltaylar.fieldscape.TOGGLE"
        private const val EXTRA_PLAYING = "playing"
        /** The notification follows the Sound / Stop button. */
        fun update(c: Context, playing: Boolean) { runCatching { c.startForegroundService(Intent(c, WalkService::class.java).putExtra(EXTRA_PLAYING, playing)) } }
        /** Call once location is granted, from the foreground (Android refuses a location service otherwise). */
        fun start(c: Context) { runCatching { c.startForegroundService(Intent(c, WalkService::class.java)) } }
        fun stop(c: Context) { c.stopService(Intent(c, WalkService::class.java)) }
    }
}
