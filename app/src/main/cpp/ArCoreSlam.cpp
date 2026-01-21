#include "ArCoreSlam.h"
#include "AndroidOut.h"
#include <assert.h>
#include <cstring>

ArCoreSlam::ArCoreSlam(JNIEnv* env, jobject activity) {
    if (!activity) {
        aout << "Activity object is null, cannot initialize ARCore" << std::endl;
        ar_session_ = nullptr;
        activity_obj_ = nullptr;
        set_torch_method_ = nullptr;
        is_torch_available_method_ = nullptr;
        return;
    }
    
    activity_obj_ = env->NewGlobalRef(activity);
    if (!activity_obj_) {
        aout << "Failed to create global reference for activity" << std::endl;
        ar_session_ = nullptr;
        set_torch_method_ = nullptr;
        is_torch_available_method_ = nullptr;
        return;
    }

    env->GetJavaVM(&java_vm_);
    
    jclass clazz = env->GetObjectClass(activity_obj_);
    set_torch_method_ = env->GetMethodID(clazz, "setTorchEnabled", "(Z)V");
    is_torch_available_method_ = env->GetMethodID(clazz, "isTorchAvailable", "()Z");
    env->DeleteLocalRef(clazz);

    ArStatus status = ArSession_create(env, activity, &ar_session_);
    if (status != AR_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "ArSession_create FAILED: %d", status);
        ar_session_ = nullptr;
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "ArSession created successfully");
    
    // Create Frame
    ArFrame_create(ar_session_, &ar_frame_);
    
    // Create reusable objects (zero per-frame allocation)
    ArPose_create(ar_session_, nullptr, &ar_pose_);
    ArLightEstimate_create(ar_session_, &ar_light_estimate_);
    ArCameraIntrinsics_create(ar_session_, &ar_intrinsics_);
    ArTrackableList_create(ar_session_, &plane_list_);

    ArConfig* ar_config = nullptr;
    ArConfig_create(ar_session_, &ar_config);
    
    // LATEST_CAMERA_IMAGE keeps rendering responsive and avoids blocking the render loop.
    ArConfig_setUpdateMode(ar_session_, ar_config, AR_UPDATE_MODE_LATEST_CAMERA_IMAGE);

    // AUTO focus: Essential for tracking varied distances (fallback to FIXED if unsupported).
    ArConfig_setFocusMode(ar_session_, ar_config, AR_FOCUS_MODE_AUTO);

    // Enable depth if supported (improves occlusion and tracking robustness)
    int32_t depth_supported = 0;
    ArSession_isDepthModeSupported(ar_session_, AR_DEPTH_MODE_AUTOMATIC, &depth_supported);
    depth_supported_ = (depth_supported != 0);
    if (depth_supported_) {
        ArConfig_setDepthMode(ar_session_, ar_config, AR_DEPTH_MODE_AUTOMATIC);
    } else {
        ArConfig_setDepthMode(ar_session_, ar_config, AR_DEPTH_MODE_DISABLED);
        __android_log_print(ANDROID_LOG_WARN, "SlamTorch", "Depth unsupported on this device/camera");
    }
    
    // Enable light estimation for better feature matching
    ArConfig_setLightEstimationMode(ar_session_, ar_config, AR_LIGHT_ESTIMATION_MODE_AMBIENT_INTENSITY);

    // Enable plane finding (horizontal + vertical)
    ArConfig_setPlaneFindingMode(ar_session_, ar_config, AR_PLANE_FINDING_MODE_HORIZONTAL_AND_VERTICAL);

    // Enable EIS if supported for more stable camera feed.
    int32_t eis_supported = 0;
    ArSession_isImageStabilizationModeSupported(ar_session_, AR_IMAGE_STABILIZATION_MODE_EIS, &eis_supported);
    if (eis_supported != 0) {
        ArConfig_setImageStabilizationMode(ar_session_, ar_config, AR_IMAGE_STABILIZATION_MODE_EIS);
    }
    
    status = ArSession_configure(ar_session_, ar_config);
    if (status == AR_ERROR_UNSUPPORTED_CONFIGURATION) {
        ArConfig_setFocusMode(ar_session_, ar_config, AR_FOCUS_MODE_FIXED);
        status = ArSession_configure(ar_session_, ar_config);
    }
    if (status != AR_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "ArSession_configure FAILED: %d", status);
    } else {
        // Check if depth was actually enabled
        ArConfig* current_config = nullptr;
        ArConfig_create(ar_session_, &current_config);
        ArSession_getConfig(ar_session_, current_config);
        ArDepthMode depth_mode;
        ArConfig_getDepthMode(ar_session_, current_config, &depth_mode);
        depth_enabled_ = (depth_mode != AR_DEPTH_MODE_DISABLED);
        ArFocusMode focus_mode;
        ArConfig_getFocusMode(ar_session_, current_config, &focus_mode);
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch",
            "ARCore configured: update=LATEST, focus=%s, depth=%s, light_est=AMBIENT, eis=%s",
            focus_mode == AR_FOCUS_MODE_AUTO ? "AUTO" : "FIXED",
            depth_enabled_ ? "ENABLED" : "DISABLED",
            eis_supported != 0 ? "ON" : "OFF");
        ArConfig_destroy(current_config);
    }
    ArConfig_destroy(ar_config);

    if (is_torch_available_method_) {
        torch_available_ = env->CallBooleanMethod(activity_obj_, is_torch_available_method_);
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Torch availability: %s", torch_available_ ? "YES" : "NO");
    }
}

