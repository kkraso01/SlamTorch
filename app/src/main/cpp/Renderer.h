#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <memory>
#include <cstdint>
#include <jni.h>
#include <vector>

#include "ArCoreSlam.h"
#include "BackgroundRenderer.h"
#include "DebugHud.h"
#include "DepthMapper.h"
#include "DepthOverlayRenderer.h"
#include "DepthMeshRenderer.h"
#include "LandmarkMap.h"
#include "OpticalFlowTracker.h"
#include "PlaneRenderer.h"
#include "PointCloudRenderer.h"
#include "VoxelMapRenderer.h"

struct android_app;

// Debug statistics structure
struct DebugStats {
    const char* tracking_state;
    int point_count;
    int map_points;
    int bearing_landmarks;
    int metric_landmarks;
    int tracked_features;
    int stable_tracks;
    float avg_track_age;
    float depth_hit_rate;
    float fps;
    const char* torch_mode;
    bool torch_enabled;
    bool depth_enabled;
    bool depth_supported;
    const char* depth_mode;
    int depth_width;
    int depth_height;
    float depth_min_m;
    float depth_max_m;
    int voxels_used;
    int points_fused_per_second;
    bool map_enabled;
    bool depth_overlay_enabled;
    const char* last_failure_reason;
    bool planes_enabled;
    const char* depth_mesh_mode;
    bool depth_mesh_wireframe;
    int depth_mesh_width;
    int depth_mesh_height;
    float depth_mesh_valid_ratio;
};

class Renderer {
public:
    Renderer(android_app *pApp);
    virtual ~Renderer();

    void handleInput();
    void render();

    // Lifecycle helpers
    void OnPause();
    void OnResume();
    
    // UI integration
    void UpdateRotation(int display_rotation);
    void ClearPersistentMap();
    void CycleTorchMode();
    void SetTorchMode(ArCoreSlam::TorchMode mode);
    void SetDepthMode(ArCoreSlam::DepthSource mode);
    void SetMapEnabled(bool enabled);
    void SetDebugOverlayEnabled(bool enabled);
    void SetPlanesEnabled(bool enabled);
    void SetDepthMeshMode(ArCoreSlam::DepthSource mode);
    void SetDepthMeshWireframe(bool enabled);
    void ClearDepthMesh();
    DebugStats GetDebugStats() const;

private:
    void initRenderer();
    void updateRenderArea();
    void createModels();

    android_app *app_;
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    EGLint width_;
    EGLint height_;

    bool shaderNeedsNewProjectionMatrix_;

    // ARCore SLAM and Rendering
    std::unique_ptr<ArCoreSlam> ar_slam_;
    std::unique_ptr<BackgroundRenderer> background_renderer_;
    std::unique_ptr<DepthOverlayRenderer> depth_overlay_renderer_;
    std::unique_ptr<DepthMeshRenderer> depth_mesh_renderer_;
    std::unique_ptr<PointCloudRenderer> point_cloud_renderer_;
    std::unique_ptr<LandmarkMap> landmark_map_;
    std::unique_ptr<OpticalFlowTracker> optical_flow_;
    std::unique_ptr<DebugHud> debug_hud_;
    std::unique_ptr<DepthMapper> depth_mapper_;
    std::unique_ptr<PlaneRenderer> plane_renderer_;
    std::unique_ptr<VoxelMapRenderer> voxel_map_renderer_;
    
    // JNI cached (attach once, not per-frame)
    JNIEnv* env_ = nullptr;
    bool jni_attached_ = false;
    
    // Preallocated matrices (no per-frame allocation)
    float view_matrix_[16];
    float projection_matrix_[16];
    float last_good_view_[16];  // For rendering map when not tracking
    float last_good_proj_[16];
    float last_good_world_from_camera_[16];
    bool has_good_matrices_ = false;
    
    // FPS tracking
    int frame_count_ = 0;
    float last_fps_ = 0.0f;
    double fps_last_time_ = 0.0;
    
    // Current rotation
    int display_rotation_ = 0;
    
    // Stats tracking
    int current_point_count_ = 0;
    int current_feature_count_ = 0;
    int current_stable_track_count_ = 0;
    float current_avg_track_age_ = 0.0f;
    float current_depth_hit_rate_ = 0.0f;
    int current_bearing_landmarks_ = 0;
    int current_metric_landmarks_ = 0;
    int current_depth_width_ = 0;
    int current_depth_height_ = 0;
    float current_depth_min_m_ = 0.0f;
    float current_depth_max_m_ = 0.0f;
    int current_voxels_used_ = 0;
    int current_points_fused_per_second_ = 0;
    int points_fused_accumulator_ = 0;
    double points_fused_last_time_ = 0.0;
    bool map_enabled_ = true;
    bool debug_overlay_enabled_ = false;
    ArCoreSlam::DepthSource depth_source_ = ArCoreSlam::DepthSource::DEPTH;
    bool planes_enabled_ = true;
    bool depth_mesh_wireframe_ = false;
    ArCoreSlam::DepthSource depth_mesh_mode_ = ArCoreSlam::DepthSource::OFF;
    int depth_mesh_width_ = 0;
    int depth_mesh_height_ = 0;
    float depth_mesh_valid_ratio_ = 0.0f;

    // CPU image buffer (Y plane)
    uint8_t* camera_image_buffer_ = nullptr;
    int camera_image_capacity_ = 0;
    int camera_image_stride_ = 0;

    // Depth debug buffer (grayscale)
    std::vector<uint8_t> depth_debug_buffer_;
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H
