#ifndef SLAMTORCH_LANDMARK_MAP_H
#define SLAMTORCH_LANDMARK_MAP_H

#include <GLES3/gl3.h>
#include <cstdint>

class LandmarkMap {
public:
    struct Landmark {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float bearing[3] = {0.0f, 0.0f, -1.0f};
        float confidence = 0.0f;
        int age = 0;
        int last_seen = 0;
        int seen_count = 0;
        bool has_metric_depth = false;
    };

    explicit LandmarkMap(int max_points);
    ~LandmarkMap();

    void BeginFrame();
    void AddMetricObservation(const float* world_pos, const float* bearing, float confidence);
    void AddBearingObservation(const float* bearing, float confidence);
    void Draw(const float* view_matrix, const float* projection_matrix, const float* world_from_camera);
    void Clear();

    int GetPointCount() const { return point_count_; }
    int GetMetricCount() const;
    int GetBearingCount() const;
    int GetFrameIndex() const { return frame_index_; }

private:
    void InitGL();
    void CleanupGL();
    void UpdateGLBuffer(const float* world_from_camera);
    void BuildColor(float confidence, int age, bool has_metric_depth, float* out_rgba) const;

    int max_points_ = 0;
    Landmark* landmarks_ = nullptr;
    int point_count_ = 0;
    int write_index_ = 0;
    int frame_index_ = 0;

    // GL resources
    GLuint vbo_ = 0;
    GLuint vao_ = 0;
    GLuint program_ = 0;
    GLint mvp_uniform_ = -1;

    struct Vertex {
        float x, y, z;
        float r, g, b, a;
    };
    Vertex* vertex_buffer_ = nullptr;
};

#endif // SLAMTORCH_LANDMARK_MAP_H
