#include "DepthMeshRenderer.h"
#include "AndroidOut.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
constexpr char kDepthMeshVertexShader[] = R"(
    #version 300 es
    precision highp float;

    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec3 a_Normal;
    layout(location = 2) in float a_Alpha;

    uniform mat4 u_MVP;

    out vec3 v_Normal;
    out float v_Alpha;

    void main() {
        gl_Position = u_MVP * vec4(a_Position, 1.0);
        v_Normal = a_Normal;
        v_Alpha = a_Alpha;
    }
)";

constexpr char kDepthMeshFragmentShader[] = R"(
    #version 300 es
    precision mediump float;

    uniform vec3 u_LightDir;
    uniform vec3 u_Color;
    uniform float u_Alpha;

    in vec3 v_Normal;
    in float v_Alpha;

    out vec4 fragColor;

    void main() {
        if (v_Alpha < 0.01) {
            discard;
        }
        vec3 normal = normalize(v_Normal);
        float lambert = max(dot(normal, normalize(u_LightDir)), 0.0);
        vec3 color = u_Color * (0.3 + 0.7 * lambert);
        fragColor = vec4(color, u_Alpha * v_Alpha);
    }
)";
}

DepthMeshRenderer::DepthMeshRenderer() = default;

DepthMeshRenderer::~DepthMeshRenderer() {
    if (vertex_buffer_) {
        glDeleteBuffers(1, &vertex_buffer_);
    }
    if (triangle_index_buffer_) {
        glDeleteBuffers(1, &triangle_index_buffer_);
    }
    if (line_index_buffer_) {
        glDeleteBuffers(1, &line_index_buffer_);
    }
    if (shader_program_) {
        glDeleteProgram(shader_program_);
    }
}

void DepthMeshRenderer::Initialize(int grid_width, int grid_height) {
    if (initialized_) return;

    grid_width_ = std::max(2, grid_width);
    grid_height_ = std::max(2, grid_height);

    const int vertex_count = grid_width_ * grid_height_;
    vertices_.resize(static_cast<size_t>(vertex_count));

    triangle_indices_.reserve(static_cast<size_t>((grid_width_ - 1) * (grid_height_ - 1) * 6));
    for (int y = 0; y < grid_height_ - 1; ++y) {
        for (int x = 0; x < grid_width_ - 1; ++x) {
            const uint16_t i0 = static_cast<uint16_t>(y * grid_width_ + x);
            const uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            const uint16_t i2 = static_cast<uint16_t>(i0 + grid_width_);
            const uint16_t i3 = static_cast<uint16_t>(i2 + 1);
            triangle_indices_.push_back(i0);
            triangle_indices_.push_back(i2);
            triangle_indices_.push_back(i1);
            triangle_indices_.push_back(i1);
            triangle_indices_.push_back(i2);
            triangle_indices_.push_back(i3);
        }
    }
    triangle_index_count_ = static_cast<int>(triangle_indices_.size());

    line_indices_.reserve(static_cast<size_t>((grid_width_ - 1) * grid_height_ * 2 + (grid_height_ - 1) * grid_width_ * 2));
    for (int y = 0; y < grid_height_; ++y) {
        for (int x = 0; x < grid_width_ - 1; ++x) {
            const uint16_t i0 = static_cast<uint16_t>(y * grid_width_ + x);
            line_indices_.push_back(i0);
            line_indices_.push_back(static_cast<uint16_t>(i0 + 1));
        }
    }
    for (int y = 0; y < grid_height_ - 1; ++y) {
        for (int x = 0; x < grid_width_; ++x) {
            const uint16_t i0 = static_cast<uint16_t>(y * grid_width_ + x);
            line_indices_.push_back(i0);
            line_indices_.push_back(static_cast<uint16_t>(i0 + grid_width_));
        }
    }
    line_index_count_ = static_cast<int>(line_indices_.size());

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    const GLchar* vert_src = kDepthMeshVertexShader;
    glShaderSource(vertex_shader, 1, &vert_src, nullptr);
    glCompileShader(vertex_shader);

    GLint compiled = 0;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_length = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char log[512];
            glGetShaderInfoLog(vertex_shader, 512, nullptr, log);
            aout << "DepthMesh vertex shader error: " << log << std::endl;
        }
        glDeleteShader(vertex_shader);
        return;
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar* frag_src = kDepthMeshFragmentShader;
    glShaderSource(fragment_shader, 1, &frag_src, nullptr);
    glCompileShader(fragment_shader);

    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_length = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char log[512];
            glGetShaderInfoLog(fragment_shader, 512, nullptr, log);
            aout << "DepthMesh fragment shader error: " << log << std::endl;
        }
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return;
    }

    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vertex_shader);
    glAttachShader(shader_program_, fragment_shader);
    glLinkProgram(shader_program_);

    GLint linked = 0;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        aout << "DepthMesh shader link failed" << std::endl;
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    mvp_uniform_ = glGetUniformLocation(shader_program_, "u_MVP");
    light_dir_uniform_ = glGetUniformLocation(shader_program_, "u_LightDir");
    color_uniform_ = glGetUniformLocation(shader_program_, "u_Color");
    alpha_uniform_ = glGetUniformLocation(shader_program_, "u_Alpha");

    glGenBuffers(1, &vertex_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &triangle_index_buffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, triangle_index_buffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, triangle_indices_.size() * sizeof(uint16_t),
                 triangle_indices_.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &line_index_buffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_index_buffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, line_indices_.size() * sizeof(uint16_t),
                 line_indices_.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    initialized_ = true;
    aout << "DepthMeshRenderer initialized: " << grid_width_ << "x" << grid_height_ << std::endl;
}

