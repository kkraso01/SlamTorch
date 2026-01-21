package com.example.slamtorch

import android.Manifest
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Surface
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.content.getSystemService
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import android.hardware.display.DisplayManager
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    private lateinit var torchController: TorchController
    private lateinit var debugOverlay: TextView
    private lateinit var debugToggleButton: MaterialButton
    private lateinit var clearMapButton: MaterialButton
    private lateinit var clearMeshButton: MaterialButton
    private lateinit var torchToggleGroup: MaterialButtonToggleGroup
    private lateinit var depthMeshToggleGroup: MaterialButtonToggleGroup
    private lateinit var mapToggleButton: MaterialButton
    private lateinit var planeToggleButton: MaterialButton
    private lateinit var wireframeToggleButton: MaterialButton
    private var debugEnabled = true
    private var uiCreated = false
    private val uiHandler = Handler(Looper.getMainLooper())
    private var displayManager: DisplayManager? = null
    
    // Native methods
    private external fun nativeUpdateRotation(rotation: Int)
    private external fun nativeClearMap()
    private external fun nativeSetTorchMode(mode: Int)
    private external fun nativeSetMapEnabled(enabled: Boolean)
    private external fun nativeSetDebugEnabled(enabled: Boolean)
    private external fun nativeSetPlanesEnabled(enabled: Boolean)
    private external fun nativeSetDepthMeshMode(mode: Int)
    private external fun nativeSetWireframeEnabled(enabled: Boolean)
    private external fun nativeClearDepthMesh()
    private external fun nativeGetDebugStats(): DebugStats

    companion object {
        private const val CAMERA_PERMISSION_REQUEST_CODE = 100
        private const val TORCH_MODE_AUTO = 0
        private const val TORCH_MODE_ON = 1
        private const val TORCH_MODE_OFF = 2
        private const val DEPTH_MODE_OFF = 0
        private const val DEPTH_MODE_DEPTH = 1
        private const val DEPTH_MODE_RAW = 2
        
        init {
            System.loadLibrary("slamtorch")
        }
    }
    
    data class DebugStats(
        val trackingState: String,
        val pointCount: Int,
        val mapPoints: Int,
        val bearingLandmarks: Int,
        val metricLandmarks: Int,
        val trackedFeatures: Int,
        val stableTracks: Int,
        val avgTrackAge: Float,
        val depthHitRate: Float,
        val fps: Float,
        val torchMode: String,
        val torchEnabled: Boolean,
        val depthEnabled: Boolean,
        val depthSupported: Boolean,
        val depthMode: String,
        val depthWidth: Int,
        val depthHeight: Int,
        val depthMinM: Float,
        val depthMaxM: Float,
        val voxelsUsed: Int,
        val pointsFusedPerSecond: Int,
        val mapEnabled: Boolean,
        val depthOverlayEnabled: Boolean,
        val lastFailureReason: String,
        val planesEnabled: Boolean,
        val depthMeshMode: String,
        val depthMeshWireframe: Boolean,
        val depthMeshWidth: Int,
        val depthMeshHeight: Int,
        val depthMeshValidRatio: Float
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Make fullscreen immersive
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_FULLSCREEN
            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        )
        
        torchController = TorchController(this)
        displayManager = getSystemService<DisplayManager>()
        
        // Request camera permission if not granted
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) 
            != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(
                this,
                arrayOf(Manifest.permission.CAMERA),
                CAMERA_PERMISSION_REQUEST_CODE
            )
        }
    }
    
    private fun createUIOverlay() {
        if (uiCreated) return
        uiCreated = true
        
        val contentView = findViewById<ViewGroup>(android.R.id.content)
        val overlay = layoutInflater.inflate(R.layout.overlay_controls, contentView, false).apply {
            elevation = 100f
            isClickable = false
        }
        debugOverlay = overlay.findViewById(R.id.debugOverlay)
        debugToggleButton = overlay.findViewById(R.id.debugToggleButton)
        clearMapButton = overlay.findViewById(R.id.clearMapButton)
        clearMeshButton = overlay.findViewById(R.id.clearMeshButton)
        torchToggleGroup = overlay.findViewById(R.id.torchToggleGroup)
        depthMeshToggleGroup = overlay.findViewById(R.id.depthMeshToggleGroup)
        mapToggleButton = overlay.findViewById(R.id.mapToggleButton)
        planeToggleButton = overlay.findViewById(R.id.planeToggleButton)
        wireframeToggleButton = overlay.findViewById(R.id.wireframeToggleButton)
        
        debugToggleButton.setOnClickListener {
            debugEnabled = debugToggleButton.isChecked
            debugOverlay.visibility = if (debugEnabled) View.VISIBLE else View.GONE
            nativeSetDebugEnabled(debugEnabled)
            if (debugEnabled) startDebugUpdates()
        }
        debugToggleButton.isChecked = true
        debugOverlay.visibility = View.VISIBLE
        nativeSetDebugEnabled(true)
        startDebugUpdates()

        clearMapButton.setOnClickListener { nativeClearMap() }
        clearMeshButton.setOnClickListener { nativeClearDepthMesh() }
        mapToggleButton.setOnClickListener {
            val enabled = mapToggleButton.isChecked
            nativeSetMapEnabled(enabled)
            mapToggleButton.text = if (enabled) "Map ON" else "Map OFF"
        }

        torchToggleGroup.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (!isChecked) return@addOnButtonCheckedListener
            when (checkedId) {
                R.id.torchAutoButton -> nativeSetTorchMode(TORCH_MODE_AUTO)
                R.id.torchOnButton -> nativeSetTorchMode(TORCH_MODE_ON)
                R.id.torchOffButton -> nativeSetTorchMode(TORCH_MODE_OFF)
            }
        }
        torchToggleGroup.check(R.id.torchAutoButton)
        depthMeshToggleGroup.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (!isChecked) return@addOnButtonCheckedListener
            when (checkedId) {
                R.id.meshOffButton -> nativeSetDepthMeshMode(DEPTH_MODE_OFF)
                R.id.meshDepthButton -> nativeSetDepthMeshMode(DEPTH_MODE_DEPTH)
                R.id.meshRawButton -> nativeSetDepthMeshMode(DEPTH_MODE_RAW)
            }
        }
        depthMeshToggleGroup.check(R.id.meshOffButton)
        nativeSetDepthMeshMode(DEPTH_MODE_OFF)
        mapToggleButton.isChecked = true
        mapToggleButton.text = "Map ON"

        planeToggleButton.setOnClickListener {
            val enabled = planeToggleButton.isChecked
            nativeSetPlanesEnabled(enabled)
            planeToggleButton.text = if (enabled) "Planes ON" else "Planes OFF"
        }
        planeToggleButton.isChecked = true
        nativeSetPlanesEnabled(true)

        wireframeToggleButton.setOnClickListener {
            val enabled = wireframeToggleButton.isChecked
            nativeSetWireframeEnabled(enabled)
            wireframeToggleButton.text = if (enabled) "Wire ON" else "Wire OFF"
        }
        wireframeToggleButton.isChecked = false
        nativeSetWireframeEnabled(false)
        if (!torchController.isTorchAvailable()) {
            torchToggleGroup.isEnabled = false
            overlay.findViewById<View>(R.id.torchAutoButton).isEnabled = false
            overlay.findViewById<View>(R.id.torchOnButton).isEnabled = false
            overlay.findViewById<View>(R.id.torchOffButton).isEnabled = false
        }

        contentView.addView(overlay)
        overlay.bringToFront()
        contentView.requestLayout()
        
        android.util.Log.i("SlamTorch", "UI overlay created with elevation=100f")
    }
    
    private fun startDebugUpdates() {
        uiHandler.postDelayed(object : Runnable {
            override fun run() {
                if (debugEnabled) {
                    try {
                        val stats = nativeGetDebugStats()
                        val torchState = if (stats.torchMode == "AUTO") {
                            "AUTO (${if (stats.torchEnabled) "ON" else "OFF"})"
                        } else {
                            stats.torchMode
                        }
                        val depthState = if (!stats.depthSupported) {
                            "UNSUPPORTED"
                        } else {
                            "${stats.depthMode} ${if (stats.depthEnabled) "ON" else "OFF"}"
                        }
                        val meshState = if (!stats.depthSupported) {
                            "UNSUPPORTED"
                        } else {
                            stats.depthMeshMode
                        }
                        debugOverlay.text = """
                            Track: ${stats.trackingState}
                            Fail: ${stats.lastFailureReason}
                            Points: ${stats.pointCount}
                            Map: ${stats.mapPoints} (B:${stats.bearingLandmarks} M:${stats.metricLandmarks})
                            Tracks: ${stats.trackedFeatures} (Stable: ${stats.stableTracks})
                            Avg age: ${"%.1f".format(stats.avgTrackAge)}
                            Depth hit: ${"%.0f".format(stats.depthHitRate)}%
                            Depth: $depthState (${stats.depthWidth}x${stats.depthHeight})
                            Depth min/max: ${"%.2f".format(stats.depthMinM)} / ${"%.2f".format(stats.depthMaxM)} m
                            Mesh: $meshState (${stats.depthMeshWidth}x${stats.depthMeshHeight}) valid=${"%.0f".format(stats.depthMeshValidRatio * 100)}%
                            Planes: ${if (stats.planesEnabled) "ON" else "OFF"} / Wire: ${if (stats.depthMeshWireframe) "ON" else "OFF"}
                            Voxels: ${stats.voxelsUsed} (fused/s: ${stats.pointsFusedPerSecond})
                            FPS: ${"%.1f".format(stats.fps)}
                            Torch: $torchState
                            Map: ${if (stats.mapEnabled) "ON" else "OFF"} / Overlay: ${if (stats.depthOverlayEnabled) "ON" else "OFF"}
                        """.trimIndent()
                    } catch (e: Exception) {
                        debugOverlay.text = "Stats unavailable"
                    }
                    uiHandler.postDelayed(this, 100)
                }
            }
        }, 100)
    }
    
    private fun updateNativeRotation() {
        val rotation = when (display?.rotation ?: Surface.ROTATION_0) {
            Surface.ROTATION_0 -> 0
            Surface.ROTATION_90 -> 1
            Surface.ROTATION_180 -> 2
            Surface.ROTATION_270 -> 3
            else -> 0
        }
        nativeUpdateRotation(rotation)
    }
    
    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        updateNativeRotation()
    }

    override fun onResume() {
        super.onResume()
        displayManager?.registerDisplayListener(displayListener, uiHandler)
        updateNativeRotation()
    }

    override fun onPause() {
        super.onPause()
        displayManager?.unregisterDisplayListener(displayListener)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            // Keep fullscreen immersive
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            )
            
            // Create UI overlay after GL surface is ready
            if (!uiCreated) {
                window.decorView.postDelayed({
                    try {
                        createUIOverlay()
                        updateNativeRotation()
                    } catch (e: Exception) {
                        android.util.Log.e("SlamTorch", "Failed to create UI: ${e.message}")
                    }
                }, 200)
            }
        }
    }

    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) = Unit
        override fun onDisplayRemoved(displayId: Int) = Unit
        override fun onDisplayChanged(displayId: Int) {
            updateNativeRotation()
        }
    }

    // Called from JNI
    fun setTorchEnabled(enabled: Boolean) {
        runOnUiThread {
            torchController.setTorch(enabled)
        }
    }

    fun isTorchAvailable(): Boolean = torchController.isTorchAvailable()
}