ArCoreSlam::~ArCoreSlam() {
    if (activity_obj_) {
        JNIEnv* env = nullptr;
        if (java_vm_ && java_vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
            env->DeleteGlobalRef(activity_obj_);
        }
        activity_obj_ = nullptr;
    }

    if (ar_pose_) {
        ArPose_destroy(ar_pose_);
    }
    if (ar_light_estimate_) {
        ArLightEstimate_destroy(ar_light_estimate_);
    }
    if (ar_intrinsics_) {
        ArCameraIntrinsics_destroy(ar_intrinsics_);
    }
    if (plane_list_) {
        ArTrackableList_destroy(plane_list_);
    }
    if (ar_camera_) {
        ArCamera_release(ar_camera_);
    }
    if (ar_point_cloud_) {
        ArPointCloud_release(ar_point_cloud_);
    }
    if (ar_frame_) {
        ArFrame_destroy(ar_frame_);
    }
    if (ar_session_) {
        ArSession_destroy(ar_session_);
    }
}

void ArCoreSlam::OnResume(JNIEnv* env) {
    if (ar_session_) {
        ArStatus status = ArSession_resume(ar_session_);
        if (status != AR_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "ArSession_resume FAILED: %d", status);
        } else {
            __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "ArSession resumed successfully");
        }
    }
}

void ArCoreSlam::OnPause() {
    if (ar_session_) {
        ArSession_pause(ar_session_);
    }
}

void ArCoreSlam::OnSurfaceChanged(int rotation, int width, int height) {
    UpdateDisplayGeometry(rotation, width, height);
}

