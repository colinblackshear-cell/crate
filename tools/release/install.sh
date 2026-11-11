#!/bin/bash
# Installs Kaleidobox on a Mac: the app, the Audio Unit (for Logic) and the VST3.
#
# The easy way (paste into Terminal and press Return):
#   /bin/bash -c "$(curl -fsSL https://github.com/colinblackshear-cell/crate/releases/latest/download/install.sh)"
#
# Downloading with curl means macOS doesn't mark the files as "from the internet", so there are
# no "unidentified developer" warnings. It can also be run from inside the downloaded zip
# (bash install.sh), and then it installs the copies sitting next to it.

set -euo pipefail

NAME="Kaleidobox"
OLD_NAME="Crate Rig"   # its earlier name; same plugin codes, so the old copies must go
ZIP_URL="${KALEIDOBOX_ZIP_URL:-https://github.com/colinblackshear-cell/crate/releases/latest/download/Kaleidobox-mac.zip}"

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }

if [ "$(uname)" != "Darwin" ]; then
    echo "Kaleidobox is for Macs. This computer isn't one." >&2
    exit 1
fi

# Where the files to install are: next to this script, or downloaded fresh.
HERE=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
    HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi
if [ -n "$HERE" ] && [ -d "$HERE/$NAME.app" ]; then
    SRC="$HERE"
else
    say "Downloading $NAME..."
    TMP="$(mktemp -d)"
    trap 'rm -rf "$TMP"' EXIT
    curl -fL --progress-bar "$ZIP_URL" -o "$TMP/kaleidobox.zip"
    ditto -x -k "$TMP/kaleidobox.zip" "$TMP"
    SRC="$(dirname "$(find "$TMP" -maxdepth 3 -name "$NAME.app" -type d | head -n 1)")"
    if [ ! -d "$SRC/$NAME.app" ]; then
        echo "The download didn't have $NAME in it. Ask Colin for a fresh link." >&2
        exit 1
    fi
fi

# Quit it if it's running, so the copy goes in cleanly.
osascript -e "quit app \"$NAME\"" >/dev/null 2>&1 || true

# The app goes in Applications (or your own Applications folder if that one is locked).
APPS="/Applications"
if [ ! -w "$APPS" ]; then
    APPS="$HOME/Applications"
    mkdir -p "$APPS"
fi
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$COMPONENTS" "$VST3"

say "Installing..."
for dir in "$APPS" "$HOME/Applications"; do
    rm -rf "$dir/$OLD_NAME.app"
done
rm -rf "$COMPONENTS/$OLD_NAME.component" "$VST3/$OLD_NAME.vst3"

install_one() {   # install_one <thing> <folder>
    rm -rf "$2/$(basename "$1")"
    ditto "$1" "$2/$(basename "$1")"
    xattr -cr "$2/$(basename "$1")" 2>/dev/null || true
    echo "  $(basename "$1")  ->  $2"
}
install_one "$SRC/$NAME.app" "$APPS"
[ -d "$SRC/$NAME.component" ] && install_one "$SRC/$NAME.component" "$COMPONENTS"
[ -d "$SRC/$NAME.vst3" ] && install_one "$SRC/$NAME.vst3" "$VST3"

# Make Logic notice the new Audio Unit without a restart of the Mac.
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

say "Done! $NAME is installed."
cat <<MSG
  - The app is in $APPS. Open it from there or from Launchpad.
  - In Logic: make a Software Instrument track and pick
    AU Instruments > Colin Blackshear > $NAME.
    (If Logic was open, quit and reopen it first.)
  - The first time it uses your mic, the Mac asks. Click Allow.
MSG

open -R "$APPS/$NAME.app" >/dev/null 2>&1 || true
