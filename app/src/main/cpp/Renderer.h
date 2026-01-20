#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <memory>
#include <string>
#include <jni.h>

#include "ArCoreSlam.h"
#include "BackgroundRenderer.h"
#include "PointCloudRenderer.h"
#include "PersistentPointMap.h"

struct android_app;

// Debug statistics structure
struct DebugStats {
    std::string tracking_state;
    int point_count;
    int map_points;
    float fps;
    std::string torch_mode;
    bool depth_enabled;
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
    void UpdateRotation(int rotation_degrees);
    void ClearPersistentMap();
    void CycleTorchMode();
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
    std::unique_ptr<PointCloudRenderer> point_cloud_renderer_;
    std::unique_ptr<PersistentPointMap> persistent_map_;
    
    // JNI cached (attach once, not per-frame)
    JNIEnv* env_ = nullptr;
    bool jni_attached_ = false;
    
    // Preallocated matrices (no per-frame allocation)
    float view_matrix_[16];
    float projection_matrix_[16];
    float last_good_view_[16];  // For rendering map when not tracking
    float last_good_proj_[16];
    bool has_good_matrices_ = false;
    
    // FPS tracking
    int frame_count_ = 0;
    float last_fps_ = 0.0f;
    double fps_last_time_ = 0.0;
    
    // Current rotation
    int display_rotation_ = 0;
    
    // Stats tracking
    int current_point_count_ = 0;
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H
