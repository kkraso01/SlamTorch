#include "PersistentPointMap.h"
#include "AndroidOut.h"
#include <android/log.h>
#include <cstring>
#include <cmath>

namespace {
    const char* VERTEX_SHADER = R"(
        #version 300 es
        precision highp float;
        
        uniform mat4 u_MVP;
        uniform float u_PointSize;
        
        layout(location = 0) in vec3 a_Position;
        
        out float v_Depth;
        
        void main() {
            gl_Position = u_MVP * vec4(a_Position, 1.0);
            gl_PointSize = u_PointSize;
            v_Depth = gl_Position.z / gl_Position.w;
        }
    )";

    const char* FRAGMENT_SHADER = R"(
        #version 300 es
        precision mediump float;
        
        in float v_Depth;
        out vec4 FragColor;
        
        void main() {
            vec2 coord = gl_PointCoord - vec2(0.5);
            if (length(coord) > 0.5) discard;
            
            // Color based on depth: close = cyan, far = blue
            float depth_norm = clamp(v_Depth * 0.5 + 0.5, 0.0, 1.0);
            vec3 color = mix(vec3(0.0, 0.9, 0.9), vec3(0.0, 0.3, 0.8), depth_norm);
            FragColor = vec4(color, 0.8);
        }
    )";
}

PersistentPointMap::PersistentPointMap(int max_points) {
    // Allocate fixed-size buffers
    point_buffer_ = new float[MAX_POINTS * 3];
    temp_transformed_ = new float[MAX_POINTS * 3];
    memset(point_buffer_, 0, MAX_POINTS * 3 * sizeof(float));
    
    InitGL();
    
    __android_log_print(ANDROID_LOG_INFO, "SlamTorch", 
        "PersistentPointMap initialized: max=%d points, voxel_size=%.3fm, decimation=1/%d",
        MAX_POINTS, VOXEL_SIZE, DECIMATION);
}

PersistentPointMap::~PersistentPointMap() {
    CleanupGL();
    delete[] point_buffer_;
    delete[] temp_transformed_;
}

void PersistentPointMap::InitGL() {
    // Compile vertex shader
    GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert_shader, 1, &VERTEX_SHADER, nullptr);
    glCompileShader(vert_shader);
    
    GLint compiled = 0;
    glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(vert_shader, 512, nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Map vertex shader error: %s", log);
    }

    // Compile fragment shader
    GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag_shader, 1, &FRAGMENT_SHADER, nullptr);
    glCompileShader(frag_shader);
    
    glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(frag_shader, 512, nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Map fragment shader error: %s", log);
    }

    // Link program
    program_ = glCreateProgram();
    glAttachShader(program_, vert_shader);
    glAttachShader(program_, frag_shader);
    glLinkProgram(program_);
    
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program_, 512, nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Map shader link error: %s", log);
    }

    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);

    // Get uniform locations
    mvp_uniform_ = glGetUniformLocation(program_, "u_MVP");
    point_size_uniform_ = glGetUniformLocation(program_, "u_PointSize");

    // Create VAO and VBO
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, MAX_POINTS * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void PersistentPointMap::CleanupGL() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

PersistentPointMap::VoxelKey PersistentPointMap::GetVoxelKey(float x, float y, float z) const {
    return VoxelKey{
        static_cast<int16_t>(std::floor(x / VOXEL_SIZE)),
        static_cast<int16_t>(std::floor(y / VOXEL_SIZE)),
        static_cast<int16_t>(std::floor(z / VOXEL_SIZE))
    };
}

bool PersistentPointMap::ShouldAddPoint(float x, float y, float z) const {
    // Check distance
    float dist_sq = x*x + y*y + z*z;
    if (dist_sq > MAX_DISTANCE * MAX_DISTANCE) {
        return false;
    }
    
    // Check voxel deduplication
    VoxelKey key = GetVoxelKey(x, y, z);
    return voxel_set_.find(key) == voxel_set_.end();
}

