package org.ft891remote.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

/**
 * Foreground service.
 *
 * Without it Android suspends the application as soon as the screen goes off
 * and the audio stops mid-contact. The service also holds two locks:
 *   - a high-performance WifiLock, or Wi-Fi power saving leaves holes in the
 *     stream;
 *   - a partial WakeLock, so that the CPU keeps processing audio.
 */
public class Ft891RemoteService extends Service {

    private static final String CHANNEL_ID = "ft891remote_link";
    private static final int NOTIFICATION_ID = 1;

    private WifiManager.WifiLock wifiLock;
    private PowerManager.WakeLock wakeLock;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    // WIFI_MODE_FULL_HIGH_PERF is deprecated since Android 10 but remains the
    // only way to keep Wi-Fi power saving from breaking up the stream.
    @SuppressWarnings("deprecation")
    @Override
    public void onCreate() {
        super.onCreate();
        createChannel();

        WifiManager wifi = (WifiManager) getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            wifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                                           "FT891Remote:wifi");
            wifiLock.setReferenceCounted(false);
            wifiLock.acquire();
        }

        PowerManager power = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (power != null) {
            wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "FT891Remote:audio");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire();
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Notification notification = buildNotification();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            // Android 14 requires the service type to be declared.
            startForeground(NOTIFICATION_ID, notification,
                            ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
        // Restarted by the system if it is killed for lack of memory.
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        super.onDestroy();
    }

    private void createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "FT891Remote", NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Link to the remote station");
        channel.setShowBadge(false);
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) manager.createNotificationChannel(channel);
    }

    // Notification.Builder(Context) is only used before Android 8, where the
    // constructor with a channel does not exist.
    @SuppressWarnings("deprecation")
    private Notification buildNotification() {
        Intent open = new Intent(this, Ft891RemoteActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);

        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent pending = PendingIntent.getActivity(this, 0, open, flags);

        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        return builder
                .setContentTitle("FT891Remote")
                .setContentText("Station link active")
                .setSmallIcon(android.R.drawable.ic_lock_silent_mode_off)
                .setContentIntent(pending)
                .setOngoing(true)
                .build();
    }
}
