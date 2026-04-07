#!/bin/bash

# Test script for VR OpenXR session state fix
# This script builds, installs, and tests the VREmulationActivity that fixes the OpenXR session state issue

set -e

echo "=== Building Cemu Android with VR fix ==="
ANDROID_NDK_HOME=/Users/clancey/Library/Android/sdk/ndk/26.3.11579264 JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-17.jdk/Contents/Home ./gradlew assembleDebug

echo "=== Installing APK to Quest ==="
ANDROID_SERIAL=2G0YC1ZF8908Z8 adb install -r app/build/outputs/apk/debug/app-debug.apk

echo "=== Clearing logs and starting VR Activity ==="
ANDROID_SERIAL=2G0YC1ZF8908Z8 adb logcat -c
ANDROID_SERIAL=2G0YC1ZF8908Z8 adb shell am force-stop info.cemu.cemu.debug
ANDROID_SERIAL=2G0YC1ZF8908Z8 adb shell am start -n info.cemu.cemu.debug/info.cemu.cemu.emulation.VREmulationActivity --es "info.cemu.cemu.debug.LaunchPath" "/data/user/0/info.cemu.cemu.debug/files/games/OOT/code/VESSEL.rpx"

echo "=== Waiting for OpenXR initialization ==="
sleep 15

echo "=== Checking OpenXR session state logs ==="
ANDROID_SERIAL=2G0YC1ZF8908Z8 adb logcat -d | grep "OpenXRManager.*state\|READY\|session.*started\|xrBeginSession\|FOCUSED\|VREmulationActivity" | grep -v "vrshell\|3262" | tail -20

echo ""
echo "=== SUCCESS INDICATORS ==="
echo "Look for these log entries:"
echo "1. 'XR_SESSION_STATE_IDLE -> XR_SESSION_STATE_READY' - session state transition"
echo "2. 'xrBeginSession [start]' and 'xrBeginSession [end]' - session started"
echo "3. 'OpenXR session started' - confirmation"
echo "4. 'OpenXR frame loop started' - frame rendering active"
echo ""
echo "If you see 'Activity is not yet in the ready state', the fix didn't work."