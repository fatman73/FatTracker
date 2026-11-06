#!/bin/sh
# FatTracker for macOS: Release build, Developer ID signature (hardened runtime), .dmg, and - with a notarytool
# keychain profile - notarization and stapling of the app and the .dmg.
#
# usage: tools/package_mac.sh [notary-profile]
#   without a profile: signed app + signed .dmg (Gatekeeper on another Mac asks once: right click > Open)
#   with a profile:    also notarized and stapled ("Notarized Developer ID")
# The profile already in the keychain is "FatSlicer" (made for FatSlicer, same Developer ID team):
#   tools/package_mac.sh FatSlicer
# A new one is made once by the user in Terminal (the password never goes into the repo or a chat):
#   xcrun notarytool store-credentials <profile> --apple-id <Apple ID> --team-id F4CUVQSJ8Y
#
# Environment: FT_SIGN_IDENTITY (default: the first "Developer ID Application" in the keychain), FT_BUILD (build-mac).
set -e
cd "$(dirname "$0")/.."
profile=$1
build=${FT_BUILD:-build-mac}
identity=${FT_SIGN_IDENTITY:-$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' | head -1)}
[ -n "$identity" ] || { echo "no Developer ID Application identity in the keychain"; exit 1; }

cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" --target FatTracker -j "$(sysctl -n hw.ncpu)"
app="$build/FatTracker.app"
version=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$app/Contents/Info.plist")
dist="$build/dist"
dmg="$dist/FatTracker-$version.dmg"
rm -rf "$dist"
mkdir -p "$dist"

echo "== signing with: $identity"
codesign --force --options runtime --timestamp --entitlements src/mac/FatTracker.entitlements --sign "$identity" "$app"
codesign --verify --deep --strict --verbose=2 "$app"

if [ -n "$profile" ]; then
    echo "== notarizing the app"
    ditto -c -k --keepParent "$app" "$dist/FatTracker.zip"
    xcrun notarytool submit "$dist/FatTracker.zip" --keychain-profile "$profile" --wait
    xcrun stapler staple "$app"
    rm "$dist/FatTracker.zip"
fi

echo "== disk image"
stage="$dist/stage"
mkdir -p "$stage"
ditto "$app" "$stage/FatTracker.app"
ln -s /Applications "$stage/Applications"
cp LICENSE "$stage/LICENSE.txt"
cp CREDITS.txt "$stage/CREDITS.txt"
hdiutil create -volname "FatTracker $version" -srcfolder "$stage" -ov -format UDZO "$dmg" >/dev/null
rm -rf "$stage"
codesign --force --timestamp --sign "$identity" "$dmg"

if [ -n "$profile" ]; then
    echo "== notarizing the disk image"
    xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait
    xcrun stapler staple "$dmg"
fi

echo "== checks"
spctl -a -vv "$app" 2>&1 || true
spctl -a -vv -t open --context context:primary-signature "$dmg" 2>&1 || true
echo "$dmg"
