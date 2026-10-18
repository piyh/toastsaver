#!/bin/sh
# Builds dist/Toastsaver.app: a self-contained macOS demo that opens the picker.
# SDL is copied into the app, and settings go to a temporary folder, so nothing
# is installed and nothing is left in your home folder.
# Needs Homebrew SDL2 (brew install sdl2). Usage: sh packaging/macos/build-app.sh
set -eu
cd "$(dirname "$0")/../.."
make
APP=dist/Toastsaver.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

# Note: macOS disks are usually case-insensitive, so the program can't be called
# "toastsaver" next to the "Toastsaver" launcher - they would be the same file.
cp bin/toastsaver "$APP/Contents/MacOS/toastsaver-bin"
SDL2=$(otool -L bin/toastsaver | awk '/libSDL2/ {print $1}')
cp "$SDL2" "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib"
# sdl2-compat looks for SDL3 next to itself first (@loader_path/libSDL3.dylib).
cp "$(brew --prefix sdl3)/lib/libSDL3.0.dylib" "$APP/Contents/Frameworks/libSDL3.dylib"
chmod u+w "$APP/Contents/Frameworks/"*.dylib
install_name_tool -id @rpath/libSDL2-2.0.0.dylib "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib"
install_name_tool -change "$SDL2" @executable_path/../Frameworks/libSDL2-2.0.0.dylib "$APP/Contents/MacOS/toastsaver-bin"

# Launcher: keep the demo's settings out of the home folder.
cat > "$APP/Contents/MacOS/Toastsaver" <<'SH'
#!/bin/sh
export XDG_CONFIG_HOME="${TMPDIR:-/tmp}/toastsaver-demo"
mkdir -p "$XDG_CONFIG_HOME"
# The demo has no Settings window, so show the clock (off by default on the Pi).
"$(dirname "$0")/toastsaver-bin" --set CLOCK=1 >/dev/null
exec "$(dirname "$0")/toastsaver-bin" --picker
SH
chmod 755 "$APP/Contents/MacOS/Toastsaver"

# App icon from the program's own icon.
ICONSET=$(mktemp -d)/Toastsaver.iconset
mkdir -p "$ICONSET"
bin/toastsaver --write-icon "$ICONSET/base.png"
for s in 16 32 128 256 512; do
    sips -z $s $s "$ICONSET/base.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$ICONSET/base.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
rm "$ICONSET/base.png"
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/Toastsaver.icns"

VERSION=$(bin/toastsaver --version)
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Toastsaver</string>
  <key>CFBundleDisplayName</key><string>Toastsaver</string>
  <key>CFBundleIdentifier</key><string>io.github.piyh.toastsaver</string>
  <key>CFBundleExecutable</key><string>Toastsaver</string>
  <key>CFBundleIconFile</key><string>Toastsaver</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Everything was modified after linking, so re-sign (ad hoc) inside out.
codesign --force --sign - "$APP/Contents/Frameworks/libSDL3.dylib" "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib" "$APP/Contents/MacOS/toastsaver-bin"
codesign --force --sign - "$APP"
(cd dist && rm -f Toastsaver-mac.zip && ditto -c -k --keepParent Toastsaver.app Toastsaver-mac.zip)
echo "Built $APP ($(du -sh "$APP" | cut -f1)) and dist/Toastsaver-mac.zip"
