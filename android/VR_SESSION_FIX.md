# OpenXR Session State Fix for Meta Quest

## Problem
The OpenXR session was stuck at `XR_SESSION_STATE_IDLE` and never transitioned to `READY`. Quest runtime logged:
```
xrCreateSession: Activity is not yet in the ready state.
```

## Root Cause
Meta Quest's OpenXR runtime requires a specific type of Android Activity for VR applications. The original `EmulationActivity` extended `AppCompatActivity` with Jetpack Compose UI, which Quest's runtime doesn't recognize as VR-ready.

## Solution
Created a separate `VREmulationActivity` that extends the basic `Activity` class (not `AppCompatActivity`) and removed all Compose UI. This matches the pattern used by working Quest VR apps and Meta's own OpenXR samples.

### Key Changes

1. **New VR Activity**: `VREmulationActivity.kt`
   - Extends `android.app.Activity` (not `AppCompatActivity`)
   - No Jetpack Compose UI - just basic Activity lifecycle
   - Calls `requestVrMode()` for Quest compatibility
   - Sets proper immersive/fullscreen flags
   - Initializes OpenXR immediately in `onResume()`

2. **AndroidManifest.xml Updates**:
   - `VREmulationActivity` has VR intent filters:
     ```xml
     <category android:name="com.oculus.intent.category.VR" />
     <category android:name="org.khronos.openxr.intent.category.IMMERSIVE_HMD" />
     ```
   - `EmulationActivity` keeps regular 2D intent filters
   - Uses `Theme.Black.NoTitleBar.Fullscreen` for VR activity

3. **Shared Constants**: `EmulationConstants.kt`
   - Moved `EXTRA_LAUNCH_PATH` to shared object
   - Prevents duplicate constant definitions

## Results
After implementing the fix, the OpenXR session successfully transitions:

```
OpenXR: nativeOnActivityReady: info.cemu.cemu.debug/info.cemu.cemu.emulation.VREmulationActivity
OpenXR: PostSessionStateChange: XR_SESSION_STATE_IDLE -> XR_SESSION_STATE_READY
OpenXR: ------------ xrBeginSession [start] -----------
OpenXR: ------------ xrBeginSession [end] -----------
OpenXRManager: OpenXR session started
```

## Files Modified/Created

### Created:
- `/app/src/main/java/info/cemu/cemu/emulation/VREmulationActivity.kt`
- `/app/src/main/java/info/cemu/cemu/emulation/EmulationConstants.kt`
- `/test_vr_session.sh`

### Modified:
- `/app/src/main/AndroidManifest.xml` - Added VREmulationActivity with VR intent filters
- `/app/src/main/java/info/cemu/cemu/emulation/EmulationActivity.kt` - Use shared constants, remove duplicate companion object
- `/app/src/main/java/info/cemu/cemu/MainActivity.kt` - Use shared constants

## Testing
Run the test script to verify the fix:
```bash
./test_vr_session.sh
```

This builds, installs, and launches the VR activity, then checks logs for successful OpenXR session transition.

## Key Insight
Meta Quest's OpenXR runtime performs activity type validation. It expects either:
1. `android.app.NativeActivity` (for native apps)
2. Basic `android.app.Activity` (for simple VR apps)
3. **NOT** `AppCompatActivity` with complex UI frameworks

The solution follows the same pattern as Meta's official OpenXR samples and other working Quest VR applications.