void DepthMeshRenderer::Update(const DepthFrame& depth_frame,
                               int camera_image_width,
                               int camera_image_height,
                               float fx,
                               float fy,
                               float cx,
                               float cy,
                               const float* world_from_camera,
                               float min_depth_m,
                               float max_depth_m) {
    if (!initialized_ || depth_frame.width <= 0 || depth_frame.height <= 0 ||
        camera_image_width <= 0 || camera_image_height <= 0 || !depth_frame.depth_data) {
        has_mesh_ = false;
        valid_ratio_ = 0.0f;
        return;
    }

    const float scale_x = static_cast<float>(depth_frame.width) / static_cast<float>(camera_image_width);
    const float scale_y = static_cast<float>(depth_frame.height) / static_cast<float>(camera_image_height);
    const float fx_depth = fx * scale_x;
    const float fy_depth = fy * scale_y;
    const float cx_depth = cx * scale_x;
    const float cy_depth = cy * scale_y;

    const float step_x = (depth_frame.width - 1) / static_cast<float>(grid_width_ - 1);
    const float step_y = (depth_frame.height - 1) / static_cast<float>(grid_height_ - 1);

    int valid_count = 0;
    const int vertex_count = grid_width_ * grid_height_;

    for (int y = 0; y < grid_height_; ++y) {
        const int sample_y = static_cast<int>(std::round(y * step_y));
        const uint8_t* depth_row = reinterpret_cast<const uint8_t*>(depth_frame.depth_data) +
                                   depth_frame.row_stride * sample_y;
        const uint8_t* conf_row = depth_frame.confidence_data
            ? depth_frame.confidence_data + depth_frame.confidence_row_stride * sample_y
            : nullptr;

        for (int x = 0; x < grid_width_; ++x) {
            const int sample_x = static_cast<int>(std::round(x * step_x));
            const uint16_t* depth_pixel = reinterpret_cast<const uint16_t*>(
                depth_row + depth_frame.pixel_stride * sample_x);
            const uint16_t depth_mm = *depth_pixel;

            const int vertex_index = y * grid_width_ + x;
            Vertex& vtx = vertices_[static_cast<size_t>(vertex_index)];

            bool valid = depth_mm != 0;
            float depth_m = static_cast<float>(depth_mm) * 0.001f;

            if (valid) {
                if (depth_m < min_depth_m || depth_m > max_depth_m) {
                    valid = false;
                }
            }

            if (valid && conf_row) {
                const uint8_t* conf_pixel = conf_row + depth_frame.confidence_pixel_stride * sample_x;
                const uint8_t confidence = *conf_pixel;
                if (confidence < 128) {
                    valid = false;
                }
            }

            if (!valid) {
                vtx.position[0] = 0.0f;
                vtx.position[1] = 0.0f;
                vtx.position[2] = 0.0f;
                vtx.normal[0] = 0.0f;
                vtx.normal[1] = 1.0f;
                vtx.normal[2] = 0.0f;
                vtx.alpha = 0.0f;
                vtx.padding = 0.0f;
                continue;
            }

            const float x_cam = (static_cast<float>(sample_x) - cx_depth) * depth_m / fx_depth;
            const float y_cam = (static_cast<float>(sample_y) - cy_depth) * depth_m / fy_depth;
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

            vtx.position[0] = world_x;
            vtx.position[1] = world_y;
            vtx.position[2] = world_z;
            vtx.alpha = 1.0f;
            vtx.padding = 0.0f;
            valid_count++;
        }
    }

    for (int y = 0; y < grid_height_; ++y) {
        for (int x = 0; x < grid_width_; ++x) {
            const int index = y * grid_width_ + x;
            Vertex& vtx = vertices_[static_cast<size_t>(index)];
            if (vtx.alpha < 0.5f) {
                vtx.normal[0] = 0.0f;
                vtx.normal[1] = 1.0f;
                vtx.normal[2] = 0.0f;
                continue;
            }

            const int right_index = index + 1;
            const int down_index = index + grid_width_;
            if (x >= grid_width_ - 1 || y >= grid_height_ - 1) {
                vtx.normal[0] = 0.0f;
                vtx.normal[1] = 1.0f;
                vtx.normal[2] = 0.0f;
                continue;
            }

            const Vertex& right = vertices_[static_cast<size_t>(right_index)];
            const Vertex& down = vertices_[static_cast<size_t>(down_index)];
            if (right.alpha < 0.5f || down.alpha < 0.5f) {
                vtx.normal[0] = 0.0f;
                vtx.normal[1] = 1.0f;
                vtx.normal[2] = 0.0f;
                continue;
            }

            float vx[3] = {
                right.position[0] - vtx.position[0],
                right.position[1] - vtx.position[1],
                right.position[2] - vtx.position[2]
            };
            float vy[3] = {
                down.position[0] - vtx.position[0],
                down.position[1] - vtx.position[1],
                down.position[2] - vtx.position[2]
            };

            float normal[3] = {
                vx[1] * vy[2] - vx[2] * vy[1],
                vx[2] * vy[0] - vx[0] * vy[2],
                vx[0] * vy[1] - vx[1] * vy[0]
            };
            Normalize(normal);
            vtx.normal[0] = normal[0];
            vtx.normal[1] = normal[1];
            vtx.normal[2] = normal[2];
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices_.size() * sizeof(Vertex), vertices_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    valid_ratio_ = vertex_count > 0
        ? static_cast<float>(valid_count) / static_cast<float>(vertex_count)
        : 0.0f;
    has_mesh_ = valid_count > 0;
}

void DepthMeshRenderer::Draw(const float* view_matrix, const float* projection_matrix, bool wireframe) const {
    if (!initialized_ || !has_mesh_) return;

    float mvp[16];
    MultiplyMatrix(mvp, projection_matrix, view_matrix);

    glUseProgram(shader_program_);
    glUniformMatrix4fv(mvp_uniform_, 1, GL_FALSE, mvp);
    glUniform3f(light_dir_uniform_, 0.3f, 1.0f, 0.4f);
    glUniform3f(color_uniform_, 0.2f, 0.55f, 1.0f);
    glUniform1f(alpha_uniform_, 0.5f);

    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, alpha));

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (wireframe) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_index_buffer_);
        glDrawElements(GL_LINES, line_index_count_, GL_UNSIGNED_SHORT, nullptr);
    } else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, triangle_index_buffer_);
        glDrawElements(GL_TRIANGLES, triangle_index_count_, GL_UNSIGNED_SHORT, nullptr);
    }

    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glUseProgram(0);
}

void DepthMeshRenderer::Clear() {
    valid_ratio_ = 0.0f;
    has_mesh_ = false;
}

void DepthMeshRenderer::MultiplyMatrix(float* out, const float* a, const float* b) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[i * 4 + j] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                out[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
            }
        }
    }
}

void DepthMeshRenderer::Normalize(float* v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-5f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    } else {
        v[0] = 0.0f;
        v[1] = 1.0f;
        v[2] = 0.0f;
    }
}