void ArCoreSlam::Update(JNIEnv* env) {
    if (!ar_session_) return;

    ArStatus update_status = ArSession_update(ar_session_, ar_frame_);
    if (update_status != AR_SUCCESS) {
        static int err_count = 0;
        if (err_count++ % 60 == 0) {
            __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "ArSession_update FAILED: %d", update_status);
        }
        return;
    }

    // Release previous camera if exists
    if (ar_camera_) {
        ArCamera_release(ar_camera_);
        ar_camera_ = nullptr;
    }
    
    // Acquire camera (must release each frame)
    ArFrame_acquireCamera(ar_session_, ar_frame_, &ar_camera_);
    
    // Log camera intrinsics once (important for understanding tracking quality)
    if (!camera_intrinsics_logged_ && ar_camera_ && ar_intrinsics_) {
        ArCamera_getImageIntrinsics(ar_session_, ar_camera_, ar_intrinsics_);
        
        float fx, fy, cx, cy;
        int32_t width, height;
        ArCameraIntrinsics_getFocalLength(ar_session_, ar_intrinsics_, &fx, &fy);
        ArCameraIntrinsics_getPrincipalPoint(ar_session_, ar_intrinsics_, &cx, &cy);
        ArCameraIntrinsics_getImageDimensions(ar_session_, ar_intrinsics_, &width, &height);

        intrinsics_fx_ = fx;
        intrinsics_fy_ = fy;
        intrinsics_cx_ = cx;
        intrinsics_cy_ = cy;
        image_width_ = width;
        image_height_ = height;
        
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch",
            "Camera intrinsics: %dx%d, fx=%.1f, fy=%.1f, cx=%.1f, cy=%.1f",
            width, height, fx, fy, cx, cy);
        
        camera_intrinsics_logged_ = true;
    } else if (ar_camera_ && ar_intrinsics_) {
        ArCamera_getImageIntrinsics(ar_session_, ar_camera_, ar_intrinsics_);
        ArCameraIntrinsics_getFocalLength(ar_session_, ar_intrinsics_, &intrinsics_fx_, &intrinsics_fy_);
        ArCameraIntrinsics_getPrincipalPoint(ar_session_, ar_intrinsics_, &intrinsics_cx_, &intrinsics_cy_);
        ArCameraIntrinsics_getImageDimensions(ar_session_, ar_intrinsics_, &image_width_, &image_height_);
    }
    
    ArCamera_getTrackingState(ar_session_, ar_camera_, &tracking_state_);
    
    // Enhanced tracking diagnostics with failure reason names
    static ArTrackingState last_logged_state = AR_TRACKING_STATE_PAUSED;
    if (tracking_state_ != AR_TRACKING_STATE_TRACKING && tracking_state_ != last_logged_state) {
        ArTrackingFailureReason reason;
        ArCamera_getTrackingFailureReason(ar_session_, ar_camera_, &reason);
        const char* reason_str = "UNKNOWN";
        switch(reason) {
            case AR_TRACKING_FAILURE_REASON_NONE: reason_str = "NONE"; break;
            case AR_TRACKING_FAILURE_REASON_BAD_STATE: reason_str = "BAD_STATE"; break;
            case AR_TRACKING_FAILURE_REASON_INSUFFICIENT_LIGHT: reason_str = "LOW_LIGHT"; break;
            case AR_TRACKING_FAILURE_REASON_EXCESSIVE_MOTION: reason_str = "MOTION_BLUR"; break;
            case AR_TRACKING_FAILURE_REASON_INSUFFICIENT_FEATURES: reason_str = "NO_FEATURES"; break;
            case AR_TRACKING_FAILURE_REASON_CAMERA_UNAVAILABLE: reason_str = "CAMERA_FAIL"; break;
        }
        last_tracking_failure_reason_ = reason_str;
        __android_log_print(ANDROID_LOG_WARN, "SlamTorch", 
            "Tracking lost: state=%d, reason=%s (%d)", tracking_state_, reason_str, reason);
        last_logged_state = tracking_state_;
    } else if (tracking_state_ == AR_TRACKING_STATE_TRACKING && last_logged_state != AR_TRACKING_STATE_TRACKING) {
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Tracking acquired");
        last_logged_state = tracking_state_;
        last_tracking_failure_reason_ = "NONE";
    }

    if (tracking_state_ == AR_TRACKING_STATE_TRACKING) {
        // Get camera pose (reuse ar_pose_, no allocation)
        ArCamera_getDisplayOrientedPose(ar_session_, ar_camera_, ar_pose_);
        
        // Acquire point cloud (release previous if exists)
        if (ar_point_cloud_) {
            ArPointCloud_release(ar_point_cloud_);
            ar_point_cloud_ = nullptr;
        }
        ArFrame_acquirePointCloud(ar_session_, ar_frame_, &ar_point_cloud_);
    }

    // Day/Night Detection Logic using ARCore Light Estimation (reuse member, zero allocation)
    if (ar_light_estimate_) {
        ArFrame_getLightEstimate(ar_session_, ar_frame_, ar_light_estimate_);
        
        ArLightEstimateState state;
        ArLightEstimate_getState(ar_session_, ar_light_estimate_, &state);
        
        if (state == AR_LIGHT_ESTIMATE_STATE_VALID) {
            float pixel_intensity = 0.0f;
            ArLightEstimate_getPixelIntensity(ar_session_, ar_light_estimate_, &pixel_intensity);
            
            // Periodic exposure/light logging (every 3 seconds)
            static int light_log_counter = 0;
            if (light_log_counter++ % 180 == 0) {
                float color_correction[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                ArLightEstimate_getColorCorrection(ar_session_, ar_light_estimate_, color_correction);
                __android_log_print(ANDROID_LOG_DEBUG, "SlamTorch",
                    "Light intensity: %.3f, color_corr=[%.2f %.2f %.2f %.2f]",
                    pixel_intensity,
                    color_correction[0], color_correction[1],
                    color_correction[2], color_correction[3]);
            }
            
            UpdateTorchLogic(env, pixel_intensity);
        }
    }
}

void ArCoreSlam::UpdatePlaneList() {
    if (!ar_session_ || !plane_list_) return;
    ArSession_getAllTrackables(ar_session_, AR_TRACKABLE_PLANE, plane_list_);
}

