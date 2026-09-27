package org.ft891remote.client;

import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * Qt's activity, extended for push-to-talk on the volume-down key.
 *
 * Qt deliberately lets the volume keys through to the system: they never
 * reach the C++ side, and no Qt event filter sees them. They have to be caught
 * here, before the framework changes the volume.
 */
public class Ft891RemoteActivity extends org.qtproject.qt.android.bindings.QtActivity {

    // Implemented in androidservice.cpp.
    public static native boolean volumePttEnabled();
    public static native void volumePtt(boolean pressed);

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // The bars come back after a swipe or a return from the background:
        // hide them again, or full screen lasts only until the first gesture.
        if (hasFocus) hideSystemBars();
    }

    /** Hides the status and navigation bars, transiently. */
    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.statusBars()
                                | WindowInsets.Type.navigationBars());
            }
        } else {
            // Before Android 11: the old visibility flags.
            final int flags = 0x00001000 | 0x00000004 | 0x00000002
                            | 0x00000100 | 0x00000200 | 0x00000400;
            getWindow().getDecorView().setSystemUiVisibility(flags);
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_VOLUME_DOWN) {
            boolean wanted;
            try {
                wanted = volumePttEnabled();
            } catch (UnsatisfiedLinkError e) {
                // The native library is not loaded yet: let the system do
                // what it usually does.
                return super.dispatchKeyEvent(event);
            }

            if (wanted) {
                // Repeats are ignored but consumed: otherwise the volume would
                // move during a long transmission.
                if (event.getRepeatCount() == 0) {
                    if (event.getAction() == KeyEvent.ACTION_DOWN)    volumePtt(true);
                    else if (event.getAction() == KeyEvent.ACTION_UP) volumePtt(false);
                }
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }
}
