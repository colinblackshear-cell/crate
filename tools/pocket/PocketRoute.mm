#include "PocketRoute.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

// The phone: asks the audio session where the sound goes, and listens for it changing. Written to build
// with or without ARC (it never retains or releases by hand).
namespace pocket
{
namespace
{
int demoRoute = -1;
std::function<void()>* routeChanged = nullptr;   // lives for the app's life
id routeObserver = nil;                           // the notification centre holds it until removed

Route routeOf (NSString* t)
{
    if ([t isEqualToString: AVAudioSessionPortBluetoothA2DP]
        || [t isEqualToString: AVAudioSessionPortBluetoothLE]
        || [t isEqualToString: AVAudioSessionPortBluetoothHFP])
        return Route::bluetooth;
    if ([t isEqualToString: AVAudioSessionPortHeadphones]
        || [t isEqualToString: AVAudioSessionPortUSBAudio]
        || [t isEqualToString: AVAudioSessionPortLineOut])
        return Route::wired;
    return Route::none;   // the built-in speaker or receiver, AirPlay, car audio, HDMI: a room can hear it
}
}

Route currentRoute()
{
    if (demoRoute >= 0)
        return (Route) (demoRoute < 0 ? 0 : demoRoute > 2 ? 2 : demoRoute);
    Route best = Route::none;
    AVAudioSessionRouteDescription* route = [AVAudioSession sharedInstance].currentRoute;
    for (AVAudioSessionPortDescription* port in route.outputs)
    {
        const Route r = routeOf (port.portType);
        if (r == Route::wired)
            return Route::wired;
        if (r == Route::bluetooth)
            best = Route::bluetooth;
    }
    return best;
}

void setDemoRoute (int route) { demoRoute = route; }

void watchRoute (std::function<void()> changed)
{
    if (routeChanged == nullptr)
        routeChanged = new std::function<void()>();
    *routeChanged = std::move (changed);
    if (routeObserver != nil)
        return;
    // queue: the main queue, so the callback runs on the message thread
    routeObserver = [[NSNotificationCenter defaultCenter] addObserverForName: AVAudioSessionRouteChangeNotification
                                                                      object: nil
                                                                       queue: [NSOperationQueue mainQueue]
                                                                  usingBlock: ^(NSNotification* note)
                                                                  {
                                                                      (void) note;
                                                                      if (routeChanged != nullptr && *routeChanged)
                                                                          (*routeChanged)();
                                                                  }];
}

int routeLatencySamples (double sampleRate)
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    const double seconds = session.inputLatency + session.outputLatency;
    if (seconds <= 0.0 || sampleRate <= 0.0)
        return -1;
    return (int) (seconds * sampleRate + 0.5);
}

const char* routeName (Route r)
{
    switch (r)
    {
        case Route::wired: return "WIRED HEADPHONES";
        case Route::bluetooth: return "AIRPODS / BLUETOOTH";
        default: return "NO HEADPHONES";
    }
}
}
