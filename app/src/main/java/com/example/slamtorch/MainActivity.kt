package com.example.slamtorch

import android.Manifest
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Bundle
import android.view.Surface
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.google.androidgamesdk.GameActivity
import com.google.android.material.floatingactionbutton.FloatingActionButton

class MainActivity : GameActivity() {
    private lateinit var torchController: TorchController
    private lateinit var debugOverlay: TextView
    private var debugEnabled = false
    private var uiCreated = false
    
    // Native methods
    private external fun nativeUpdateRotation(rotation: Int)
    private external fun nativeClearMap()
    private external fun nativeCycleTorch()
    private external fun nativeGetDebugStats(): DebugStats

    companion object {
        private const val CAMERA_PERMISSION_REQUEST_CODE = 100
        
        init {
            System.loadLibrary("slamtorch")
        }
    }
    
    data class DebugStats(
        val trackingState: String,
        val pointCount: Int,
        val mapPoints: Int,
        val fps: Float,
        val torchMode: String,
        val depthEnabled: Boolean
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
        
        // Add overlay directly to content view with higher Z-order
        val contentView = findViewById<ViewGroup>(android.R.id.content)
        val overlay = FrameLayout(this).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            elevation = 100f  // High elevation to stay on top
            isClickable = false  // Don't block touches to GL surface
        }
        
        // Debug overlay (top-left)
        debugOverlay = TextView(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply {
                setMargins(16, 60, 0, 0)
            }
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 11f
            setShadowLayer(4f, 0f, 0f, 0xFF000000.toInt())
            visibility = View.GONE
        }
        overlay.addView(debugOverlay)
        
        // Control buttons container (bottom-right)
        val buttonContainer = FrameLayout(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply {
                gravity = android.view.Gravity.BOTTOM or android.view.Gravity.END
                setMargins(0, 0, 16, 16)
            }
        }
        
        // Debug toggle button
        val debugBtn = FloatingActionButton(this).apply {
            size = FloatingActionButton.SIZE_MINI
            setImageResource(android.R.drawable.ic_menu_info_details)
            isClickable = true
            isFocusable = true
            elevation = 12f
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply {
                setMargins(0, 0, 0, 140)
            }
            setOnClickListener {
                android.util.Log.i("SlamTorch", "Debug button clicked")
                debugEnabled = !debugEnabled
                debugOverlay.visibility = if (debugEnabled) View.VISIBLE else View.GONE
                if (debugEnabled) startDebugUpdates()
            }
        }
        buttonContainer.addView(debugBtn)
        
        // Clear map button
        val clearBtn = FloatingActionButton(this).apply {
            size = FloatingActionButton.SIZE_MINI
            setImageResource(android.R.drawable.ic_menu_delete)
            isClickable = true
            isFocusable = true
            elevation = 12f
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply {
                setMargins(0, 0, 0, 70)
            }
            setOnClickListener {
                android.util.Log.i("SlamTorch", "Clear map button clicked")
                nativeClearMap()
            }
        }
        buttonContainer.addView(clearBtn)
        
        // Torch toggle button
        val torchBtn = FloatingActionButton(this).apply {
            size = FloatingActionButton.SIZE_MINI
            setImageResource(android.R.drawable.ic_menu_day)
            isClickable = true
            isFocusable = true
            elevation = 12f
            setOnClickListener {
                android.util.Log.i("SlamTorch", "Torch button clicked")
                nativeCycleTorch()
            }
        }
        buttonContainer.addView(torchBtn)
        
        overlay.addView(buttonContainer)
        
        // Add overlay to content view
        contentView.addView(overlay)
        overlay.bringToFront()
        contentView.requestLayout()
        
        android.util.Log.i("SlamTorch", "UI overlay created with elevation=100f")
    }
    
    private fun startDebugUpdates() {
        debugOverlay.postDelayed(object : Runnable {
            override fun run() {
                if (debugEnabled) {
                    try {
                        val stats = nativeGetDebugStats()
                        debugOverlay.text = """
                            Track: ${stats.trackingState}
                            Points: ${stats.pointCount}
                            Map: ${stats.mapPoints}
                            FPS: ${"%.1f".format(stats.fps)}
                            Torch: ${stats.torchMode}
                            Depth: ${if (stats.depthEnabled) "YES" else "NO"}
                        """.trimIndent()
                    } catch (e: Exception) {
                        debugOverlay.text = "Stats unavailable"
                    }
                    debugOverlay.postDelayed(this, 100)
                }
            }
        }, 100)
    }
    
    private fun updateNativeRotation() {
        val rotation = when (windowManager.defaultDisplay.rotation) {
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

    // Called from JNI
    fun setTorchEnabled(enabled: Boolean) {
        runOnUiThread {
            torchController.setTorch(enabled)
        }
    }
}
