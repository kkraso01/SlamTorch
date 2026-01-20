#include "DepthMapper.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int kVoxelCount = DepthMapper::kGridDim * DepthMapper::kGridDim * DepthMapper::kGridDim;
constexpr float kHalfExtent = DepthMapper::kGridDim * DepthMapper::kVoxelSize * 0.5f;
constexpr float kRecenterDistance = kHalfExtent * 0.35f;
}

DepthMapper::DepthMapper()
    : occupancy_(kVoxelCount, 0),
      render_points_(kVoxelCount * 3, 0.0f) {}

void DepthMapper::Reset() {
    std::fill(occupancy_.begin(), occupancy_.end(), 0);
    voxels_used_ = 0;
    render_point_count_ = 0;
    render_dirty_ = true;
    stats_ = Stats{};
    origin_set_ = false;
}

void DepthMapper::RecenterIfNeeded(const float* world_from_camera) {
    if (!world_from_camera) return;
    const float cam_x = world_from_camera[12];
    const float cam_y = world_from_camera[13];
    const float cam_z = world_from_camera[14];
    if (!origin_set_) {
        origin_[0] = cam_x;
        origin_[1] = cam_y;
        origin_[2] = cam_z;
        origin_set_ = true;
        return;
    }

    const float dx = cam_x - origin_[0];
    const float dy = cam_y - origin_[1];
    const float dz = cam_z - origin_[2];
    if (std::fabs(dx) > kRecenterDistance ||
        std::fabs(dy) > kRecenterDistance ||
        std::fabs(dz) > kRecenterDistance) {
        origin_[0] = cam_x;
        origin_[1] = cam_y;
        origin_[2] = cam_z;
        std::fill(occupancy_.begin(), occupancy_.end(), 0);
        voxels_used_ = 0;
        render_dirty_ = true;
    }
}

void DepthMapper::Update(const DepthFrame& frame,
                         float fx, float fy, float cx, float cy,
                         int image_width, int image_height,
                         const float* world_from_camera) {
    stats_.points_fused_last_frame = 0;
    stats_.min_depth_m = 0.0f;
    stats_.max_depth_m = 0.0f;
    stats_.voxels_used = voxels_used_;

    if (!enabled_ || !frame.depth_data || !world_from_camera) {
        return;
    }

    RecenterIfNeeded(world_from_camera);
    if (!origin_set_) return;

    const float scale_x = (image_width > 0) ? (static_cast<float>(frame.width) / static_cast<float>(image_width)) : 1.0f;
    const float scale_y = (image_height > 0) ? (static_cast<float>(frame.height) / static_cast<float>(image_height)) : 1.0f;
    const float fx_depth = fx * scale_x;
    const float fy_depth = fy * scale_y;
    const float cx_depth = cx * scale_x;
    const float cy_depth = cy * scale_y;

    const int stride = 4;
    float min_depth = 0.0f;
    float max_depth = 0.0f;

    for (int y = 0; y < frame.height; y += stride) {
        const uint8_t* row = reinterpret_cast<const uint8_t*>(frame.depth_data) + frame.row_stride * y;
        const uint8_t* conf_row = frame.confidence_data
            ? (reinterpret_cast<const uint8_t*>(frame.confidence_data) + frame.confidence_row_stride * y)
            : nullptr;
        for (int x = 0; x < frame.width; x += stride) {
            const uint16_t* depth_pixel = reinterpret_cast<const uint16_t*>(row + frame.pixel_stride * x);
            const uint16_t depth_mm = *depth_pixel;
            if (depth_mm == 0) continue;
            const float depth_m = static_cast<float>(depth_mm) * 0.001f;
            if (depth_m < kMinDepthM || depth_m > kMaxDepthM) continue;

            if (conf_row && frame.confidence_pixel_stride > 0) {
                const uint8_t confidence = *(conf_row + frame.confidence_pixel_stride * x);
                if (confidence < kConfidenceThreshold) continue;
            }

            if (min_depth == 0.0f || depth_m < min_depth) min_depth = depth_m;
            if (depth_m > max_depth) max_depth = depth_m;

            const float x_cam = (static_cast<float>(x) - cx_depth) * depth_m / fx_depth;
            const float y_cam = (static_cast<float>(y) - cy_depth) * depth_m / fy_depth;
            const float z_cam = -depth_m;

            const float world_x = world_from_camera[0] * x_cam +
                                  world_from_camera[4] * y_cam +
                                  world_from_camera[8] * z_cam +
                                  world_from_camera[12];
            const float world_y = world_from_camera[1] * x_cam +
                                  world_from_camera[5] * y_cam +
                                  world_from_camera[9] * z_cam +
                                  world_from_camera[13];
            const float world_z = world_from_camera[2] * x_cam +
                                  world_from_camera[6] * y_cam +
                                  world_from_camera[10] * z_cam +
                                  world_from_camera[14];

            const float local_x = world_x - origin_[0] + kHalfExtent;
            const float local_y = world_y - origin_[1] + kHalfExtent;
            const float local_z = world_z - origin_[2] + kHalfExtent;
            const int gx = static_cast<int>(local_x / kVoxelSize);
            const int gy = static_cast<int>(local_y / kVoxelSize);
            const int gz = static_cast<int>(local_z / kVoxelSize);
            if (gx < 0 || gy < 0 || gz < 0 || gx >= kGridDim || gy >= kGridDim || gz >= kGridDim) {
                continue;
            }

            const int idx = gx + (gy * kGridDim) + (gz * kGridDim * kGridDim);
            if (occupancy_[idx] == 0) {
                voxels_used_++;
            }
            const int next = std::min<int>(kOccupancyMax, occupancy_[idx] + kOccupancyIncrement);
            occupancy_[idx] = static_cast<uint8_t>(next);
            stats_.points_fused_last_frame++;
            render_dirty_ = true;
        }
    }

    stats_.voxels_used = voxels_used_;
    stats_.min_depth_m = min_depth;
    stats_.max_depth_m = max_depth;
}

void DepthMapper::RebuildRenderPoints() {
    render_point_count_ = 0;
    for (int z = 0; z < kGridDim; ++z) {
        for (int y = 0; y < kGridDim; ++y) {
            for (int x = 0; x < kGridDim; ++x) {
                const int idx = x + (y * kGridDim) + (z * kGridDim * kGridDim);
                if (occupancy_[idx] == 0) continue;
                const float world_x = origin_[0] + (static_cast<float>(x) + 0.5f) * kVoxelSize - kHalfExtent;
                const float world_y = origin_[1] + (static_cast<float>(y) + 0.5f) * kVoxelSize - kHalfExtent;
                const float world_z = origin_[2] + (static_cast<float>(z) + 0.5f) * kVoxelSize - kHalfExtent;
                const int out_idx = render_point_count_ * 3;
                render_points_[out_idx + 0] = world_x;
                render_points_[out_idx + 1] = world_y;
                render_points_[out_idx + 2] = world_z;
                render_point_count_++;
            }
        }
    }
    render_dirty_ = false;
}

const float* DepthMapper::GetRenderPoints(int* out_count, bool* out_dirty) {
    const bool was_dirty = render_dirty_;
    if (render_dirty_) {
        RebuildRenderPoints();
    }
    if (out_count) *out_count = render_point_count_;
    if (out_dirty) *out_dirty = was_dirty;
    return render_points_.data();
}