void PersistentPointMap::TransformPoint(const float* mat, float x, float y, float z, float* out) const {
    // mat is column-major 4x4
    out[0] = mat[0]*x + mat[4]*y + mat[8]*z  + mat[12];
    out[1] = mat[1]*x + mat[5]*y + mat[9]*z  + mat[13];
    out[2] = mat[2]*x + mat[6]*y + mat[10]*z + mat[14];
}

void PersistentPointMap::AddPoints(const float* world_from_camera, const float* points, int num_points) {
    if (!points || num_points == 0) return;

    static int log_counter = 0;
    int points_added = 0;
    
    // Decimate and transform points
    for (int i = 0; i < num_points; i += DECIMATION) {
        float cx = points[i * 4 + 0];
        float cy = points[i * 4 + 1];
        float cz = points[i * 4 + 2];
        float confidence = points[i * 4 + 3];
        
        // Filter by confidence (production-grade: only high-confidence points)
        if (confidence < 0.3f) continue;
        
        // Transform to world space
        float wx, wy, wz;
        TransformPoint(world_from_camera, cx, cy, cz, temp_transformed_ + points_added * 3);
        wx = temp_transformed_[points_added * 3 + 0];
        wy = temp_transformed_[points_added * 3 + 1];
        wz = temp_transformed_[points_added * 3 + 2];
        
        // Filter by distance and voxel
        if (ShouldAddPoint(wx, wy, wz)) {
            // Add to ring buffer
            int idx = write_index_ * 3;
            
            // If overwriting, remove old voxel
            if (current_count_ == MAX_POINTS) {
                float old_x = point_buffer_[idx + 0];
                float old_y = point_buffer_[idx + 1];
                float old_z = point_buffer_[idx + 2];
                VoxelKey old_key = GetVoxelKey(old_x, old_y, old_z);
                voxel_set_.erase(old_key);
                has_wrapped_ = true;
            }
            
            // Add new point
            point_buffer_[idx + 0] = wx;
            point_buffer_[idx + 1] = wy;
            point_buffer_[idx + 2] = wz;
            
            VoxelKey key = GetVoxelKey(wx, wy, wz);
            voxel_set_.insert(key);
            
            write_index_ = (write_index_ + 1) % MAX_POINTS;
            if (current_count_ < MAX_POINTS) {
                current_count_++;
            }
            points_added++;
            total_added_++;
        }
    }
    
    // Log periodically
    if (log_counter++ % 60 == 0 && points_added > 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "SlamTorch",
            "Map: added %d/%d points, total=%d, wrapped=%d",
            points_added, num_points, current_count_, has_wrapped_);
    }
    
    // Update GL buffer if we added points
    if (points_added > 0) {
        UpdateGLBuffer();
    }
}

void PersistentPointMap::UpdateGLBuffer() {
    if (current_count_ == 0) return;
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    
    // Orphan buffer for better performance
    glBufferData(GL_ARRAY_BUFFER, MAX_POINTS * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    // Upload current points
    glBufferSubData(GL_ARRAY_BUFFER, 0, current_count_ * 3 * sizeof(float), point_buffer_);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void PersistentPointMap::Draw(const float* view_matrix, const float* projection_matrix) {
    if (current_count_ == 0) return;

    // Compute MVP (projection * view)
    float mvp[16];
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += projection_matrix[k * 4 + row] * view_matrix[col * 4 + k];
            }
            mvp[col * 4 + row] = sum;
        }
    }

    glUseProgram(program_);
    glUniformMatrix4fv(mvp_uniform_, 1, GL_FALSE, mvp);
    glUniform1f(point_size_uniform_, 10.0f);  // 10px for dense production map

    glBindVertexArray(vao_);
    
    // Enable blending for semi-transparent points
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    
    glDrawArrays(GL_POINTS, 0, current_count_);
    
    glBindVertexArray(0);
}

void PersistentPointMap::Clear() {
    current_count_ = 0;
    write_index_ = 0;
    total_added_ = 0;
    has_wrapped_ = false;
    voxel_set_.clear();
    memset(point_buffer_, 0, MAX_POINTS * 3 * sizeof(float));
    
    __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "PersistentPointMap cleared");
}
