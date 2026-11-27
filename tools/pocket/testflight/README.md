# Kaleidobox Pocket on TestFlight

What's already set in the app (tools/pocket/CMakeLists.txt):

| | |
|---|---|
| Bundle ID | `com.colinblackshear.kaleidobox.pocket` |
| Name on the phone | Kaleidobox Pocket |
| Version | 0.2.0 (`POCKET_VERSION`) |
| Build number | the TestFlight workflow's run number; 1 for builds made on the Mac |
| Devices | iPhone (runs on iPad in iPhone mode), portrait, iOS 16 or later |
| Icon | `icon/pocket-icon.png`, full-bleed, no transparency (`tools/rig/icon/make_icon.py --phone`) |
| Microphone text | "Kaleidobox Pocket records sounds from your microphone." |
| Local network text | for Link to Mac over Wi-Fi (Bonjour `_kaleidobox._tcp`) |
| Export compliance | `ITSAppUsesNonExemptEncryption = false` (no encryption of its own), so no question per upload |
| Signing | automatic, with the team in the `APPLE_TEAM_ID` secret |

## One-time setup

1. **App Store Connect > Apps > + > New App**: iOS, name "Kaleidobox Pocket", language English (U.S.),
   bundle ID `com.colinblackshear.kaleidobox.pocket`, SKU `kaleidobox-pocket`, Full Access.
   Creating the record doesn't publish anything.
2. **Users and Access > Integrations > App Store Connect API > +**: name "GitHub TestFlight", access
   **Admin** (needed so Xcode can make the distribution certificate in the cloud). Download the .p8
   file (Apple only lets you download it once). Note the Key ID and the Issuer ID.
3. **developer.apple.com > Account > Membership details**: note the Team ID.
4. **GitHub > crate > Settings > Secrets and variables > Actions**: add `APPLE_TEAM_ID`, `ASC_KEY_ID`,
   `ASC_ISSUER_ID`, and `ASC_KEY_P8` (the whole text of the .p8 file).

## Each build

GitHub > Actions > **TestFlight** > Run workflow (on `main`). It builds Release, signs, and uploads.
The build shows up in App Store Connect > Kaleidobox Pocket > TestFlight after Apple processes it
(usually 5 to 30 minutes). **Uploading doesn't invite anyone.** Turn "upload" off to only make the
.ipa and keep it as a download on the run.

## Sending it to friends (only when you're ready)

- **Internal testers** (people on your App Store Connect team, up to 100): no Apple review.
- **External testers** (anyone with an email, or a public link, up to 10,000): the first build of each
  version goes through a short Beta App Review (usually under a day). Uses the "What to Test" and
  beta description in `TESTER_NOTES.md`.
