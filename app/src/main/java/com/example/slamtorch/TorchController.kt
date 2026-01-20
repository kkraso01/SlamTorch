package com.example.slamtorch

import android.content.Context
import android.hardware.camera2.CameraManager
import android.util.Log

class TorchController(private val context: Context) {
    private val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private var cameraId: String? = null

    init {
        try {
            cameraId = cameraManager.cameraIdList.firstOrNull { id ->
                val characteristics = cameraManager.getCameraCharacteristics(id)
                characteristics.get(android.hardware.camera2.CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
            }
        } catch (e: Exception) {
            Log.e("TorchController", "Failed to find camera with flash", e)
        }
    }

    fun setTorch(enabled: Boolean) {
        val id = cameraId ?: return
        try {
            cameraManager.setTorchMode(id, enabled)
        } catch (e: Exception) {
            Log.e("TorchController", "Failed to set torch mode", e)
        }
    }
}
