#!/usr/bin/env bash
# Two-pass build for the Apollon GUI framework.
#
#   1) compile the native library  -> libReverseSigma.so
#   2) copy it to assets/assets/icu/demo.dat   (Platform1.loadLib expects it there)
#   3) build the APK
#
# Run from the project root (this file's directory).
set -euo pipefail
cd "$(dirname "$0")"

chmod +x gradlew

ABI="${ABI:-arm64-v8a}"

echo "[1/3] Building native library ($ABI)..."
./gradlew :app:externalNativeBuildDebug --no-daemon

SO="$(find app/build app/.cxx -type f -name libReverseSigma.so -path "*${ABI}*" 2>/dev/null | head -n1 || true)"
if [ -z "$SO" ]; then
  SO="$(find app/build -type f -name libReverseSigma.so 2>/dev/null | head -n1 || true)"
fi
if [ -z "$SO" ]; then
  echo "ERROR: libReverseSigma.so not found after native build" >&2
  exit 1
fi
echo "      using: $SO"

echo "[2/3] Copying to assets/assets/icu/demo.dat..."
mkdir -p app/src/main/assets/assets/icu
cp -f "$SO" app/src/main/assets/assets/icu/demo.dat

echo "[3/3] Building APK..."
./gradlew :app:assembleDebug --no-daemon

APK="app/build/outputs/apk/debug/app-debug.apk"
echo "DONE: $APK"
ls -lh "$APK"
