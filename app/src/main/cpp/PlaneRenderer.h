#ifndef SLAMTORCH_PLANE_RENDERER_H
#define SLAMTORCH_PLANE_RENDERER_H

#include <GLES3/gl3.h>
#include <array>
#include "arcore/arcore_c_api.h"

class PlaneRenderer {
public:
    PlaneRenderer();
    ~PlaneRenderer();

    void Initialize(const ArSession* session);
    void Update(const ArSession* session, const ArTrackableList* plane_list);
    void Draw(const float* view_matrix, const float* projection_matrix) const;

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }
    int GetPlaneCount() const { return plane_count_; }

private:
    struct PlaneDrawInfo {
        int vertex_start = 0;
        int vertex_count = 0;
        int index_start = 0;
        int index_count = 0;
        ArPlaneType type = AR_PLANE_HORIZONTAL_UPWARD_FACING;
    };

    static constexpr int kMaxPlanes = 64;
    static constexpr int kMaxVerticesPerPlane = 128;
    static constexpr int kMaxVertices = kMaxPlanes * kMaxVerticesPerPlane;
    static constexpr int kMaxIndices = kMaxPlanes * (kMaxVerticesPerPlane - 2) * 3;

    void UploadBuffers();
    int TriangulatePolygon(const float* polygon, int vertex_count, uint16_t base_index,
                           uint16_t* out_indices, int max_indices) const;
    static void MultiplyMatrix(float* out, const float* a, const float* b);

    GLuint shader_program_ = 0;
    GLuint vertex_buffer_ = 0;
    GLuint index_buffer_ = 0;

    GLint mvp_uniform_ = -1;
    GLint color_uniform_ = -1;

    std::array<PlaneDrawInfo, kMaxPlanes> plane_draw_info_{};
    std::array<float, kMaxVertices * 3> vertices_{};
    std::array<uint16_t, kMaxIndices> indices_{};

    int plane_count_ = 0;
    int vertex_count_ = 0;
    int index_count_ = 0;

    bool initialized_ = false;
    bool enabled_ = true;

    ArPose* plane_pose_ = nullptr;
};

#endif // SLAMTORCH_PLANE_RENDERER_H
