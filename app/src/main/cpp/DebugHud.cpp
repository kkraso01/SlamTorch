#include "DebugHud.h"

void DebugHud::Update(const ArCoreSlam* slam,
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
                      float depth_mesh_valid_ratio) {
    data_.point_count = point_count;
    data_.map_points = map_points;
    data_.bearing_landmarks = bearing_landmarks;
    data_.metric_landmarks = metric_landmarks;
    data_.tracked_features = tracked_features;
    data_.stable_tracks = stable_tracks;
    data_.avg_track_age = avg_track_age;
    data_.depth_hit_rate = depth_hit_rate;
    data_.fps = fps;
    data_.depth_supported = depth_supported;
    data_.depth_mode = depth_mode;
    data_.depth_width = depth_width;
    data_.depth_height = depth_height;
    data_.depth_min_m = depth_min_m;
    data_.depth_max_m = depth_max_m;
    data_.voxels_used = voxels_used;
    data_.points_fused_per_second = points_fused_per_second;
    data_.map_enabled = map_enabled;
    data_.depth_overlay_enabled = depth_overlay_enabled;
    data_.planes_enabled = planes_enabled;
    data_.depth_mesh_mode = depth_mesh_mode;
    data_.depth_mesh_wireframe = depth_mesh_wireframe;
    data_.depth_mesh_width = depth_mesh_width;
    data_.depth_mesh_height = depth_mesh_height;
    data_.depth_mesh_valid_ratio = depth_mesh_valid_ratio;
    data_.tracking_state = "NONE";
    data_.torch_mode = "NONE";
    data_.last_failure_reason = "NONE";
    data_.torch_enabled = false;
    data_.depth_enabled = false;

    if (!slam) return;

    const ArTrackingState state = slam->GetTrackingState();
    data_.tracking_state = (state == AR_TRACKING_STATE_TRACKING) ? "TRACKING" :
                           (state == AR_TRACKING_STATE_PAUSED) ? "PAUSED" : "STOPPED";
    data_.last_failure_reason = slam->GetLastTrackingFailureReason();

    auto mode = slam->GetTorchMode();
    data_.torch_mode = (mode == ArCoreSlam::TorchMode::AUTO) ? "AUTO" :
                       (mode == ArCoreSlam::TorchMode::MANUAL_ON) ? "ON" : "OFF";
    data_.torch_enabled = slam->IsTorchOn();
    data_.depth_enabled = slam->IsDepthEnabled();
    data_.depth_supported = slam->IsDepthSupported();
}
