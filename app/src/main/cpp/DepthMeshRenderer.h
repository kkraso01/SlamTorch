#ifndef SLAMTORCH_DEPTH_MESH_RENDERER_H
#define SLAMTORCH_DEPTH_MESH_RENDERER_H

#include <GLES3/gl3.h>
#include <vector>
#include "DepthFrame.h"

class DepthMeshRenderer {
public:
    DepthMeshRenderer();
    ~DepthMeshRenderer();

    void Initialize(int grid_width, int grid_height);
    void Update(const DepthFrame& depth_frame,
                int camera_image_width,
                int camera_image_height,
                float fx,
                float fy,
                float cx,
                float cy,
                const float* world_from_camera,
                float min_depth_m,
                float max_depth_m);
    void Draw(const float* view_matrix, const float* projection_matrix, bool wireframe) const;
    void Clear();

    float GetValidRatio() const { return valid_ratio_; }
    int GetGridWidth() const { return grid_width_; }
    int GetGridHeight() const { return grid_height_; }
    bool HasMesh() const { return has_mesh_; }

private:
    struct Vertex {
        float position[3];
        float normal[3];
        float alpha;
        float padding; // Keep 4-float alignment.
    };

    static void MultiplyMatrix(float* out, const float* a, const float* b);
    static void Normalize(float* v);

    GLuint shader_program_ = 0;
    GLuint vertex_buffer_ = 0;
    GLuint triangle_index_buffer_ = 0;
    GLuint line_index_buffer_ = 0;

    GLint mvp_uniform_ = -1;
    GLint light_dir_uniform_ = -1;
    GLint color_uniform_ = -1;
    GLint alpha_uniform_ = -1;

    std::vector<Vertex> vertices_;
    std::vector<uint16_t> triangle_indices_;
    std::vector<uint16_t> line_indices_;

    int grid_width_ = 0;
    int grid_height_ = 0;
    int triangle_index_count_ = 0;
    int line_index_count_ = 0;

    float valid_ratio_ = 0.0f;
    bool initialized_ = false;
    bool has_mesh_ = false;
};

#endif // SLAMTORCH_DEPTH_MESH_RENDERER_H