bool ArCoreSlam::AcquireCameraImageY(uint8_t* dst, int dst_stride, int dst_capacity,
                                     int* out_width, int* out_height) {
    if (!ar_session_ || !ar_frame_) return false;

    ArImage* image = nullptr;
    ArStatus status = ArFrame_acquireCameraImage(ar_session_, ar_frame_, &image);
    if (status != AR_SUCCESS || !image) {
        return false;
    }

    int32_t width = 0;
    int32_t height = 0;
    ArImage_getWidth(ar_session_, image, &width);
    ArImage_getHeight(ar_session_, image, &height);

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;

    if (!dst || dst_capacity < (width * height) || dst_stride < width) {
        ArImage_release(image);
        return false;
    }

    const uint8_t* plane_data = nullptr;
    int32_t data_length = 0;
    int32_t row_stride = 0;
    ArImage_getPlaneData(ar_session_, image, 0, &plane_data, &data_length);
    ArImage_getPlaneRowStride(ar_session_, image, 0, &row_stride);

    if (!plane_data || row_stride <= 0) {
        ArImage_release(image);
        return false;
    }

    const uint8_t* src = plane_data;
    uint8_t* dst_row = dst;
    for (int y = 0; y < height; ++y) {
        memcpy(dst_row, src, static_cast<size_t>(width));
        src += row_stride;
        dst_row += dst_stride;
    }

    ArImage_release(image);
    return true;
}

bool ArCoreSlam::AcquireDepthFrame(DepthSource source, DepthFrame* out_frame,
                                   ArImage** out_depth_image, ArImage** out_confidence_image) {
    if (!ar_session_ || !ar_frame_ || !depth_enabled_) return false;
    if (source == DepthSource::OFF) return false;
    if (!out_frame || !out_depth_image || !out_confidence_image) return false;

    *out_depth_image = nullptr;
    *out_confidence_image = nullptr;
    *out_frame = DepthFrame{};

    ArImage* depth_image = nullptr;
    ArStatus status = (source == DepthSource::RAW)
        ? ArFrame_acquireRawDepthImage16Bits(ar_session_, ar_frame_, &depth_image)
        : ArFrame_acquireDepthImage16Bits(ar_session_, ar_frame_, &depth_image);
    if (status != AR_SUCCESS || !depth_image) {
        return false;
    }

    int32_t width = 0;
    int32_t height = 0;
    int32_t format = 0;
    int64_t timestamp = 0;
    ArImage_getWidth(ar_session_, depth_image, &width);
    ArImage_getHeight(ar_session_, depth_image, &height);
    ArImage_getFormat(ar_session_, depth_image, &format);
    ArImage_getTimestamp(ar_session_, depth_image, &timestamp);

    const uint8_t* plane_data = nullptr;
    int32_t data_length = 0;
    int32_t row_stride = 0;
    int32_t pixel_stride = 0;
    ArImage_getPlaneData(ar_session_, depth_image, 0, &plane_data, &data_length);
    ArImage_getPlaneRowStride(ar_session_, depth_image, 0, &row_stride);
    ArImage_getPlanePixelStride(ar_session_, depth_image, 0, &pixel_stride);

    out_frame->depth_data = reinterpret_cast<const uint16_t*>(plane_data);
    out_frame->width = width;
    out_frame->height = height;
    out_frame->row_stride = row_stride;
    out_frame->pixel_stride = pixel_stride;
    out_frame->format = format;
    out_frame->timestamp_ns = timestamp;
    out_frame->is_raw = (source == DepthSource::RAW);
    *out_depth_image = depth_image;

    if (source == DepthSource::RAW) {
        ArImage* confidence_image = nullptr;
        ArStatus conf_status = ArFrame_acquireRawDepthConfidenceImage(ar_session_, ar_frame_, &confidence_image);
        if (conf_status == AR_SUCCESS && confidence_image) {
            int32_t conf_format = 0;
            ArImage_getFormat(ar_session_, confidence_image, &conf_format);
            const uint8_t* conf_plane_data = nullptr;
            int32_t conf_length = 0;
            int32_t conf_row_stride = 0;
            int32_t conf_pixel_stride = 0;
            ArImage_getPlaneData(ar_session_, confidence_image, 0, &conf_plane_data, &conf_length);
            ArImage_getPlaneRowStride(ar_session_, confidence_image, 0, &conf_row_stride);
            ArImage_getPlanePixelStride(ar_session_, confidence_image, 0, &conf_pixel_stride);

            out_frame->confidence_data = conf_plane_data;
            out_frame->confidence_row_stride = conf_row_stride;
            out_frame->confidence_pixel_stride = conf_pixel_stride;
            out_frame->confidence_format = conf_format;
            *out_confidence_image = confidence_image;
        }
    }

    return true;
}

