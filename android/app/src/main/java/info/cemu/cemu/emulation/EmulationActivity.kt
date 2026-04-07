package info.cemu.cemu.emulation

import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import info.cemu.cemu.BuildConfig
import info.cemu.cemu.common.ui.components.ActivityContent
import info.cemu.cemu.common.ui.localization.TranslatableContent
import kotlin.system.exitProcess

class EmulationActivity : AppCompatActivity() {
    private lateinit var sensorManager: SensorManager

    companion object {
        @JvmStatic
        var currentActivity: EmulationActivity? = null
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
            launchPath = extras.getString(EXTRA_LAUNCH_PATH)
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
        android.util.Log.d("Cemu", "EmulationActivity.onCreate() called")
        currentActivity = this
        sensorManager = SensorManager(this)
        sensorManager.setDeviceRotationProvider { display.rotation }

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Request immersive mode for Quest VR
        try {
            // On Quest, requestVrMode tells the system this is a VR activity
            val vrMethod = android.app.Activity::class.java.getMethod("requestVrMode", android.content.ComponentName::class.java)
            vrMethod.invoke(this, null as android.content.ComponentName?)
            android.util.Log.d("Cemu", "requestVrMode called successfully")
        } catch (e: Exception) {
            android.util.Log.d("Cemu", "requestVrMode not available: ${e.message}")
        }

        setFullscreen()

        val gamePath = getGamePath()

        setContent {
            TranslatableContent {
                ActivityContent {
                    EmulationScreen(
                        gamePath = gamePath,
                        setMotionSensorEnabled = sensorManager::setIsListening,
                        onQuit = ::onQuit,
                    )
                }
            }
        }
    }

    override fun onPause() {
        super.onPause()
        sensorManager.pauseListening()
    }

    private var openxrInitialized = false

    override fun onResume() {
        super.onResume()
        android.util.Log.d("Cemu", "EmulationActivity.onResume() called")
        sensorManager.resumeListening()

        // Initialize OpenXR after window is ready — use post() to defer past layout
        if (!openxrInitialized) {
            openxrInitialized = true
            // Wait for window to be fully drawn before OpenXR init
            window.decorView.post {
                android.util.Log.d("Cemu", "Window ready, starting OpenXR init")
                Thread {
                    // Small delay to let Quest compositor process the window
                    Thread.sleep(500)
                    try {
                        val result = info.cemu.cemu.nativeinterface.NativeEmulation.initializeOpenXR(this)
                        android.util.Log.d("Cemu", "OpenXR init result: $result")
                    } catch (e: Exception) {
                        android.util.Log.e("Cemu", "OpenXR init failed: ${e.message}")
                    }
                }.start()
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        sensorManager.pauseListening()
    }

    private fun setFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
    }

    private fun onQuit() {
        finish()
        exitProcess(0)
    }

    companion object {
        const val EXTRA_LAUNCH_PATH: String = BuildConfig.APPLICATION_ID + ".LaunchPath"
    }
}
