#ifndef SLAMTORCH_DEBUG_HUD_H
#define SLAMTORCH_DEBUG_HUD_H

#include "ArCoreSlam.h"

struct DebugHudData {
    const char* tracking_state = "NONE";
    const char* torch_mode = "NONE";
    const char* last_failure_reason = "NONE";
    int point_count = 0;
    int map_points = 0;
    int bearing_landmarks = 0;
    int metric_landmarks = 0;
    int tracked_features = 0;
    int stable_tracks = 0;
    float avg_track_age = 0.0f;
    float depth_hit_rate = 0.0f;
    float fps = 0.0f;
    bool torch_enabled = false;
    bool depth_enabled = false;
    bool depth_supported = false;
    const char* depth_mode = "OFF";
    int depth_width = 0;
    int depth_height = 0;
    float depth_min_m = 0.0f;
    float depth_max_m = 0.0f;
    int voxels_used = 0;
    int points_fused_per_second = 0;
    bool map_enabled = false;
    bool depth_overlay_enabled = false;
    bool planes_enabled = false;
    const char* depth_mesh_mode = "OFF";
    bool depth_mesh_wireframe = false;
    int depth_mesh_width = 0;
    int depth_mesh_height = 0;
    float depth_mesh_valid_ratio = 0.0f;
};

class DebugHud {
public:
    void Update(const ArCoreSlam* slam,
                int point_count,
                int map_points,
                int bearing_landmarks,
                int metric_landmarks,
                int tracked_features,
                int stable_tracks,
                float avg_track_age,
                float depth_hit_rate,
                float fps,
                bool depth_supported,
                const char* depth_mode,
                int depth_width,
                int depth_height,
                float depth_min_m,
                float depth_max_m,
                int voxels_used,
                int points_fused_per_second,
                bool map_enabled,
                bool depth_overlay_enabled,
                bool planes_enabled,
                const char* depth_mesh_mode,
                bool depth_mesh_wireframe,
                int depth_mesh_width,
                int depth_mesh_height,
                float depth_mesh_valid_ratio);
    const DebugHudData& GetData() const { return data_; }

private:
    DebugHudData data_;
};

#endif // SLAMTORCH_DEBUG_HUD_H
