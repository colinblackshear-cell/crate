#include "PocketRoute.h"

#import <AVFoundation/AVFoundation.h>

namespace pocket
{
bool headphonesConnected()
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    for (AVAudioSessionPortDescription* port in session.currentRoute.outputs)
    {
        NSString* t = port.portType;
        if ([t isEqualToString: AVAudioSessionPortHeadphones]
            || [t isEqualToString: AVAudioSessionPortBluetoothA2DP]
            || [t isEqualToString: AVAudioSessionPortBluetoothLE]
            || [t isEqualToString: AVAudioSessionPortBluetoothHFP]
            || [t isEqualToString: AVAudioSessionPortUSBAudio]
            || [t isEqualToString: AVAudioSessionPortLineOut]
            || [t isEqualToString: AVAudioSessionPortAirPlay])
            return true;
    }
    return false;
}
}
