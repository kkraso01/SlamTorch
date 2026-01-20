package com.example.slamtorch

import android.content.Context
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraCharacteristics
import android.util.Log

class TorchController(private val context: Context) {
    private val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private var cameraId: String? = null
    private var torchAvailable = false

    init {
        try {
            cameraId = cameraManager.cameraIdList.firstOrNull { id ->
                val characteristics = cameraManager.getCameraCharacteristics(id)
                val hasFlash = characteristics.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
                val isBack = characteristics.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK
                hasFlash && isBack
            }
            torchAvailable = cameraId != null
        } catch (e: Exception) {
            Log.e("TorchController", "Failed to find camera with flash", e)
        }
    }

    fun setTorch(enabled: Boolean) {
        if (!torchAvailable) return
        val id = cameraId ?: return
        try {
            cameraManager.setTorchMode(id, enabled)
        } catch (e: Exception) {
            Log.e("TorchController", "Failed to set torch mode", e)
        }
    }

    fun isTorchAvailable(): Boolean = torchAvailable
}
