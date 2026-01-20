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
                float fps);
    const DebugHudData& GetData() const { return data_; }

private:
    DebugHudData data_;
};

#endif // SLAMTORCH_DEBUG_HUD_H
