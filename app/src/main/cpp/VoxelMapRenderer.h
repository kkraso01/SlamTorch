#ifndef SLAMTORCH_VOXEL_MAP_RENDERER_H
#define SLAMTORCH_VOXEL_MAP_RENDERER_H

#include <GLES3/gl3.h>

class VoxelMapRenderer {
public:
    void Initialize();
    void UpdatePoints(const float* points, int point_count);
    void Draw(const float* view, const float* proj);
    int GetPointCount() const { return point_count_; }

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint mvp_uniform_ = -1;
    GLint point_size_uniform_ = -1;
    int point_count_ = 0;
};

#endif // SLAMTORCH_VOXEL_MAP_RENDERER_H
