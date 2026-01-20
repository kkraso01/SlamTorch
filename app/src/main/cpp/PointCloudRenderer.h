#ifndef SLAMTORCH_POINT_CLOUD_RENDERER_H
#define SLAMTORCH_POINT_CLOUD_RENDERER_H

#include <GLES3/gl3.h>
#include "arcore/arcore_c_api.h"
#include <array>

// Renders ARCore point cloud with zero allocations
// Uses fixed-size ring buffer for points
class PointCloudRenderer {
public:
    PointCloudRenderer();
    ~PointCloudRenderer();

    void Initialize();
    
    // Draw point cloud with given view and projection matrices
    void Draw(const ArSession* session, const ArPointCloud* point_cloud,
              const float* view_matrix, const float* projection_matrix);

private:
    static constexpr int kMaxPoints = 16384;  // Max points to visualize
    
    GLuint shader_program_ = 0;
    GLuint vertex_buffer_ = 0;
    
    // Uniforms
    GLint mvp_uniform_ = -1;
    GLint point_size_uniform_ = -1;
    GLint color_uniform_ = -1;
    
    // Preallocated point buffer (xyz for each point)
    std::array<float, kMaxPoints * 4> point_buffer_;  // vec4 for alignment
    int num_points_ = 0;
    
    bool initialized_ = false;
};

#endif // SLAMTORCH_POINT_CLOUD_RENDERER_H
