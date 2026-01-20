#include "OpticalFlowTracker.h"
#include "AndroidOut.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int kWindowRadius = 2;
constexpr int kWindowSize = kWindowRadius * 2 + 1;
constexpr int kIterations = 6;
constexpr float kMinDet = 1e-4f;
constexpr float kMaxError = 20.0f;
constexpr int kGridSize = 24;
constexpr int kMinBorder = 6;
constexpr float kGradThresh = 18.0f;
}

OpticalFlowTracker::OpticalFlowTracker(int max_features, int pyramid_levels)
    : max_features_(max_features),
      pyramid_levels_(pyramid_levels),
      reseed_threshold_(max_features / 2) {
    tracks_ = new Track[max_features_];
    level_widths_ = new int[pyramid_levels_];
    level_heights_ = new int[pyramid_levels_];
    pyramid_prev_ = new uint8_t*[pyramid_levels_];
    pyramid_curr_ = new uint8_t*[pyramid_levels_];
    memset(pyramid_prev_, 0, sizeof(uint8_t*) * pyramid_levels_);
    memset(pyramid_curr_, 0, sizeof(uint8_t*) * pyramid_levels_);
}

OpticalFlowTracker::~OpticalFlowTracker() {
    for (int i = 0; i < pyramid_levels_; ++i) {
        delete[] pyramid_prev_[i];
        delete[] pyramid_curr_[i];
    }
    delete[] pyramid_prev_;
    delete[] pyramid_curr_;
    delete[] level_widths_;
    delete[] level_heights_;
    delete[] tracks_;
}

void OpticalFlowTracker::Reset() {
    track_count_ = 0;
    has_prev_ = false;
    for (int i = 0; i < max_features_; ++i) {
        tracks_[i] = Track();
    }
}

void OpticalFlowTracker::Initialize(int width, int height) {
    if (width == width_ && height == height_ && pyramid_prev_[0]) {
        return;
    }
    width_ = width;
    height_ = height;
    AllocatePyramids();
    Reset();
    aout << "OpticalFlowTracker initialized: " << width_ << "x" << height_ << std::endl;
}

void OpticalFlowTracker::AllocatePyramids() {
    int w = width_;
    int h = height_;
    for (int level = 0; level < pyramid_levels_; ++level) {
        level_widths_[level] = w;
        level_heights_[level] = h;
        delete[] pyramid_prev_[level];
        delete[] pyramid_curr_[level];
        pyramid_prev_[level] = new uint8_t[w * h];
        pyramid_curr_[level] = new uint8_t[w * h];
        memset(pyramid_prev_[level], 0, static_cast<size_t>(w * h));
        memset(pyramid_curr_[level], 0, static_cast<size_t>(w * h));
        w = (w + 1) / 2;
        h = (h + 1) / 2;
    }
}

void OpticalFlowTracker::BuildPyramid(const uint8_t* src, uint8_t** pyramid) {
    if (!src || !pyramid[0]) return;
    const int base_size = width_ * height_;
    memcpy(pyramid[0], src, static_cast<size_t>(base_size));

    for (int level = 1; level < pyramid_levels_; ++level) {
        const int prev_w = level_widths_[level - 1];
        const int prev_h = level_heights_[level - 1];
        const int w = level_widths_[level];
        const int h = level_heights_[level];
        const uint8_t* prev = pyramid[level - 1];
        uint8_t* curr = pyramid[level];

        for (int y = 0; y < h; ++y) {
            const int src_y = y * 2;
            const int src_y1 = (src_y + 1 < prev_h) ? (src_y + 1) : src_y;
            for (int x = 0; x < w; ++x) {
                const int src_x = x * 2;
                const int src_x1 = (src_x + 1 < prev_w) ? (src_x + 1) : src_x;
                int sum = 0;
                sum += prev[src_y * prev_w + src_x];
                sum += prev[src_y * prev_w + src_x1];
                sum += prev[src_y1 * prev_w + src_x];
                sum += prev[src_y1 * prev_w + src_x1];
                curr[y * w + x] = static_cast<uint8_t>(sum / 4);
            }
        }
    }
}

void OpticalFlowTracker::SwapPyramids() {
    uint8_t** temp = pyramid_prev_;
    pyramid_prev_ = pyramid_curr_;
    pyramid_curr_ = temp;
    has_prev_ = true;
}

bool OpticalFlowTracker::Update(const uint8_t* image, int width, int height) {
    if (!image) return false;
    if (width <= 0 || height <= 0) return false;
    if (width != width_ || height != height_) {
        Initialize(width, height);
    }

    BuildPyramid(image, pyramid_curr_);

    if (!has_prev_) {
        DetectFeatures(pyramid_curr_[0]);
        SwapPyramids();
        return true;
    }

    int active_count = 0;
    for (int i = 0; i < track_count_; ++i) {
        if (!tracks_[i].active) continue;
        if (TrackFeature(i)) {
            tracks_[i].age++;
            tracks_[i].stable_count++;
            active_count++;
        } else {
            tracks_[i].active = false;
            tracks_[i].stable_count = 0;
        }
    }

    if (active_count < reseed_threshold_) {
        DetectFeatures(pyramid_curr_[0]);
    }

    SwapPyramids();
    return true;
}

