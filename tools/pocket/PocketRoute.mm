#include "PocketRoute.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <string>

// The phone: asks the audio session where the sound goes, and listens for it changing. Written to build
// with or without ARC (it never retains or releases by hand).
namespace pocket
{
namespace
{
int demoRoute = -1;
std::function<void()>* routeChanged = nullptr;   // lives for the app's life
id routeObserver = nil;                           // the notification centre holds it until removed
std::string silentMic;                            // UID of a headset mic heard giving only silence

bool isWiredMic (NSString* t)
{
    return [t isEqualToString: AVAudioSessionPortHeadsetMic]
        || [t isEqualToString: AVAudioSessionPortUSBAudio]
        || [t isEqualToString: AVAudioSessionPortLineIn];
}

AVAudioSessionPortDescription* currentInput()
{
    NSArray<AVAudioSessionPortDescription*>* ins = [AVAudioSession sharedInstance].currentRoute.inputs;
    return ins.count > 0 ? ins[0] : nil;
}

bool isSilentMic (AVAudioSessionPortDescription* port)
{
    return ! silentMic.empty() && port.UID != nil && silentMic == port.UID.UTF8String;
}

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

bool pickMic()
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    if (! [session.category isEqualToString: AVAudioSessionCategoryPlayAndRecord])
        return false;   // no mic allowed: the device only plays

    AVAudioSessionPortDescription* builtIn = nil;
    AVAudioSessionPortDescription* wired = nil;
    bool silentStillThere = false;
    for (AVAudioSessionPortDescription* port in session.availableInputs)
    {
        if (isSilentMic (port))
        {
            silentStillThere = true;
            continue;
        }
        if ([port.portType isEqualToString: AVAudioSessionPortBuiltInMic])
            builtIn = port;
        else if (wired == nil && isWiredMic (port.portType))
            wired = port;
    }
    if (! silentStillThere)
        silentMic.clear();   // unplugged: the next time it's plugged in it gets another chance

    AVAudioSessionPortDescription* want = wired != nil ? wired : builtIn;
    if (want == nil)
        return false;
    AVAudioSessionPortDescription* now = currentInput();
    AVAudioSessionPortDescription* preferred = session.preferredInput;
    if (now != nil && [now.UID isEqualToString: want.UID]
        && preferred != nil && [preferred.UID isEqualToString: want.UID])
        return false;   // already there: setting it again would only send another route change
    NSError* error = nil;
    return [session setPreferredInput: want error: &error] && error == nil;
}

bool avoidSilentMic()
{
    AVAudioSessionPortDescription* now = currentInput();
    if (now == nil || [now.portType isEqualToString: AVAudioSessionPortBuiltInMic])
        return false;
    silentMic = now.UID != nil ? now.UID.UTF8String : "";
    pickMic();
    AVAudioSessionPortDescription* after = currentInput();
    return after == nil || ! [after.UID isEqualToString: now.UID];
}

bool micIsBuiltIn()
{
    AVAudioSessionPortDescription* now = currentInput();
    return now == nil || [now.portType isEqualToString: AVAudioSessionPortBuiltInMic];
}

const char* micName()
{
    AVAudioSessionPortDescription* now = currentInput();
    if (now == nil)
        return "NO MIC";
    NSString* t = now.portType;
    if ([t isEqualToString: AVAudioSessionPortBuiltInMic])
        return "IPHONE MIC";
    if ([t isEqualToString: AVAudioSessionPortHeadsetMic])
        return "HEADSET MIC";
    if ([t isEqualToString: AVAudioSessionPortBluetoothHFP])
        return "BLUETOOTH MIC";
    return "USB INPUT";
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
