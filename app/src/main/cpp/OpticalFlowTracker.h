#ifndef SLAMTORCH_OPTICAL_FLOW_TRACKER_H
#define SLAMTORCH_OPTICAL_FLOW_TRACKER_H

#include <cstdint>

class OpticalFlowTracker {
public:
    struct Track {
        float x = 0.0f;
        float y = 0.0f;
        float prev_x = 0.0f;
        float prev_y = 0.0f;
        float error = 0.0f;
        int age = 0;
        int stable_count = 0;
        bool active = false;
    };

    OpticalFlowTracker(int max_features, int pyramid_levels);
    ~OpticalFlowTracker();

    void Reset();
    void Initialize(int width, int height);
    bool Update(const uint8_t* image, int width, int height);

    int GetTrackCount() const { return track_count_; }
    const Track* GetTracks() const { return tracks_; }
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    bool HasImage() const { return has_prev_; }

private:
    void AllocatePyramids();
    void BuildPyramid(const uint8_t* src, uint8_t** pyramid);
    void SwapPyramids();
    void DetectFeatures(const uint8_t* image);
    bool TrackFeature(int track_index);
    float TrackFeatureAtLevel(int level, float x, float y, float* out_x, float* out_y);
    float SampleBilinear(const uint8_t* image, int width, int height, float x, float y) const;

    int max_features_ = 0;
    int pyramid_levels_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool has_prev_ = false;

    uint8_t** pyramid_prev_ = nullptr;
    uint8_t** pyramid_curr_ = nullptr;
    int* level_widths_ = nullptr;
    int* level_heights_ = nullptr;

    Track* tracks_ = nullptr;
    int track_count_ = 0;

    int reseed_threshold_ = 0;
};

#endif // SLAMTORCH_OPTICAL_FLOW_TRACKER_H
