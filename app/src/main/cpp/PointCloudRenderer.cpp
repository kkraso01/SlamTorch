#include "PointCloudRenderer.h"
#include "AndroidOut.h"
#include <cstring>
#include <algorithm>

namespace {
    constexpr char kVertexShader[] = R"(
        #version 300 es
        precision highp float;
        
        layout(location = 0) in vec4 a_Position;
        
        uniform mat4 u_MVP;
        uniform float u_PointSize;
        
        void main() {
            gl_Position = u_MVP * a_Position;
            gl_PointSize = u_PointSize;
        }
    )";

    constexpr char kFragmentShader[] = R"(
        #version 300 es
        precision mediump float;
        
        uniform vec4 u_Color;
        
        out vec4 fragColor;
        
        void main() {
            // Make points circular
            vec2 coord = gl_PointCoord - vec2(0.5);
            if (dot(coord, coord) > 0.25) discard;
            fragColor = u_Color;
        }
    )";
    
    // Matrix multiplication: result = a * b (4x4)
    void MatrixMultiply(float* result, const float* a, const float* b) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result[i * 4 + j] = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    result[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
                }
            }
        }
    }
}

PointCloudRenderer::PointCloudRenderer() {
    point_buffer_.fill(0.0f);
}

PointCloudRenderer::~PointCloudRenderer() {
    if (vertex_buffer_) glDeleteBuffers(1, &vertex_buffer_);
    if (shader_program_) glDeleteProgram(shader_program_);
}

void PointCloudRenderer::Initialize() {
    if (initialized_) return;
    
    // Compile vertex shader
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    const GLchar* vert_src = kVertexShader;
    glShaderSource(vertex_shader, 1, &vert_src, nullptr);
    glCompileShader(vertex_shader);
    
    GLint compiled;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_length;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char log[512];
            glGetShaderInfoLog(vertex_shader, 512, nullptr, log);
            aout << "PointCloud vertex shader error: " << log << std::endl;
        }
        return;
    }
    
    // Compile fragment shader
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar* frag_src = kFragmentShader;
    glShaderSource(fragment_shader, 1, &frag_src, nullptr);
    glCompileShader(fragment_shader);
    
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_length;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char log[512];
            glGetShaderInfoLog(fragment_shader, 512, nullptr, log);
            aout << "PointCloud fragment shader error: " << log << std::endl;
        }
        glDeleteShader(vertex_shader);
        return;
    }
    
    // Link program
    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vertex_shader);
    glAttachShader(shader_program_, fragment_shader);
    glLinkProgram(shader_program_);
    
    GLint linked;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        aout << "PointCloud shader link failed" << std::endl;
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    // Get uniform locations
    mvp_uniform_ = glGetUniformLocation(shader_program_, "u_MVP");
    point_size_uniform_ = glGetUniformLocation(shader_program_, "u_PointSize");
    color_uniform_ = glGetUniformLocation(shader_program_, "u_Color");
    
    // Create dynamic vertex buffer
    glGenBuffers(1, &vertex_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, point_buffer_.size() * sizeof(float), 
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    initialized_ = true;
    aout << "PointCloudRenderer initialized successfully" << std::endl;
}

void PointCloudRenderer::Draw(const ArSession* session, const ArPointCloud* point_cloud,
                               const float* view_matrix, const float* projection_matrix) {
    if (!initialized_ || !point_cloud || !session) return;
    
    // Get point count from ARCore
    int32_t num_points_arcore = 0;
    ArPointCloud_getNumberOfPoints(session, point_cloud, &num_points_arcore);
    
    if (num_points_arcore == 0) return;
    
    static int log_count = 0;
    if (log_count++ % 60 == 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "SlamTorch", "PointCloudRenderer::Draw() - %d points", num_points_arcore);
    }
    
    // Clamp to our buffer size
    num_points_ = std::min(static_cast<int>(num_points_arcore), kMaxPoints);
    
    // Get point data (xyz format, 4 floats per point with confidence)
    const float* point_data = nullptr;
    ArPointCloud_getData(session, point_cloud, &point_data);
    
    if (!point_data) {
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Point cloud data is NULL!");
        return;
    }
    
    if (!point_data) return;
    
    // Copy points (ARCore gives vec4: x,y,z,confidence)
    // We just copy as-is since our buffer is vec4 aligned
    std::memcpy(point_buffer_.data(), point_data, 
                num_points_ * 4 * sizeof(float));
    
    // Upload to GPU (orphan + reupload pattern for streaming)
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, point_buffer_.size() * sizeof(float), 
                 nullptr, GL_DYNAMIC_DRAW);  // Orphan
    glBufferSubData(GL_ARRAY_BUFFER, 0, 
                    num_points_ * 4 * sizeof(float), 
                    point_buffer_.data());
    
    // Compute MVP matrix (no heap allocation)
    float mvp_matrix[16];
    MatrixMultiply(mvp_matrix, projection_matrix, view_matrix);
    
    // Set GL state
    glUseProgram(shader_program_);
    glUniformMatrix4fv(mvp_uniform_, 1, GL_FALSE, mvp_matrix);
    glUniform1f(point_size_uniform_, 15.0f);  // Larger points for visibility
    glUniform4f(color_uniform_, 0.31f, 0.78f, 0.47f, 1.0f);  // Green
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)0);
    
    // Enable depth test but allow points to write over background
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);  // Allow points at same depth
    glDepthMask(GL_TRUE);
    
    // Enable blending for better visibility
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Draw points
    glDrawArrays(GL_POINTS, 0, num_points_);
    
    static int draw_log = 0;
    if (draw_log++ % 60 == 0) {
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Drew %d points with GL_POINTS", num_points_);
    }
    
    // Cleanup
    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