void ArCoreSlam::ReleaseDepthImage(ArImage* image) {
    if (image) {
        ArImage_release(image);
    }
}

void ArCoreSlam::GetImageDimensions(int* out_width, int* out_height) const {
    if (out_width) *out_width = image_width_;
    if (out_height) *out_height = image_height_;
}

void ArCoreSlam::GetCameraIntrinsics(float* out_fx, float* out_fy, float* out_cx, float* out_cy) const {
    if (out_fx) *out_fx = intrinsics_fx_;
    if (out_fy) *out_fy = intrinsics_fy_;
    if (out_cx) *out_cx = intrinsics_cx_;
    if (out_cy) *out_cy = intrinsics_cy_;
}

void ArCoreSlam::GetViewMatrix(float* out_matrix) const {
    if (!ar_camera_ || tracking_state_ != AR_TRACKING_STATE_TRACKING) {
        // Identity matrix if not tracking
        for (int i = 0; i < 16; ++i) out_matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        return;
    }
    ArCamera_getViewMatrix(ar_session_, ar_camera_, out_matrix);
}

void ArCoreSlam::GetProjectionMatrix(float near, float far, float* out_matrix) const {
    if (!ar_camera_) {
        // Identity matrix if no camera
        for (int i = 0; i < 16; ++i) out_matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        return;
    }
    ArCamera_getProjectionMatrix(ar_session_, ar_camera_, near, far, out_matrix);
}

void ArCoreSlam::GetWorldFromCameraMatrix(float* out_matrix) const {
    if (!ar_camera_ || tracking_state_ != AR_TRACKING_STATE_TRACKING) {
        // Identity if not tracking
        for (int i = 0; i < 16; ++i) out_matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        return;
    }
    
    // Get camera pose in world space (ArPose is world-from-camera)
    float pose_matrix[16];
    ArCamera_getDisplayOrientedPose(ar_session_, ar_camera_, ar_pose_);
    ArPose_getMatrix(ar_session_, ar_pose_, pose_matrix);
    
    // Copy to output (pose matrix is already world-from-camera)
    for (int i = 0; i < 16; ++i) {
        out_matrix[i] = pose_matrix[i];
    }
}

void ArCoreSlam::UpdateTorchLogic(JNIEnv* env, float light_intensity) {
    if (!torch_available_) {
        return;
    }

    bool target_state = current_torch_state_;
    int required_frames = 1;

    if (torch_mode_ == TorchMode::MANUAL_ON) {
        target_state = true;
    } else if (torch_mode_ == TorchMode::MANUAL_OFF) {
        target_state = false;
    } else {
        // AUTO Mode with Hysteresis
        // Intensity is [0.0 (dark) to 1.0 (bright)].
        // Low light threshold: 0.2
        // High light threshold: 0.4
        if (light_intensity < 0.2f) {
            target_state = true; 
        } else if (light_intensity > 0.4f) {
            target_state = false;
        }
        required_frames = 15;
    }

    if (target_state != current_torch_state_) {
        if (pending_torch_state_ != target_state) {
            pending_torch_state_ = target_state;
            torch_pending_frames_ = 0;
        }
        torch_pending_frames_++;
        if (torch_pending_frames_ >= required_frames) {
            current_torch_state_ = target_state;
            torch_pending_frames_ = 0;
            CallJavaSetTorch(env, current_torch_state_);
        }
    } else {
        torch_pending_frames_ = 0;
        pending_torch_state_ = current_torch_state_;
    }
}

void ArCoreSlam::CallJavaSetTorch(JNIEnv* env, bool enabled) {
    if (env && activity_obj_ && set_torch_method_ && torch_available_) {
        env->CallVoidMethod(activity_obj_, set_torch_method_, (jboolean)enabled);
    }
}

void ArCoreSlam::UpdateDisplayGeometry(int rotation, int width, int height) {
    if (!ar_session_ || width <= 0 || height <= 0) {
        return;
    }

    if (rotation != display_rotation_ || width != ar_frame_width_ || height != ar_frame_height_) {
        display_rotation_ = rotation;
        ar_frame_width_ = width;
        ar_frame_height_ = height;
        ArSession_setDisplayGeometry(ar_session_, rotation, width, height);
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Display geometry set: rot=%d, %dx%d", rotation, width, height);
    }
}
