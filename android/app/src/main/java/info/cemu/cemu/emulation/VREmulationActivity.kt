package info.cemu.cemu.emulation

import android.app.Activity
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import info.cemu.cemu.BuildConfig

/**
 * VR-specific activity for Meta Quest OpenXR rendering.
 * This extends the basic Activity class (not AppCompatActivity) to ensure
 * Quest's OpenXR runtime recognizes it as a VR-ready activity.
 * No Compose UI - just basic OpenXR initialization.
 */
class VREmulationActivity : Activity() {
    private lateinit var sensorManager: SensorManager

    companion object {
        @JvmStatic
        var currentActivity: VREmulationActivity? = null
            private set
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (InputHandler.onMotionEvent(event)) {
            return true
        }
        return super.onGenericMotionEvent(event)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (InputHandler.onKeyEvent(event)) {
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    private fun getGamePath(): String {
        val extras = intent.extras
        val data = intent.data
        var launchPath: String? = null

        if (extras != null) {
            launchPath = extras.getString(EmulationConstants.EXTRA_LAUNCH_PATH)
        }

        if (launchPath == null && data != null) {
            launchPath = data.toString()
        }

        if (launchPath == null) {
            throw RuntimeException("launchPath is null")
        }

        return launchPath
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        android.util.Log.d("Cemu", "VREmulationActivity.onCreate() called")
        currentActivity = this
        sensorManager = SensorManager(this)
        sensorManager.setDeviceRotationProvider { display.rotation }

        // Keep screen on and set immersive mode for VR
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Remove all decorations for VR
        window.decorView.systemUiVisibility = (
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
            android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            android.view.View.SYSTEM_UI_FLAG_FULLSCREEN or
            android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        )

        // Request VR mode - this is important for Quest
        try {
            val vrMethod = android.app.Activity::class.java.getMethod("requestVrMode", android.content.ComponentName::class.java)
            vrMethod.invoke(this, null as android.content.ComponentName?)
            android.util.Log.d("Cemu", "requestVrMode called successfully")
        } catch (e: Exception) {
            android.util.Log.d("Cemu", "requestVrMode not available: ${e.message}")
        }

        val gamePath = getGamePath()
        android.util.Log.d("Cemu", "VR Activity launching game: $gamePath")

        // No UI - just a blank activity that starts OpenXR
        // The OpenXR swapchain will handle all VR rendering
    }

    override fun onPause() {
        super.onPause()
        sensorManager.pauseListening()
    }

    private var openxrInitialized = false

    override fun onResume() {
        super.onResume()
        android.util.Log.d("Cemu", "VREmulationActivity.onResume() called")
        sensorManager.resumeListening()

        // Initialize OpenXR and launch game in VR
        if (!openxrInitialized) {
            openxrInitialized = true
            Thread {
                try {
                    android.util.Log.d("Cemu", "VR: Starting OpenXR init")
                    val result = info.cemu.cemu.nativeinterface.NativeEmulation.initializeOpenXR(this)
                    android.util.Log.d("Cemu", "VR: OpenXR init result: $result")

                    if (result) {
                        val gamePath = getGamePath()

                        // 1. Prepare title and systems (waits for CemuCommonInit)
                        val prepareResult = info.cemu.cemu.nativeinterface.NativeEmulation.prepareTitle(gamePath)
                        if (prepareResult != 0) {
                            android.util.Log.e("Cemu", "VR: Failed to prepare title: $prepareResult")
                            return@Thread
                        }
                        info.cemu.cemu.nativeinterface.NativeEmulation.initializeSystems()

                        // 2. Create renderer + start session + launch game (handles keepalive internally)
                        info.cemu.cemu.nativeinterface.NativeEmulation.initializeRendererForVR()

                        // 3. Launch game
                        info.cemu.cemu.nativeinterface.NativeEmulation.launchTitle()
                        android.util.Log.d("Cemu", "VR: Game launched!")

                        // Input polling disabled for now — conflicts with keepalive thread
                        // TODO: integrate input polling into the keepalive frame loop
                    } else {
                        android.util.Log.e("Cemu", "VR: OpenXR initialization failed")
                    }
                } catch (e: Exception) {
                    android.util.Log.e("Cemu", "VR: Init failed: ${e.message}", e)
                }
            }.start()
        }
    }

    @Volatile private var inputPollingRunning = false
    private var inputPollingThread: Thread? = null

    private fun startInputPolling() {
        inputPollingRunning = true
        inputPollingThread = Thread {
            android.util.Log.d("Cemu", "VR: Input polling started")
            while (inputPollingRunning) {
                try {
                    info.cemu.cemu.nativeinterface.NativeEmulation.pollOpenXRInput()
                } catch (e: Exception) {
                    android.util.Log.e("Cemu", "VR: Input poll error: ${e.message}")
                    break
                }
                Thread.sleep(16) // ~60Hz
            }
            android.util.Log.d("Cemu", "VR: Input polling stopped")
        }.also { it.start() }
    }

    override fun onDestroy() {
        super.onDestroy()
        android.util.Log.d("Cemu", "VREmulationActivity.onDestroy() called")
        inputPollingRunning = false
        inputPollingThread?.join(1000)
        sensorManager.pauseListening()

        try {
            info.cemu.cemu.nativeinterface.NativeEmulation.shutdownOpenXR()
        } catch (e: Exception) {
            android.util.Log.e("Cemu", "Error shutting down OpenXR: ${e.message}")
        }
    }
}