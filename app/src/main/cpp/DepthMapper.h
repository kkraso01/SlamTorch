#ifndef SLAMTORCH_DEPTH_MAPPER_H
#define SLAMTORCH_DEPTH_MAPPER_H

#include "DepthFrame.h"
#include <cstdint>
#include <vector>

class DepthMapper {
public:
    struct Stats {
        int voxels_used = 0;
        int points_fused_last_frame = 0;
        float min_depth_m = 0.0f;
        float max_depth_m = 0.0f;
    };

    static constexpr int kGridDim = 96;
    static constexpr float kVoxelSize = 0.10f;

    DepthMapper();

    void Reset();
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    void Update(const DepthFrame& frame,
                float fx, float fy, float cx, float cy,
                int image_width, int image_height,
                const float* world_from_camera);

    const float* GetRenderPoints(int* out_count, bool* out_dirty);
    const Stats& GetStats() const { return stats_; }

private:
    void RecenterIfNeeded(const float* world_from_camera);
    void RebuildRenderPoints();
    static constexpr float kMinDepthM = 0.2f;
    static constexpr float kMaxDepthM = 6.0f;
    static constexpr uint8_t kOccupancyIncrement = 8;
    static constexpr uint8_t kOccupancyMax = 255;
    static constexpr int kConfidenceThreshold = 128;

    bool enabled_ = true;
    bool origin_set_ = false;
    float origin_[3] = {0.0f, 0.0f, 0.0f};
    int voxels_used_ = 0;
    bool render_dirty_ = false;

    std::vector<uint8_t> occupancy_;
    std::vector<float> render_points_;
    int render_point_count_ = 0;

    Stats stats_;
};

#endif // SLAMTORCH_DEPTH_MAPPER_H
