#!/usr/bin/env bash
#
# Builds a debug APK of LeviLauncher on a normal x86_64 host
# (Linux / macOS / WSL). Cannot run on arm64 Android/Termux: the Android
# SDK build-tools (aapt2) and the NDK host compilers are linux-x86_64 only.
#
# Requirements:
#   - JDK 17+ (JDK 21 recommended)
#   - Android SDK with: platform-tools, platforms;android-36,
#     build-tools;35.0.0, ndk;28.2.13676358
#   - ANDROID_HOME or ANDROID_SDK_ROOT pointing at the SDK
#
# Usage:
#   ./scripts/build_debug_apk.sh            # assembleDebug
#   ./scripts/build_debug_apk.sh assembleRelease
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TASK="${1:-assembleDebug}"

if ! command -v java >/dev/null 2>&1; then
  echo "error: java not found (install JDK 17+)" >&2
  exit 1
fi

SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [ -z "$SDK" ] && [ -f local.properties ]; then
  SDK="$(sed -n 's/^sdk.dir=//p' local.properties | head -1)"
fi
if [ -z "$SDK" ] || [ ! -d "$SDK" ]; then
  echo "error: Android SDK not found. Set ANDROID_HOME or sdk.dir in local.properties." >&2
  exit 1
fi

if [ ! -f local.properties ]; then
  printf 'sdk.dir=%s\ncurseforge.api_key=\n' "$SDK" > local.properties
  echo "wrote local.properties (sdk.dir=$SDK)"
fi

# Firebase plugin needs a google-services.json; a CI placeholder is enough for
# a local build. Replace with the real one if you need Crashlytics/Analytics.
if [ ! -f app/google-services.json ]; then
  cat > app/google-services.json <<'JSON'
{
  "project_info": {
    "project_number": "000000000000",
    "project_id": "ci-placeholder",
    "storage_bucket": "ci-placeholder.appspot.com"
  },
  "client": [
    {
      "client_info": {
        "mobilesdk_app_id": "1:000000000000:android:0000000000000000000000",
        "android_client_info": { "package_name": "org.levimc.launcher" }
      },
      "oauth_client": [],
      "api_key": [ { "current_key": "AIzaSyCIOnlyPlaceholderKey" } ],
      "services": { "appinvite_service": { "other_platform_oauth_client": [] } }
    }
  ],
  "configuration_version": "1"
}
JSON
  echo "wrote app/google-services.json (placeholder)"
fi

chmod +x ./gradlew
echo "building :app:$TASK (SDK=$SDK)"
./gradlew ":app:${TASK}" -PdisableCrashlyticsUpload --stacktrace

echo
echo "Output APKs:"
find app/build/outputs/apk -name '*.apk' -print 2>/dev/null || true
