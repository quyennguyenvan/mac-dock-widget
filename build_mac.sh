#!/bin/bash
# ============================================
#  SysMonitor - macOS Build Script
# ============================================
# Creates SysMonitor.app bundle ready to install.
# Usage: chmod +x build_mac.sh && ./build_mac.sh

set -e

echo "============================================"
echo " SysMonitor - macOS Build Script"
echo "============================================"
echo

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$SCRIPT_DIR/src/mac.main.cpp"
APP_NAME="SysMonitor"
APP_BUNDLE="$SCRIPT_DIR/$APP_NAME.app"
CONTENTS="$APP_BUNDLE/Contents"
MACOS_DIR="$CONTENTS/MacOS"
BINARY="$MACOS_DIR/$APP_NAME"

# Check for Xcode command-line tools
if ! command -v clang++ &> /dev/null; then
    echo "[ERROR] clang++ not found. Install Xcode Command Line Tools:"
    echo "        xcode-select --install"
    exit 1
fi

echo "[*] Compiling $APP_NAME..."
echo

# Clean previous build
rm -rf "$APP_BUNDLE"
mkdir -p "$MACOS_DIR"

# Compile as Objective-C++ (the file uses Cocoa / @interface syntax)
clang++ -x objective-c++ -std=c++17 -O2 \
    -framework Cocoa \
    -framework IOKit \
    -fobjc-arc \
    -Wno-deprecated-declarations \
    "$SRC" -o "$BINARY"

if [ $? -ne 0 ]; then
    echo
    echo "[FAIL] Build failed!"
    exit 1
fi

# Create Info.plist
cat > "$CONTENTS/Info.plist" << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>SysMonitor</string>
    <key>CFBundleDisplayName</key>
    <string>SysMonitor</string>
    <key>CFBundleIdentifier</key>
    <string>com.sysmonitor.widget</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleExecutable</key>
    <string>SysMonitor</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSUIElement</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSAppTransportSecurity</key>
    <dict>
        <key>NSAllowsArbitraryLoads</key>
        <true/>
    </dict>
</dict>
</plist>
PLIST

echo
echo "[OK] Build successful!"
echo "     Output: $APP_BUNDLE"
echo
echo "To install:"
echo "  cp -r $APP_BUNDLE /Applications/"
echo
echo "To run:"
echo "  open $APP_BUNDLE"
echo
