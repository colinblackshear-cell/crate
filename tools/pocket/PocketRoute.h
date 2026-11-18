#pragma once

namespace pocket
{
// True when the sound goes out through headphones, earbuds or another private output (wired, Bluetooth,
// USB). False when it goes to the phone's own speaker. On a computer it says true, so the preview doesn't
// nag; set POCKET_NO_HEADPHONES=1 to see the warning. Message thread.
bool headphonesConnected();
}