void OpticalFlowTracker::DetectFeatures(const uint8_t* image) {
    track_count_ = 0;
    const int w = width_;
    const int h = height_;
    const int grid_x = (w - kMinBorder) / kGridSize;
    const int grid_y = (h - kMinBorder) / kGridSize;

    for (int gy = 0; gy <= grid_y; ++gy) {
        for (int gx = 0; gx <= grid_x; ++gx) {
            float best_score = 0.0f;
            int best_x = -1;
            int best_y = -1;
            const int start_x = kMinBorder + gx * kGridSize;
            const int start_y = kMinBorder + gy * kGridSize;
            const int end_x = std::min(start_x + kGridSize, w - kMinBorder);
            const int end_y = std::min(start_y + kGridSize, h - kMinBorder);

            for (int y = start_y; y < end_y; y += 2) {
                for (int x = start_x; x < end_x; x += 2) {
                    const int idx = y * w + x;
                    const uint8_t intensity = image[idx];
                    if (intensity < 15 || intensity > 240) {
                        continue;
                    }
                    const float ix = 0.5f * (image[idx + 1] - image[idx - 1]);
                    const float iy = 0.5f * (image[idx + w] - image[idx - w]);
                    const float score = ix * ix + iy * iy;
                    if (score > best_score && score > kGradThresh) {
                        best_score = score;
                        best_x = x;
                        best_y = y;
                    }
                }
            }

            if (best_x >= 0 && track_count_ < max_features_) {
                Track& t = tracks_[track_count_++];
                t.x = static_cast<float>(best_x);
                t.y = static_cast<float>(best_y);
                t.prev_x = t.x;
                t.prev_y = t.y;
                t.age = 1;
                t.stable_count = 1;
                t.active = true;
                t.error = 0.0f;
            }
        }
    }
}

bool OpticalFlowTracker::TrackFeature(int track_index) {
    Track& t = tracks_[track_index];
    float x = t.x;
    float y = t.y;
    float error = 0.0f;

    for (int level = pyramid_levels_ - 1; level >= 0; --level) {
        const float scale = 1.0f / static_cast<float>(1 << level);
        float lx = x * scale;
        float ly = y * scale;
        float out_x = lx;
        float out_y = ly;

        error = TrackFeatureAtLevel(level, lx, ly, &out_x, &out_y);
        if (error > kMaxError) {
            return false;
        }

        x = out_x / scale;
        y = out_y / scale;
    }

    if (x < kMinBorder || y < kMinBorder || x >= width_ - kMinBorder || y >= height_ - kMinBorder) {
        return false;
    }

    t.prev_x = t.x;
    t.prev_y = t.y;
    t.x = x;
    t.y = y;
    t.error = error;
    return true;
}

float OpticalFlowTracker::TrackFeatureAtLevel(int level, float x, float y, float* out_x, float* out_y) {
    const uint8_t* prev = pyramid_prev_[level];
    const uint8_t* curr = pyramid_curr_[level];
    const int w = level_widths_[level];
    const int h = level_heights_[level];
    float dx = 0.0f;
    float dy = 0.0f;

    for (int iter = 0; iter < kIterations; ++iter) {
        float sum_ix2 = 0.0f;
        float sum_iy2 = 0.0f;
        float sum_ixiy = 0.0f;
        float sum_ixit = 0.0f;
        float sum_iyit = 0.0f;

        for (int wy = -kWindowRadius; wy <= kWindowRadius; ++wy) {
            for (int wx = -kWindowRadius; wx <= kWindowRadius; ++wx) {
                const float px = x + static_cast<float>(wx);
                const float py = y + static_cast<float>(wy);
                const float qx = px + dx;
                const float qy = py + dy;

                if (px < 1.0f || py < 1.0f || px >= w - 1.0f || py >= h - 1.0f) {
                    continue;
                }
                if (qx < 1.0f || qy < 1.0f || qx >= w - 1.0f || qy >= h - 1.0f) {
                    continue;
                }

                const float prev_val = SampleBilinear(prev, w, h, px, py);
                const float curr_val = SampleBilinear(curr, w, h, qx, qy);

                const float ix = 0.5f * (SampleBilinear(prev, w, h, px + 1.0f, py) -
                                         SampleBilinear(prev, w, h, px - 1.0f, py));
                const float iy = 0.5f * (SampleBilinear(prev, w, h, px, py + 1.0f) -
                                         SampleBilinear(prev, w, h, px, py - 1.0f));

                const float it = curr_val - prev_val;

                sum_ix2 += ix * ix;
                sum_iy2 += iy * iy;
                sum_ixiy += ix * iy;
                sum_ixit += ix * it;
                sum_iyit += iy * it;
            }
        }

        const float det = sum_ix2 * sum_iy2 - sum_ixiy * sum_ixiy;
        if (det < kMinDet) {
            return kMaxError + 1.0f;
        }

        const float inv_det = 1.0f / det;
        const float delta_x = (-sum_iy2 * sum_ixit + sum_ixiy * sum_iyit) * inv_det;
        const float delta_y = (sum_ixiy * sum_ixit - sum_ix2 * sum_iyit) * inv_det;

        dx += delta_x;
        dy += delta_y;

        if (delta_x * delta_x + delta_y * delta_y < 1e-4f) {
            break;
        }
    }

    *out_x = x + dx;
    *out_y = y + dy;
    return std::sqrt(dx * dx + dy * dy);
}

float OpticalFlowTracker::SampleBilinear(const uint8_t* image, int width, int height, float x, float y) const {
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = (x0 + 1 < width) ? x0 + 1 : x0;
    const int y1 = (y0 + 1 < height) ? y0 + 1 : y0;
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const float v00 = image[y0 * width + x0];
    const float v10 = image[y0 * width + x1];
    const float v01 = image[y1 * width + x0];
    const float v11 = image[y1 * width + x1];

    const float v0 = v00 + fx * (v10 - v00);
    const float v1 = v01 + fx * (v11 - v01);
    return v0 + fy * (v1 - v0);
}
