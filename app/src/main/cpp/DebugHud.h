#ifndef SLAMTORCH_DEBUG_HUD_H
#define SLAMTORCH_DEBUG_HUD_H

#include "ArCoreSlam.h"

struct DebugHudData {
    const char* tracking_state = "NONE";
    const char* torch_mode = "NONE";
    const char* last_failure_reason = "NONE";
    int point_count = 0;
    int map_points = 0;
    float fps = 0.0f;
    bool torch_enabled = false;
    bool depth_enabled = false;
};

class DebugHud {
public:
    void Update(const ArCoreSlam* slam,
                int point_count,
                int map_points,
                float fps);
    const DebugHudData& GetData() const { return data_; }

private:
    DebugHudData data_;
};

#endif // SLAMTORCH_DEBUG_HUD_H
