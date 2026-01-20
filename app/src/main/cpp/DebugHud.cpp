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
                      float fps) {
    data_.point_count = point_count;
    data_.map_points = map_points;
    data_.bearing_landmarks = bearing_landmarks;
    data_.metric_landmarks = metric_landmarks;
    data_.tracked_features = tracked_features;
    data_.stable_tracks = stable_tracks;
    data_.avg_track_age = avg_track_age;
    data_.depth_hit_rate = depth_hit_rate;
    data_.fps = fps;
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
}
