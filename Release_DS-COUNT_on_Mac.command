#!/bin/bash
# Signs DS-COUNT with your Developer ID, has Apple notarize it, and makes the files to distribute:
#   dist/DS-COUNT-<version>-macOS.pkg   installer (VST3, AU, Standalone app)
#   dist/DS-COUNT-<version>-macOS.dmg   disk image with the installer and a read-me
# Double-click to run. The first time it asks for your Apple ID details for notarization and keeps them
# in the macOS Keychain (profile "DSCOUNT_NOTARY"); nothing is stored in this folder.
cd "$(dirname "$0")" || exit 1
ROOT="$PWD"
exec > >(tee "$ROOT/release_log.txt") 2>&1
step() { printf "\n=== %s ===\n" "$*"; }
finish() { echo; echo "DSCOUNT_RELEASE_FINISHED status=$1"; echo "You can close this window."; exit "$1"; }
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer

VERSION=$(sed -n 's/^project(DSCount VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="DS-COUNT"
PROFILE="DSCOUNT_NOTARY"
B="$HOME/Library/Caches/DS-COUNT-build"
A="$B/DSCount_artefacts/Release"
W="$HOME/Library/Caches/DS-COUNT-release"
echo "$NAME $VERSION"

step "Build"
if [ ! -d "$A/VST3/$NAME.vst3" ] || [ -n "$(find Source CMakeLists.txt -newer "$A/VST3/$NAME.vst3/Contents/MacOS/$NAME" -print -quit)" ]; then
  echo "Building first (sources changed since the last build)"
  bash "$ROOT/Build_DS-COUNT_on_Mac.command" > /dev/null 2>&1
  BS=$?
  grep -E "dsp_tests=|DSCOUNT_BUILD_FINISHED" "$ROOT/build_log.txt"
  [ "$BS" = 0 ] || { echo "BLOCKER: the build or its tests failed, see build_log.txt"; finish 2; }
else
  echo "Using the current build in $A"
fi

step "Signing identities"
# Pick by SHA-1 fingerprint: the Keychain can hold two certificates with the same name (renewed or
# re-downloaded), and signing by name is then ambiguous. Prefer the one that expires last.
pick() {   # $1 = policy, $2 = name prefix
  best=""; bestEnd=0
  for h in $(security find-identity -v -p "$1" | grep "\"$2" | awk '{print $2}' | sort -u); do
    end=$(security find-certificate -a -Z -p -c "$2" 2>/dev/null | awk -v h="$h" '
      /^SHA-1 hash:/ {cur=$3} /BEGIN CERT/ {if (cur==h) on=1} on {print} /END CERT/ {on=0}' \
      | openssl x509 -noout -enddate 2>/dev/null | head -1 | cut -d= -f2)
    t=$(date -j -f "%b %e %T %Y %Z" "$end" +%s 2>/dev/null || echo 1)
    if [ "$t" -gt "$bestEnd" ]; then best=$h; bestEnd=$t; fi
  done
  echo "$best"
}
APP_ID=$(pick codesigning "Developer ID Application")
INST_ID=$(pick basic "Developer ID Installer")
APP_NAME=$(security find-identity -v -p codesigning | grep "$APP_ID" | sed -n 's/.*"\(.*\)".*/\1/p' | head -1)
INST_NAME=$(security find-identity -v -p basic | grep "$INST_ID" | sed -n 's/.*"\(.*\)".*/\1/p' | head -1)
echo "Application: ${APP_NAME:-MISSING} ${APP_ID}"
echo "Installer:   ${INST_NAME:-MISSING} ${INST_ID}"
echo "Developer ID certificates in the Keychain: $(security find-identity -v -p basic | grep -c 'Developer ID')"
# productbuild signs by name; use the fingerprint only when the name is ambiguous.
INST_SIGN="$INST_NAME"
[ "$(security find-identity -v -p basic | grep -c "\"$INST_NAME\"")" -gt 1 ] && INST_SIGN="$INST_ID"
if [ -z "$APP_ID" ] || [ -z "$INST_ID" ]; then
  echo
  echo "BLOCKER: a Developer ID certificate is missing from the Keychain."
  echo "Open Xcode > Settings > Accounts, select your Apple ID, Manage Certificates..., click + and add"
  echo "'Developer ID Application' and 'Developer ID Installer'. Then run this again."
  finish 3
fi
TEAM=$(echo "$APP_NAME" | sed -n 's/.*(\([A-Z0-9]*\))$/\1/p')

step "Notarization login"
if ! xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null 2>&1; then
  echo "First time only: enter your Apple ID e-mail and an app-specific password"
  echo "(make one at https://account.apple.com > Sign-In and Security > App-Specific Passwords)."
  xcrun notarytool store-credentials "$PROFILE" --team-id "$TEAM" || { echo "BLOCKER: could not save the notarization login"; finish 4; }
fi
echo "profile $PROFILE ok (team $TEAM)"

notarize() {   # $1 = file to submit
  echo "Submitting $(basename "$1") to Apple (usually 1 to 10 minutes)..."
  OUT=$(xcrun notarytool submit "$1" --keychain-profile "$PROFILE" --wait 2>&1)
  echo "$OUT" | grep -E "id:|status:" | tail -3
  if ! echo "$OUT" | grep -q "status: Accepted"; then
    ID=$(echo "$OUT" | sed -n 's/^ *id: \(.*\)/\1/p' | head -1)
    [ -n "$ID" ] && xcrun notarytool log "$ID" --keychain-profile "$PROFILE"
    return 1
  fi
}

step "Sign the plug-ins and the app (hardened runtime, secure timestamp)"
rm -rf "$W" && mkdir -p "$W/bundles" "$W/pkg" "$W/dmg"
cp -R "$A/VST3/$NAME.vst3" "$A/AU/$NAME.component" "$A/Standalone/$NAME.app" "$W/bundles/"
xattr -cr "$W/bundles"
cat > "$W/app.entitlements" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>com.apple.security.device.audio-input</key><true/>
</dict></plist>
EOF
for b in "$W/bundles/$NAME.vst3" "$W/bundles/$NAME.component"; do
  codesign --force --options runtime --timestamp --sign "$APP_ID" "$b" || finish 5
done
codesign --force --options runtime --timestamp --entitlements "$W/app.entitlements" --sign "$APP_ID" "$W/bundles/$NAME.app" || finish 5
for b in "$W/bundles/"*; do codesign --verify --strict --deep --verbose=1 "$b" || finish 5; done

step "Notarize and staple the bundles (so copies installed by hand pass Gatekeeper too)"
ditto -c -k --sequesterRsrc "$W/bundles" "$W/bundles.zip"
notarize "$W/bundles.zip" || { echo "BLOCKER: Apple rejected the bundles (log above)"; finish 6; }
for b in "$W/bundles/"*; do xcrun stapler staple -q "$b" || finish 6; done

step "Build the installer"
make_component() {   # $1 bundle, $2 install location, $3 identifier, $4 output
  R="$W/root_$(basename "$4" .pkg)"; mkdir -p "$R"; cp -R "$1" "$R/"
  pkgbuild --analyze --root "$R" "$R.plist" >/dev/null
  n=$(/usr/libexec/PlistBuddy -c "Print" "$R.plist" | grep -c "BundleIsRelocatable")
  for ((i=0; i<n; i++)); do /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$R.plist"; done
  pkgbuild --root "$R" --component-plist "$R.plist" --install-location "$2" --identifier "$3" --version "$VERSION" "$4" >/dev/null || finish 7
}
make_component "$W/bundles/$NAME.vst3" "/Library/Audio/Plug-Ins/VST3" com.yraziel.dscount.vst3 "$W/pkg/vst3.pkg"
make_component "$W/bundles/$NAME.component" "/Library/Audio/Plug-Ins/Components" com.yraziel.dscount.au "$W/pkg/au.pkg"
make_component "$W/bundles/$NAME.app" "/Applications" com.yraziel.dscount.app "$W/pkg/app.pkg"
mkdir -p "$W/res"
cat > "$W/res/welcome.txt" <<EOF
$NAME $VERSION by Yishay Raziel

Mastering dynamics processor: two-channel dynamics with band processing and de-essing, dual release,
look-ahead, Mid/Side, parallel mode, true-peak limiter, loudness meter and dither.

This installs the VST3 and Audio Unit plug-ins for all users, and the standalone app in Applications.
Requires macOS 12 or later (Apple Silicon or Intel).

https://yraziel.com
EOF
cat > "$W/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$NAME $VERSION</title>
  <organization>com.yraziel</organization>
  <welcome file="welcome.txt"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
  <os-version min="12.0"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="app"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plug-in"><pkg-ref id="com.yraziel.dscount.vst3"/></choice>
  <choice id="au" title="Audio Unit plug-in"><pkg-ref id="com.yraziel.dscount.au"/></choice>
  <choice id="app" title="Standalone app"><pkg-ref id="com.yraziel.dscount.app"/></choice>
  <pkg-ref id="com.yraziel.dscount.vst3" version="$VERSION">vst3.pkg</pkg-ref>
  <pkg-ref id="com.yraziel.dscount.au" version="$VERSION">au.pkg</pkg-ref>
  <pkg-ref id="com.yraziel.dscount.app" version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
EOF
PKG="$W/dmg/$NAME-$VERSION-macOS.pkg"
productbuild --distribution "$W/distribution.xml" --resources "$W/res" --package-path "$W/pkg" --sign "$INST_SIGN" --timestamp "$PKG" || finish 7
pkgutil --check-signature "$PKG" | head -4

step "Notarize and staple the installer"
notarize "$PKG" || { echo "BLOCKER: Apple rejected the installer (log above)"; finish 8; }
xcrun stapler staple -q "$PKG" || finish 8

step "Disk image"
cat > "$W/dmg/Read Me.txt" <<EOF
$NAME $VERSION by Yishay Raziel

Install: open "$NAME-$VERSION-macOS.pkg" and follow the steps. Then rescan plug-ins in your DAW.
The plug-ins go to /Library/Audio/Plug-Ins (VST3 and Components), the app to /Applications.

Uninstall: delete $NAME.vst3, $NAME.component and $NAME.app from those folders.

https://yraziel.com
EOF
DMG="$W/$NAME-$VERSION-macOS.dmg"
hdiutil create -volname "$NAME $VERSION" -srcfolder "$W/dmg" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null || finish 9
codesign --force --timestamp --sign "$APP_ID" "$DMG" || finish 9
notarize "$DMG" || { echo "BLOCKER: Apple rejected the disk image (log above)"; finish 9; }
xcrun stapler staple -q "$DMG" || finish 9

step "Gatekeeper check (as a user who downloads it would see it)"
spctl -a -vv -t install "$PKG" 2>&1
spctl -a -vv -t open --context context:primary-signature "$DMG" 2>&1
spctl -a -vv -t exec "$W/bundles/$NAME.app" 2>&1
for b in "$W/bundles/$NAME.vst3" "$W/bundles/$NAME.component"; do xcrun stapler validate "$b"; done

step "Files to distribute"
mkdir -p "$ROOT/dist"
rm -f "$ROOT/dist/$NAME-$VERSION-macOS."*
cp "$PKG" "$DMG" "$ROOT/dist/"
(cd "$ROOT/dist" && shasum -a 256 "$NAME-$VERSION-macOS.pkg" "$NAME-$VERSION-macOS.dmg" | tee "$NAME-$VERSION-macOS.sha256")
ls -l "$ROOT/dist"
finish 0
