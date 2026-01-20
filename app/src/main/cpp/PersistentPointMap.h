#ifndef SLAMTORCH_PERSISTENT_POINT_MAP_H
#define SLAMTORCH_PERSISTENT_POINT_MAP_H

#include <GLES3/gl3.h>
#include <cstdint>
#include <unordered_set>

// Zero-allocation persistent point map for ARCore SLAM visualization
class PersistentPointMap {
public:
    PersistentPointMap(int max_points = 500000);
    ~PersistentPointMap();

    // Add points from ARCore point cloud (camera space -> world space)
    // world_from_camera: 4x4 column-major transform matrix
    // points: float4 array (xyzw with confidence in w)
    // num_points: number of points in array
    void AddPoints(const float* world_from_camera, const float* points, int num_points);

    // Render accumulated map with given view/projection matrices
    void Draw(const float* view_matrix, const float* projection_matrix);

    // Clear all accumulated points
    void Clear();

    // Diagnostics
    int GetPointCount() const { return current_count_; }
    int GetTotalAdded() const { return total_added_; }
    bool IsBufferWrapped() const { return has_wrapped_; }

private:
    static constexpr int MAX_POINTS = 500000;  // Production-grade: 500k points
    static constexpr float MAX_DISTANCE = 10.0f;  // Extended range
    static constexpr int DECIMATION = 2;  // Keep 1/2 of input points
    static constexpr float VOXEL_SIZE = 0.02f;  // 2cm voxel for high density

    // Fixed-size ring buffer
    float* point_buffer_ = nullptr;  // 3 floats per point (xyz)
    int current_count_ = 0;
    int write_index_ = 0;
    int total_added_ = 0;
    bool has_wrapped_ = false;

    // Spatial deduplication (simple voxel hash)
    struct VoxelKey {
        int16_t x, y, z;
        bool operator==(const VoxelKey& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    struct VoxelHash {
        size_t operator()(const VoxelKey& k) const {
            return ((size_t)k.x << 32) | ((size_t)k.y << 16) | (size_t)k.z;
        }
    };
    std::unordered_set<VoxelKey, VoxelHash> voxel_set_;

    // OpenGL resources
    GLuint vbo_ = 0;
    GLuint vao_ = 0;
    GLuint program_ = 0;
    GLint mvp_uniform_ = -1;
    GLint point_size_uniform_ = -1;

    // Pre-allocated temp buffer for transform
    float* temp_transformed_ = nullptr;

    // Helper functions
    void InitGL();
    void CleanupGL();
    VoxelKey GetVoxelKey(float x, float y, float z) const;
    bool ShouldAddPoint(float x, float y, float z) const;
    void TransformPoint(const float* mat, float x, float y, float z, float* out) const;
    void UpdateGLBuffer();
};

#endif // SLAMTORCH_PERSISTENT_POINT_MAP_H
