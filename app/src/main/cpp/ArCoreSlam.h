#ifndef SLAMTORCH_ARCORE_SLAM_H
#define SLAMTORCH_ARCORE_SLAM_H

#include "arcore/arcore_c_api.h"
#include "DepthFrame.h"
#include <android/native_window.h>
#include <cstdint>
#include <jni.h>

class ArCoreSlam {
public:
    ArCoreSlam(JNIEnv* env, jobject activity);
    ~ArCoreSlam();

    void OnResume(JNIEnv* env);
    void OnPause();
    void OnSurfaceChanged(int rotation, int width, int height);
    
    // Main frame loop update
    void Update(JNIEnv* env);

    // Getters for rendering (zero-copy)
    const ArSession* GetSession() const { return ar_session_; }
    const ArFrame* GetFrame() const { return ar_frame_; }
    ArTrackingState GetTrackingState() const { return tracking_state_; }
    
    // Get camera matrices (preallocated buffers)
    void GetViewMatrix(float* out_matrix) const;
    void GetProjectionMatrix(float near, float far, float* out_matrix) const;
    void GetWorldFromCameraMatrix(float* out_matrix) const;  // For persistent mapping
    const ArPointCloud* GetPointCloud() const { return ar_point_cloud_; }
    const char* GetLastTrackingFailureReason() const { return last_tracking_failure_reason_; }
    void UpdatePlaneList();
    const ArTrackableList* GetPlaneList() const { return plane_list_; }

    // CPU image acquisition (Y plane only). Returns true if image copied.
    bool AcquireCameraImageY(uint8_t* dst, int dst_stride, int dst_capacity,
                             int* out_width, int* out_height);

    // Depth image acquisition (16-bit). Caller must release via ReleaseDepthImage.
    enum class DepthSource { OFF, DEPTH, RAW };
    bool AcquireDepthFrame(DepthSource source, DepthFrame* out_frame,
                           ArImage** out_depth_image, ArImage** out_confidence_image);
    void ReleaseDepthImage(ArImage* image);

    // Camera intrinsics (image space)
    void GetImageDimensions(int* out_width, int* out_height) const;
    void GetCameraIntrinsics(float* out_fx, float* out_fy, float* out_cx, float* out_cy) const;

    // Torch Logic
    enum class TorchMode { AUTO, MANUAL_ON, MANUAL_OFF };
    void SetTorchMode(TorchMode mode) { torch_mode_ = mode; }
    TorchMode GetTorchMode() const { return torch_mode_; }
    bool IsDepthEnabled() const { return depth_enabled_; }
    bool IsDepthSupported() const { return depth_supported_; }
    bool IsTorchOn() const { return current_torch_state_; }
    bool IsTorchAvailable() const { return torch_available_; }

private:
    ArSession* ar_session_ = nullptr;
    ArFrame* ar_frame_ = nullptr;
    ArPointCloud* ar_point_cloud_ = nullptr;
    ArCamera* ar_camera_ = nullptr;
    ArPose* ar_pose_ = nullptr;
    ArLightEstimate* ar_light_estimate_ = nullptr;
    ArCameraIntrinsics* ar_intrinsics_ = nullptr;
    ArTrackableList* plane_list_ = nullptr;
    
    ArTrackingState tracking_state_ = AR_TRACKING_STATE_STOPPED;
    bool install_requested_ = false;
    bool depth_enabled_ = false;
    bool depth_supported_ = false;
    bool camera_intrinsics_logged_ = false;
    int32_t image_width_ = 0;
    int32_t image_height_ = 0;
    float intrinsics_fx_ = 0.0f;
    float intrinsics_fy_ = 0.0f;
    float intrinsics_cx_ = 0.0f;
    float intrinsics_cy_ = 0.0f;
    const char* last_tracking_failure_reason_ = "NONE";

    // Torch control
    TorchMode torch_mode_ = TorchMode::AUTO;
    bool current_torch_state_ = false;
    bool pending_torch_state_ = false;
    int torch_pending_frames_ = 0;
    bool torch_available_ = false;
    int display_rotation_ = 0;
    int ar_frame_width_ = 0;
    int ar_frame_height_ = 0;
    
    jobject activity_obj_ = nullptr;
    JavaVM* java_vm_ = nullptr;
    jmethodID set_torch_method_ = nullptr;
    jmethodID is_torch_available_method_ = nullptr;

    void UpdateTorchLogic(JNIEnv* env, float light_intensity);
    void CallJavaSetTorch(JNIEnv* env, bool enabled);
    void UpdateDisplayGeometry(int rotation, int width, int height);
};

#endif // SLAMTORCH_ARCORE_SLAM_H
