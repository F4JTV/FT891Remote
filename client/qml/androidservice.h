// Start and stop of the Android foreground service. Without it, audio stops
// as soon as the screen goes off.
#pragma once

namespace rr {
void startAndroidService();
void stopAndroidService();

// Height of the status bar in physical pixels, 0 outside Android. From
// Android 15 the window extends under the system bar: without this margin the
// header of the application would slide under it.
int androidStatusBarHeight();

// The Java activity calls into this from Android's UI thread; the target is
// reached through a queued connection.
class VolumePttSink {
public:
    virtual ~VolumePttSink() = default;
    virtual bool volumePttWanted() const = 0;
    virtual void volumePttChanged(bool pressed) = 0;
};
void setVolumePttSink(VolumePttSink *sink);
}
