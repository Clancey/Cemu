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
import info.cemu.cemu.nativeinterface.NativeEmulation
import kotlin.system.exitProcess

class EmulationActivity : AppCompatActivity() {
    private lateinit var sensorManager: SensorManager
    @Volatile private var openxrInputRunning = false
    private var openxrInputThread: Thread? = null

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
        android.util.Log.d("Cemu", "EmulationActivity.onCreate() called")
        currentActivity = this
        sensorManager = SensorManager(this)
        sensorManager.setDeviceRotationProvider { display.rotation }

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        setFullscreen()

        // Initialize OpenXR for controller input (not rendering)
        initOpenXRInput()

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

    private fun initOpenXRInput() {
        Thread {
            try {
                // Wait a moment for the activity window to be ready
                Thread.sleep(500)
                val result = NativeEmulation.initializeOpenXR(this)
                android.util.Log.d("Cemu", "OpenXR input init result: $result")
                if (result) {
                    startOpenXRInputPolling()
                }
            } catch (e: Exception) {
                android.util.Log.e("Cemu", "OpenXR input init failed: ${e.message}")
            }
        }.start()
    }

    private fun startOpenXRInputPolling() {
        openxrInputRunning = true
        openxrInputThread = Thread {
            android.util.Log.d("Cemu", "OpenXR input polling started")
            while (openxrInputRunning) {
                try {
                    NativeEmulation.pollOpenXRInput()
                } catch (e: Exception) {
                    android.util.Log.e("Cemu", "OpenXR poll error: ${e.message}")
                    break
                }
                Thread.sleep(16) // ~60Hz
            }
            android.util.Log.d("Cemu", "OpenXR input polling stopped")
        }.also { it.start() }
    }

    override fun onPause() {
        super.onPause()
        sensorManager.pauseListening()
    }

    override fun onResume() {
        super.onResume()
        sensorManager.resumeListening()
    }

    override fun onDestroy() {
        super.onDestroy()
        openxrInputRunning = false
        openxrInputThread?.join(1000)
        sensorManager.pauseListening()
        try {
            NativeEmulation.shutdownOpenXR()
        } catch (e: Exception) {
            android.util.Log.e("Cemu", "Error shutting down OpenXR: ${e.message}")
        }
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
}
