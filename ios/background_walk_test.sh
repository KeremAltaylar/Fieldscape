#!/bin/sh
# Screen-off walking on the iOS simulator (Kerem, 2026-09-27: "off screen playing I want"): the app is
# sent to the background while it plays, the simulated GPS moves, and the walk log (Documents/walk.log,
# one line per step) must keep growing with the new positions. Run on the Mac from ~/fs after
# `sh ios/build.sh`:   sh ios/background_walk_test.sh
set -e
APP=build/ios/dd/Build/Products/Debug-iphonesimulator/Fieldscape.app
ID=net.keremaltaylar.fieldscape
DEV="Fieldscape iPhone"
xcrun simctl boot "$DEV" 2>/dev/null || true
xcrun simctl install "$DEV" "$APP"
xcrun simctl privacy "$DEV" grant location "$ID"
xcrun simctl location "$DEV" set 41.00771,29.038879          # on the Koşuyolu route
DATA=$(xcrun simctl get_app_container "$DEV" "$ID" data)
rm -f "$DATA/Documents/walk.log"
xcrun simctl launch "$DEV" "$ID" >/dev/null
sleep 12                                                       # features, recordings, sound
FG=$(grep -c "41.0077" "$DATA/Documents/walk.log" 2>/dev/null || echo 0)
xcrun simctl launch "$DEV" com.apple.Preferences >/dev/null   # Fieldscape goes to the background
sleep 3
for lat in 41.00830 41.00890 41.00950 41.01010; do            # walking north, screen "off"
  xcrun simctl location "$DEV" set "$lat,29.03950"; sleep 3
done
BG=$(grep -c " 29.03950" "$DATA/Documents/walk.log" 2>/dev/null || echo 0)
echo "steps in the foreground: $FG · steps while in the background: $BG"
xcrun simctl terminate "$DEV" "$ID" 2>/dev/null || true
xcrun simctl terminate "$DEV" com.apple.Preferences 2>/dev/null || true
if [ "$FG" -gt 0 ] && [ "$BG" -ge 3 ]; then echo "PASS background walk"; else echo "FAIL background walk"; exit 1; fi
